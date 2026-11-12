#include "Application.hpp"

#include "../game/EclInterpreter.hpp"

#include <cstdio>
#include <cstring>

namespace th185::app {

namespace {

// Frame RNG. Seeded once so a replay of the same tick count is reproducible.
Rng g_rng;
bool g_rng_ready = false;

constexpr u32 kSceneCount = 9;

// ECL files follow the scene naming: world01t.ecl / world01.ecl ...
const char* kSceneScripts[] = {
    "world01t.ecl", "world01.ecl", "world02.ecl", "world03.ecl", "world04.ecl",
    "world05.ecl", "world06.ecl", "world07.ecl", "world08.ecl",
};

}  // namespace

Application& instance() {
    static Application app;
    return app;
}

bool Application::initialize(std::vector<u8> archive_bytes) {
    archive_bytes_ = std::move(archive_bytes);
    if (archive_bytes_.empty()) {
        error_ = "archive is empty";
        phase_ = Phase::Failed;
        return false;
    }
    if (!archive_.parse(archive_bytes_, &error_)) {
        phase_ = Phase::Failed;
        return false;
    }
    if (!session_.open(archive_bytes_, &error_)) {
        phase_ = Phase::Failed;
        return false;
    }
    if (!load_stage(0)) {
        phase_ = Phase::Failed;
        return false;
    }
    // The scene file names its own animation bank, so load THAT instead of a
    // hardcoded one. Falling back to enemy.anm keeps the shell showing
    // something when a scene names a bank we cannot decode.
    load_scene_banks();
    raster_.resize(640, 480);
    render();
    phase_ = Phase::Idle;
    stats_ = FrameStats{};
    return true;
}

bool Application::register_stage_scripts(u32 stage_index) {
    if (stage_index >= kSceneCount) {
        error_ = "stage index out of range";
        return false;
    }

    // Register the scene script plus default.ecl. default.ecl holds the shared
    // subroutines that the scene scripts call, so without it Call resolution
    // fails for most of the stage logic.
    const char* wanted[2] = { kSceneScripts[stage_index], "default.ecl" };
    EclResource resource;
    bool any = false;
    for (const char* name : wanted) {
        std::size_t entry = archive_.entries.size();
        for (std::size_t i = 0; i < archive_.entries.size(); ++i) {
            if (archive_.entries[i].name == name) { entry = i; break; }
        }
        if (entry == archive_.entries.size()) continue;
        std::vector<u8> blob = archive_.extract(archive_bytes_, entry, &error_);
        if (blob.empty()) continue;
        EclResource part;
        if (!part.parse(std::move(blob), &error_)) continue;
        any = true;
        // Merge into `resource` by moving its subroutines and data.
        if (resource.data.empty()) resource = std::move(part);
        else {
            for (EclSubroutine& sub : part.subs) resource.subs.push_back(std::move(sub));
        }
    }
    if (!any) {
        // No scripts is not fatal: the stage can still be inspected.
        runnable_ = 0;
        return true;
    }

    // Reset the context and register every subroutine, remembering which ones
    // are fully implemented so tick() only runs those.
    ecl_ = EclContext{};
    active_scripts_.clear();
    runnable_ = 0;
    for (const EclSubroutine& sub : resource.subs) {
        std::vector<u8> code;
        u32 code_base = 0;
        if (!sub.instructions.empty()) {
            code_base = sub.instructions.front().offset;
            const u32 end = sub.instructions.back().offset +
                            sub.instructions.back().size;
            if (code_base < resource.data.size() && end <= resource.data.size()) {
                code.assign(resource.data.begin() + code_base,
                            resource.data.begin() + end);
            }
        }
        bool all_implemented = !sub.instructions.empty();
        for (const EclInstruction& ins : sub.instructions) {
            if (!EclInterpreter::is_supported(ins.id)) {
                all_implemented = false;
                break;
            }
        }
        // `main` is the scene controller: it is where op1013 schedules live, so
        // it has to run even when some of its opcodes are still unimplemented.
        // The interpreter stops cleanly on an unsupported opcode, so a partial
        // run still executes everything before that point -- which is where the
        // schedules are emitted.
        const bool is_controller = sub.name == "main";
        if (all_implemented || is_controller) {
            // The controller runs FIRST. Every other script in the file is a
            // helper it calls by name; running them standalone spends the
            // per-frame budget on their wait loops before the controller ever
            // gets a turn, which is why `main` never advanced.
            if (is_controller) active_scripts_.insert(active_scripts_.begin(), sub.name);
            else active_scripts_.push_back(sub.name);
            ++runnable_;
        }
        ecl_.add_subroutine(sub.name, std::move(code));
        ecl_.subroutines[sub.name].code_base = code_base;
    }
    return true;
}

bool Application::load_animation_bank(const char* name, u32 script_limit) {
    // Load one .anm and decode its textures. A scene needs several banks:
    // worldNN.anm is the BACKGROUND (one sprite), enemy.anm holds the enemies.
    // Sprites are numbered per bank, so each bank keeps its own texture array.
    std::size_t entry = archive_.entries.size();
    for (std::size_t i = 0; i < archive_.entries.size(); ++i) {
        if (archive_.entries[i].name == name) { entry = i; break; }
    }
    if (entry == archive_.entries.size()) return false;

    std::vector<u8> blob = archive_.extract(archive_bytes_, entry, &error_);
    if (blob.empty()) return false;

    banks_.push_back(AnimationBank{});
    AnimationBank& bank = banks_.back();
    bank.name = name;
    if (!bank.resource.parse(std::move(blob), &error_)) {
        banks_.pop_back();
        return false;
    }

    bank.textures.reserve(bank.resource.textures.size());
    for (const AnmTexture& texture : bank.resource.textures) {
        bank.textures.push_back(decode_texture(
            texture.format, texture.width, texture.height,
            bank.resource.data.data() + texture.offset + 16, texture.size));
    }

    if (script_limit == 0) return true;

    // Lay this bank's roots out on a grid. Banks are drawn in load order, so
    // the background (loaded first) ends up behind the enemies.
    const u32 limit = u32(bank.resource.script_list.size() < script_limit
                          ? bank.resource.script_list.size() : script_limit);
    const u32 columns = 8;
    const float row_base = float(banks_.size() - 1) * 2.0f - 1.5f;
    for (u32 s = 0; s < limit; ++s) {
        const float column = float(s % columns);
        const float row = float(s / columns);
        animations_.create(&bank.resource, i32(s), 0, 0xFFFFFFFFu,
                           (column - float(columns - 1) * 0.5f) * 64.0f,
                           (row + row_base - 1.0f) * 64.0f);
    }

    if (!animation_bank_.empty()) animation_bank_ += '+';
    animation_bank_ += name;
    return true;
}
void Application::load_scene_banks() {
    banks_.clear();
    animations_ = AnmManager{};
    animation_bank_.clear();

    // Background first so it is drawn behind everything else.
    load_animation_bank(session_.stage.anm_name.c_str(), 1);
    // Then the enemies, which is where the visible content actually lives.
    load_animation_bank("enemy.anm", 32);
    // The player atlas is its own bank; load it without auto-spawning roots
    // because Player owns exactly one instance.
    load_animation_bank("pl01.anm", 0);
    // The bullet atlas is needed for the shot sprites; no auto-spawn because
    // BulletPool owns the instances.
    load_animation_bank("bullet.anm", 0);

    // Bind the player to the bank that was just appended.
    if (!banks_.empty()) {
        const AnimationBank& player_bank = banks_.back();
        if (player_bank.name == "pl01.anm") {
            player_.bind(&animations_, &player_bank.resource);
        }
    }
}

void Application::consume_schedules() {
    // op1013 carries a wave schedule: "spawn wave `slot` at these 8 frame
    // numbers". The interpreter records them; this turns them into enemies.
    //
    // The interpreter is not a coroutine -- `main` is re-run from the start every
    // tick and stops at its first unsupported opcode, so the SAME schedules get
    // recorded again each frame. Keying on (slot, time) rather than on the vector
    // index makes the firing idempotent, which is what the original does: a
    // schedule entry is a one-shot timer, not a per-frame event.
    const auto& schedules = ecl_.schedules;
    stats_.schedules_total = u32(schedules.size());

    for (const EclContext::SpawnSchedule& schedule : schedules) {
        for (u32 i = 0; i < 8; ++i) {
            const u32 due = schedule.times[i];
            if (due == 0) continue;
            if (stats_.ticks < due) continue;

            // Pack (slot, time) into one key. Slot is < 32 and time < 2^27 for
            // every observed value, so a u64 is enough and collisions cannot
            // happen within the data.
            const u64 key = (u64(schedule.slot) << 32) | u64(due);
            if (fired_schedule_keys_.count(key)) continue;
            fired_schedule_keys_.insert(key);
            ++stats_.schedules_fired;

            // Spread slots across the top of the field. Slots come in pairs
            // (0/1, 4/5, 8/9, 12/13, 16/17), so the pair index gives a column.
            const u32 pair = schedule.slot / 4;
            const float x = (float(pair) - 2.0f) * 60.0f;
            const float y = -180.0f;

            // The wave template selected by op1011 picks the sprite and the
            // drift speed. Falling back to template 0 keeps the game playable
            // for scenes that do not declare one.
            const i32 wave = ecl_.last_wave_template;
            const u32 template_index = wave >= 0 ? u32(wave) : 0u;
            // The enemy bank has 120 sprites; the template is used modulo that
            // so every template gets a distinct look without an explicit table.
            const u32 sprite = template_index % 120u;
            // Higher templates drift a little faster, which makes the wave
            // progression visible without inventing per-template data.
            const float speed = 0.30f + float(template_index % 8u) * 0.05f;
            enemies_.spawn(x, y, 0.0f, speed, 16.0f, 2, sprite);
        }
    }
    stats_.enemies_alive = enemies_.stats().alive;
}
void Application::reset_scenario() {
    bullets_.clear();
    enemies_.clear();
    player_ = Player{};
    // Re-bind the player to its bank: the reset dropped the animation handle.
    for (const AnimationBank& bank : banks_) {
        if (bank.name == "pl01.anm") {
            player_.bind(&animations_, &bank.resource);
            break;
        }
    }
    stats_.collision_hits = 0;
    stats_.player_hits_taken = 0;
    ecl_.coroutines.clear();
    fired_schedule_keys_.clear();
    stats_.schedules_fired = 0;
    stats_.player_lives = player_.lives();
}

u32 Application::spawn_test_wave(u32 count) {
    // Drifting targets spread across the upper field. They move slowly downward
    // so they stay in range of the player's shots long enough to be hit.
    u32 spawned = 0;
    for (u32 i = 0; i < count; ++i) {
        // Keep the targets near the player's firing line. The player's shots are
        // two straight columns at x = +-6, so a wide spread would never be hit --
        // which is exactly what the first collision test revealed.
        const float column = float(i % 3);
        const float row = float(i / 3);
        const float x = (column - 1.0f) * 6.0f;
        const float y = -160.0f + row * 40.0f;
        // Radius follows the sprite size the enemy bank actually uses (32..64),
        // halved because the hitbox is a circle around the sprite centre.
        const float radius = 16.0f;
        if (enemies_.spawn(x, y, 0.0f, 0.35f, radius, 3, 0)) ++spawned;
    }
    // Publish the counts immediately. tick() also refreshes them, but a caller
    // that spawns and then reads would otherwise see the stale value -- which is
    // exactly what made the spawn test look like a failure.
    stats_.enemies_alive = enemies_.stats().alive;
    return spawned;
}

void Application::render() {
    if (raster_.width() == 0) {
        raster_.resize(640, 480);
    }
    // Opaque dark clear: a transparent framebuffer composites to white in
    // every viewer and hides the geometry.
    raster_.clear(Rgba8{24, 24, 48, 255});
    // AnmVm knows which bank it came from, so it can resolve its own texture;
    // the manager only needs a way to ask. Passing a null array makes it draw
    // untextured, which is why the banks are handed over individually.
    u32 quads = 0;
    for (const AnimationBank& bank : banks_) {
        quads += animations_.render_bank(
            raster_, &bank.resource,
            bank.textures.empty() ? nullptr : bank.textures.data(),
            u32(bank.textures.size()),
            float(raster_.width()) * 0.5f,
            float(raster_.height()) * 0.5f);
    }
    stats_.quads = quads;

    // Enemies, drawn from the enemy bank when available.
    const AnimationBank* enemy_bank = nullptr;
    for (const AnimationBank& bank : banks_) {
        if (bank.name == "enemy.anm") { enemy_bank = &bank; break; }
    }
    for (const Enemy& enemy : enemies_.enemies()) {
        if (!enemy.alive) continue;

        AnmQuad quad;
        const float half = enemy.radius * 2.0f;
        quad.x[0] = float(raster_.width()) * 0.5f + enemy.x - half;
        quad.y[0] = float(raster_.height()) * 0.5f + enemy.y - half;
        quad.x[1] = quad.x[0] + half * 2.0f;
        quad.y[1] = quad.y[0];
        quad.x[2] = quad.x[1];
        quad.y[2] = quad.y[0] + half * 2.0f;
        quad.x[3] = quad.x[0];
        quad.y[3] = quad.y[2];

        quad.u[0] = 0.0f;
        quad.v[0] = 0.0f;
        quad.u[1] = 1.0f;
        quad.v[1] = 0.0f;
        quad.u[2] = 1.0f;
        quad.v[2] = 1.0f;
        quad.u[3] = 0.0f;
        quad.v[3] = 1.0f;
        quad.blend = AnmBlendMode::Alpha;
        quad.nearest = true;

        // The wave template chose the sprite, so draw THAT one.
        if (enemy_bank && !enemy_bank->textures.empty() &&
            !enemy_bank->resource.sprites.empty()) {
            const u32 sprite_index =
                enemy.sprite < enemy_bank->resource.sprites.size()
                    ? enemy.sprite : 0u;
            const AnmSprite& sprite =
                enemy_bank->resource.sprites[sprite_index];
            quad.u[0] = sprite.u0;
            quad.v[0] = sprite.v0;
            quad.u[1] = sprite.u1;
            quad.v[1] = sprite.v0;
            quad.u[2] = sprite.u1;
            quad.v[2] = sprite.v1;
            quad.u[3] = sprite.u0;
            quad.v[3] = sprite.v1;
            if (sprite.texture < enemy_bank->textures.size()) {
                quad.texture = &enemy_bank->textures[sprite.texture];
            }
        }

        raster_.draw(quad);
        ++quads;
    }
    stats_.quads = quads;
    // Bullets are drawn from the bullet bank when it is loaded, otherwise as
    // tinted squares. Either way the position and lifetime are the real thing.
    const AnimationBank* bullet_bank = nullptr;
    for (const AnimationBank& bank : banks_) {
        if (bank.name == "bullet.anm") { bullet_bank = &bank; break; }
    }
    for (const Bullet& bullet : bullets_.bullets()) {
        if (!bullet.alive) continue;

        AnmQuad quad;
        const float half = 8.0f;
        quad.x[0] = float(raster_.width()) * 0.5f + bullet.x - half;
        quad.y[0] = float(raster_.height()) * 0.5f + bullet.y - half;
        quad.x[1] = quad.x[0] + half * 2.0f;
        quad.y[1] = quad.y[0];
        quad.x[2] = quad.x[1];
        quad.y[2] = quad.y[0] + half * 2.0f;
        quad.x[3] = quad.x[0];
        quad.y[3] = quad.y[2];

        quad.u[0] = 0.0f;
        quad.v[0] = 0.0f;
        quad.u[1] = 1.0f;
        quad.v[1] = 0.0f;
        quad.u[2] = 1.0f;
        quad.v[2] = 1.0f;
        quad.u[3] = 0.0f;
        quad.v[3] = 1.0f;

        quad.tint = Rgba8{u8((bullet.color >> 16) & 0xFF),
                          u8((bullet.color >> 8) & 0xFF),
                          u8(bullet.color & 0xFF),
                          u8((bullet.color >> 24) & 0xFF)};
        quad.blend = AnmBlendMode::Additive;
        quad.nearest = true;

        if (bullet_bank && !bullet_bank->textures.empty() &&
            bullet.sprite < bullet_bank->resource.sprites.size()) {
            const AnmSprite& sprite =
                bullet_bank->resource.sprites[bullet.sprite];
            quad.u[0] = sprite.u0;
            quad.v[0] = sprite.v0;
            quad.u[1] = sprite.u1;
            quad.v[1] = sprite.v0;
            quad.u[2] = sprite.u1;
            quad.v[2] = sprite.v1;
            quad.u[3] = sprite.u0;
            quad.v[3] = sprite.v1;
            const u32 texture_index = sprite.texture;
            if (texture_index < bullet_bank->textures.size()) {
                quad.texture = &bullet_bank->textures[texture_index];
            }
        }

        raster_.draw(quad);
        ++quads;
    }
    stats_.quads = quads;
    stats_.pixels_written = raster_.pixels_written;
    stats_.live_animations = animations_.live_count();
}

bool Application::load_stage(u32 stage_index) {
    if (!session_.load_stage(stage_index, &error_)) return false;
    stage_index_ = stage_index;
    if (!register_stage_scripts(stage_index)) return false;

    // Each scene names its own animation bank; load it so the picture matches
    // the stage rather than whatever was loaded before.
    load_scene_banks();
    spawn_test_wave(16);
    render();
    return true;
}

bool Application::tick(u32 held) {
    // Input is recorded but not yet consumed: no player entity exists, so there
    // is nothing to steer. Keeping the parameter plumbed means the shell can
    // already send it and wiring the player will not change this signature.
    stats_.held_buttons = held;
    if (phase_ == Phase::Failed) return false;
    if (phase_ != Phase::Running) return true;

    ++stats_.ticks;

    // Run every runnable subroutine once per tick with a bounded step budget.
    // This is where a real frame loop would also render, mix audio and poll
    // input; none of those exist yet.
    for (const std::string& name : active_scripts_) {
        const auto found = ecl_.subroutines.find(name);
        if (found == ecl_.subroutines.end()) continue;

        // Coroutine entry: survives op23 waits so the scene controller can
        // advance past its wait loops. A plain run() restarts at instruction 0
        // every frame and therefore never leaves the wait.
        // Run against a COPY, then merge ONLY the fields the callee is
        // expected to change. Writing the whole context back would replace
        // the coroutine table and the recorded wave templates with the
        // snapshot taken before this script ran -- which is exactly why
        // WorldWave01 was called repeatedly but its op1011 template never
        // appeared: the next script's write-back discarded it.
        EclInterpreter runner;
        runner.context = ecl_;
        runner.run_coroutine(name, found->second.code,
                             found->second.code_base);

        // Merge: coroutines (progress), stacks are inside them, and the
        // records the interpreter accumulates. Everything else (frames,
        // builtins, subroutines) is read-only here.
        ecl_.coroutines = std::move(runner.context.coroutines);
        ecl_.calls_made = std::move(runner.context.calls_made);
        ecl_.missing_calls = std::move(runner.context.missing_calls);
        ecl_.wave_templates = std::move(runner.context.wave_templates);
        ecl_.last_wave_template = runner.context.last_wave_template;
        ecl_.schedules = std::move(runner.context.schedules);
        ecl_.recorded = std::move(runner.context.recorded);
        ecl_.last_timer_value = runner.context.last_timer_value;
        ecl_.timer_writes = runner.context.timer_writes;


        stats_.ecl_instructions += runner.stats().instructions;
        stats_.ecl_faults += runner.stats().fault_count;
        stats_.ecl_unsupported += runner.stats().unsupported_count;
        if (stats_.ecl_first_unsupported == 0 &&
            runner.stats().unsupported_count > 0) {
            stats_.ecl_first_unsupported = runner.stats().first_unsupported_opcode;
        }
        // One-shot diagnostic: report which subroutines the controller reached.
        static bool reported_calls = false;
        if (!reported_calls && !ecl_.calls_made.empty() && stats_.ticks > 5) {
            reported_calls = true;
            std::printf("ECL_MISSING %zu:", ecl_.missing_calls.size());
            for (const std::string& miss : ecl_.missing_calls) {
                std::printf(" %s", miss.c_str());
            }
            std::printf("\n");
            std::printf("ECL_CALLS %zu:", ecl_.calls_made.size());
            for (const std::string& call : ecl_.calls_made) {
                std::printf(" %s", call.c_str());
            }
            std::printf("\n");
        }

        if (runner.stats().max_stack_depth > stats_.max_stack_depth) {
            stats_.max_stack_depth = runner.stats().max_stack_depth;
        }
    }

    // Input drives the player, which drives its animation instance.
    player_.update(held, &bullets_);
    bullets_.update();
    enemies_.update();
    // Collision runs after both pools have moved, so a bullet and a target that
    // crossed paths this frame are still tested against each other.
    // Accumulate: a per-frame value would read as 0 whenever the last frame had
    // no hit, which is what made the first collision test look like a failure.
    stats_.collision_hits += enemies_.resolve_collisions(&bullets_);
    stats_.enemies_alive = enemies_.stats().alive;
    stats_.enemies_killed = enemies_.stats().killed;

    // ECL schedules create enemies over time.
    consume_schedules();

    // Enemies fire aimed shots at the player.
    enemies_.fire_at(&bullets_, player_.x(), player_.y());

    // Player vs hostile bullets. The player's hitbox is 2 units, far smaller
    // than its 30x46 sprite -- that gap is what makes grazing feel fair.
    const float hitbox = player_.hitbox_radius();
    for (u32 i = 0; i < BULLET_CAPACITY; ++i) {
        Bullet& bullet = bullets_.mutable_bullet(i);
        if (!bullet.alive || !bullet.hostile) continue;
        const float dx = bullet.x - player_.x();
        const float dy = bullet.y - player_.y();
        if (dx * dx + dy * dy > hitbox * hitbox) continue;
        bullets_.release(i);
        if (player_.take_hit()) ++stats_.player_hits_taken;
    }
    stats_.player_lives = player_.lives();
    stats_.enemy_shots = enemies_.stats().enemy_shots;
    stats_.player_x = player_.x();
    stats_.player_y = player_.y();
    stats_.player_focused = player_.focused();
    stats_.player_shots = player_.stats().shots;
    stats_.player_bombs = player_.stats().bombs;
    stats_.bullets_alive = bullets_.stats().alive;
    stats_.bullets_fired = player_.stats().bullets_fired;

    // Advance the animation tree, then draw it.
    animations_.update(rng_, 1.0f);
    render();

    ++stats_.frame;
    return true;
}

bool Application::start() {
    if (phase_ == Phase::Failed) return false;
    if (phase_ == Phase::Running) return true;
    phase_ = Phase::Running;
    return true;
}

bool Application::pause() {
    if (phase_ != Phase::Running) return false;
    phase_ = Phase::Paused;
    return true;
}

bool Application::resume() {
    if (phase_ != Phase::Paused) return false;
    phase_ = Phase::Running;
    return true;
}

bool Application::restart() {
    if (archive_bytes_.empty()) {
        error_ = "nothing loaded";
        return false;
    }
    stats_ = FrameStats{};
    if (!session_.load_stage(0, &error_)) return false;
    stage_index_ = 0;
    if (!register_stage_scripts(0)) return false;
    phase_ = Phase::Idle;
    return true;
}

}  // namespace th185::app
