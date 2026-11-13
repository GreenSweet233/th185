#include "Application.hpp"

#include "../game/EclInterpreter.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
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

namespace {

// A boss script's entry subroutine is named BossNN or BossNNt (the tutorial
// variant). Boss01Boss, Boss01Boss1, Boss01BossCard1 and friends are helpers.
// The boss SCENE controller is BossNNtest / BossNNttest, not BossNN.
//
// Boss01test is the entry: it opens with op40 (EnterFrame), carries nine
// op1013 wave schedules, then op300 Call Boss01 (the boss body) and
// op11 Call WaveSync, then waits on builtin slot 104 with the op1019 / op1017
// pair -- exactly the shape worldNN.main has.
//
// Boss01 itself is the BODY that the controller calls: it opens with op40 /
// op502 / op30 / op1006 / op1024 / op307 then op300 Call Boss01Boss. Running
// both as top-level coroutines would run the body twice.
bool is_boss_entry(const std::string& name) {
    // BossNNtest (10 chars) or BossNNttest (11 chars).
    if (name.size() != 10 && name.size() != 11) return false;
    if (name.compare(0, 4, "Boss") != 0) return false;
    if (name[4] < '0' || name[4] > '9') return false;
    if (name[5] < '0' || name[5] > '9') return false;
    // The suffix is "test" for BossNNtest and "ttest" for BossNNttest;
    // compare from the end so the offset is derived, not hardcoded.
    const char* tail = (name.size() == 11) ? "ttest" : "test";
    const std::size_t tail_len = (name.size() == 11) ? 5u : 4u;
    return name.compare(name.size() - tail_len, tail_len, tail) == 0;
}

}  // namespace
bool Application::register_stage_scripts(u32 stage_index) {
    if (stage_index >= kSceneCount) {
        error_ = "stage index out of range";
        return false;
    }

    // Load the scene layout first: its group table holds the real formation
    // coordinates that op1013 slots index into.
    stage_ = StageScript{};
    {
        static const char* kStageFiles[kSceneCount] = {
            "world01t.std", "world01.std", "world02.std", "world03.std",
            "world04.std", "world05.std", "world06.std", "world07.std",
            "world08.std",
        };
        std::size_t entry = archive_.entries.size();
        for (std::size_t k = 0; k < archive_.entries.size(); ++k) {
            if (archive_.entries[k].name == kStageFiles[stage_index]) {
                entry = k;
                break;
            }
        }
        if (entry < archive_.entries.size()) {
            std::string extract_error;
            std::vector<u8> blob =
                archive_.extract(archive_bytes_, entry, &extract_error);
            if (!blob.empty()) {
                std::string parse_error;
                if (!stage_.parse(std::move(blob), &parse_error)) {
                    // A layout we cannot parse is not fatal: the scene still
                    // runs, it just falls back to the synthetic formation.
                    stage_ = StageScript{};
                }
            }
        }
    }

    // Register the scene script, default.ecl, and every waveNN.ecl.
    //
    // waveNN.ecl are the WAVE TEMPLATE libraries: op1011 selects a template
    // index and the engine runs that file's subroutines. worldNN.ecl only
    // calls its own WorldWaveNN helpers, so without the wave libraries the
    // bullet-emitting subroutines (Wave01_GirlA_at and friends) are never
    // reachable -- which is why bullet_requests stayed at 0.
    std::vector<std::string> wanted;
    wanted.push_back(kSceneScripts[stage_index]);
    wanted.push_back("default.ecl");
    // Boss scripts. They are separate files (bossNN.ecl) that worldNN.ecl never
    // calls by name -- the engine picks one from the stage's card. Loading all
    // 27 makes their subroutines reachable; which one runs is a separate
    // question this build does not answer yet.
    {
        char boss[32];
        for (u32 i = 1; i <= 27; ++i) {
            std::snprintf(boss, sizeof(boss), "boss%02u.ecl", i);
            wanted.push_back(boss);
        }
    }
    {
        char name[32];
        for (u32 i = 1; i <= 74; ++i) {
            std::snprintf(name, sizeof(name), "wave%02u.ecl", i);
            wanted.push_back(name);
        }
    }
    // Reset the context BEFORE registering: add_subroutine writes into it, so
    // clearing afterwards would discard every subroutine just loaded.
    ecl_ = EclContext{};
    active_scripts_.clear();
    runnable_ = 0;

    EclResource resource;
    bool any = false;
    for (const std::string& name : wanted) {
        std::size_t entry = archive_.entries.size();
        for (std::size_t i = 0; i < archive_.entries.size(); ++i) {
            if (archive_.entries[i].name == name) { entry = i; break; }
        }

        if (entry == archive_.entries.size()) continue;
        std::vector<u8> blob = archive_.extract(archive_bytes_, entry, &error_);
        if (blob.empty()) continue;
        EclResource part;
        const bool parsed = part.parse(std::move(blob), &error_);

        if (!parsed) continue;
        any = true;
        // Each file's subroutines carry offsets into THAT file's data, so the
        // code slice has to be taken here, while `part.data` is still the
        // right buffer. Merging first and slicing later would use the first
        // file's data for every file -- which is why loading the 74 wave
        // libraries broke the scene controller's own code.
        for (const EclSubroutine& sub : part.subs) {
            std::vector<u8> code;
            u32 code_base = 0;
            if (!sub.instructions.empty()) {
                code_base = sub.instructions.front().offset;
                const u32 end = sub.instructions.back().offset +
                                sub.instructions.back().size;
                if (code_base < part.data.size() && end <= part.data.size()) {
                    code.assign(part.data.begin() + code_base,
                                part.data.begin() + end);
                }
            }
            if (code.empty()) continue;
            EclSubroutine sliced = sub;
            sliced.instructions.clear();
            ecl_.add_subroutine(sub.name, std::move(code));
            ecl_.subroutines[sub.name].code_base = code_base;
            resource.subs.push_back(std::move(sliced));
        }
    }
    if (!any) {
        // No scripts is not fatal: the stage can still be inspected.
        runnable_ = 0;
        return true;
    }

    // Registration already happened while slicing (each file's offsets are
    // relative to its own data). All that remains is deciding which
    // subroutines run as top-level coroutines.
    for (const EclSubroutine& sub : resource.subs) {
        // Top-level coroutines are the ENTRY points, not every subroutine.
        //
        //   main       the scene controller
        //   BossNN     a boss script's entry (Boss01, Boss02, ...)
        //
        // Everything else is a helper the entry reaches through Call, and
        // running helpers standalone burns the per-frame budget on their own
        // wait loops -- world04's helpers alone spent ~80000 instructions per
        // frame, which starved the controller before it reached op1013.
        //
        // BossNN was identified as an entry point by its shape: no file outside
        // its own calls it, and it starts with op40 / op502 / op30 / op1006 /
        // op1024 / op307 then op300 Call BossNNBoss -- the same prologue main
        // uses.
        if (sub.name == "main" || is_boss_entry(sub.name)) {
            active_scripts_.push_back(sub.name);
            ++runnable_;
        }
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

            // Three levels of layout data, in order of authority:
            //
            //   1. The wave entry whose slot_id names a group -> that group's
            //      coordinates (world04: slot_id 1,0,5,4 against 15 groups).
            //   2. The wave entry's own f1/f2 position, when it has no group.
            //   3. A synthetic row, when the scene has no wave entries at all
            //      (the tutorial stages declare waves but no groups).
            //
            // All .std coordinates live in a 384x448 space, so subtract half
            // to centre them on the field.
            float x = 0.0f;
            float y = -180.0f;
            float group_size = 16.0f;
            bool placed = false;

            // Level 1: op1013 slot indexes the GROUP table directly.
            //
            // The slot values are the same 9 numbers in every scene
            // (0,1,4,5,8,9,12,13,16), so they are channel ids, not wave
            // indices -- matching them against wave.slot_id only ever hit
            // 1-2 of 9 entries. Taking the slot modulo the group count keeps
            // every group reachable and spreads the wave across the field.
            const StageWave* wave_entry = nullptr;
            for (const StageWave& candidate : stage_.waves) {
                if (candidate.slot_id == schedule.slot) { wave_entry = &candidate; break; }
            }
            const u32 group_index = schedule.slot % stage_.groups.size();
            if (!stage_.groups.empty()) {
                wave_entry = nullptr;   // use the group path below
            }
            if (!stage_.groups.empty()) {
                const StageGroup& group = stage_.groups[group_index];
                // The group table is in the SCENE's own coordinate space, and
                // that space differs per stage:
                //   world05/06/07/08 span 250..710 units (field-sized)
                //   world04 spans only 38 x 293, centred near (115, 146)
                // Feeding world04's raw values into a 384x448 field put every
                // enemy in a single column.
                //
                // Normalise by the group table's own bounding box, then map to
                // the playfield: that works for both shapes without per-scene
                // constants.
                float min_x = stage_.groups[0].x, max_x = min_x;
                float min_y = stage_.groups[0].y, max_y = min_y;
                for (const StageGroup& g : stage_.groups) {
                    if (g.x < min_x) min_x = g.x;
                    if (g.x > max_x) max_x = g.x;
                    if (g.y < min_y) min_y = g.y;
                    if (g.y > max_y) max_y = g.y;
                }
                const float span_x = max_x - min_x;
                const float span_y = max_y - min_y;
                const float centre_x = (min_x + max_x) * 0.5f;
                const float centre_y = (min_y + max_y) * 0.5f;
                // Target span: most of the 384-wide field, and most of the
                // 448-tall one. A degenerate axis (all groups in one line)
                // keeps the centre instead of dividing by zero.
                constexpr float FIELD_W = 320.0f;
                constexpr float FIELD_H = 360.0f;
                const float scale_x = span_x > 1.0f ? FIELD_W / span_x : 0.0f;
                const float scale_y = span_y > 1.0f ? FIELD_H / span_y : 0.0f;
                x = (group.x - centre_x) * scale_x;
                y = (group.y - centre_y) * scale_y;
                if (group.size_a > 0.0f) group_size = group.size_a;
                placed = true;
            }
            // Level 2: the wave entry's own position. f1/f2 are NOT a spawn
            // point -- world01t's first entry reads 1024/512, which is the
            // field extent, not a location -- so this level is deliberately
            // left unused rather than guessed at.
            // Level 3: synthetic row.
            if (!placed) {
                const u32 pair = schedule.slot / 4;
                x = (float(pair) - 2.0f) * 60.0f;
            }

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


            enemies_.spawn(x, y, 0.0f, speed, group_size, 2, sprite);
        }
    }
    stats_.enemies_alive = enemies_.stats().alive;

    // Run the wave templates the scene just declared.
    //
    // op1011 appends a template index and op1012 closes the block, so
    // wave_blocks holds one list per WorldWaveNN invocation. Template N lives
    // in waveNN.ecl; loading it and running its subroutines is what finally
    // reaches the bullet-emitting code (WaveNN_GirlA_at and friends).
    for (const std::vector<i32>& block : ecl_.wave_blocks) {
        for (i32 template_index : block) {
            if (template_index <= 0) continue;
            const u64 key = u64(0x80000000u) | u64(u32(template_index));
            if (fired_schedule_keys_.count(key)) continue;
            fired_schedule_keys_.insert(key);

            char bank[32];
            std::snprintf(bank, sizeof(bank), "wave%02d.ecl", int(template_index));
            std::size_t entry = archive_.entries.size();
            for (std::size_t k = 0; k < archive_.entries.size(); ++k) {
                if (archive_.entries[k].name == bank) { entry = k; break; }
            }
            if (entry >= archive_.entries.size()) continue;

            std::string wave_error;
            std::vector<u8> blob = archive_.extract(archive_bytes_, entry, &wave_error);
            if (blob.empty()) continue;
            EclResource wave_resource;
            if (!wave_resource.parse(std::move(blob), &wave_error)) continue;

            // Run every subprogram in the template once. The pattern
            // recorders (op600..op611, op535) then fill bullet_requests,
            // which the loop below turns into real bullets.
            for (const EclSubroutine& sub : wave_resource.subs) {
                std::vector<u8> code;
                u32 code_base = 0;
                if (!sub.instructions.empty()) {
                    code_base = sub.instructions.front().offset;
                    const u32 end = sub.instructions.back().offset +
                                    sub.instructions.back().size;
                    if (code_base < wave_resource.data.size() &&
                        end <= wave_resource.data.size()) {
                        code.assign(wave_resource.data.begin() + code_base,
                                    wave_resource.data.begin() + end);
                    }
                }
                if (code.empty()) continue;
                ecl_.add_subroutine(sub.name, code);
                ecl_.subroutines[sub.name].code_base = code_base;

                EclInterpreter runner;
                runner.context = ecl_;
                runner.run(code, 4000, code_base);
                ecl_.bullet_requests = std::move(runner.context.bullet_requests);
                ecl_.delays_seen = std::move(runner.context.delays_seen);
                ecl_.requests_total = runner.context.requests_total;
            }
        }
    }
    ecl_.wave_blocks.clear();

    // Turn recorded bullet patterns into real bullets. Each request is one
    // emission: N bullets spread over [angle_lo, angle_hi] at the recorded
    // speed, aimed along the enemy's facing. Delays are per-bullet frame
    // counts, so a request fans out over time instead of all at once.
    for (const EclContext::BulletRequest& request : ecl_.bullet_requests) {
        const u32 count = request.count > 0 ? request.count : u32(request.angles.size());
        if (count == 0) continue;
        const float speed = request.speed > 0.0f ? request.speed : 3.0f;
        const float lo = request.angle_lo;
        const float hi = request.angle_hi;
        for (u32 i = 0; i < count; ++i) {
            // Angle: explicit op44 pushes win; otherwise interpolate the range.
            float angle = 0.0f;
            if (i < request.angles.size()) {
                angle = request.angles[i];
            } else if (count > 1) {
                angle = lo + (hi - lo) * (float(i) / float(count - 1));
            }
            const float vx = std::sin(angle) * speed;
            const float vy = std::cos(angle) * speed;
            // Spawn from the first live enemy; a pattern belongs to the wave
            // that emitted it, and the enemy pool is where waves live.
            float sx = 0.0f;
            float sy = -200.0f;
            for (u32 e = 0; e < ENEMY_CAPACITY; ++e) {
                const Enemy& enemy = enemies_.enemies()[e];
                if (!enemy.alive) continue;
                sx = enemy.x;
                sy = enemy.y;
                break;
            }
            // op535 gives a per-bullet delay in frames: the fan fires in
            // sequence instead of all at once. A zero entry means "now".
            const u16 delay = u16(request.delays[i % 8]);
            if (bullets_.spawn(sx, sy, vx, vy, 0, 0xFFFF8080u, true, delay)) {
                ++stats_.enemy_shots;
            }
        }
    }
    ecl_.bullet_requests.clear();
    ecl_.pending_bullet = EclContext::BulletRequest{};
}
void Application::reset_scenario() {
    bullets_.clear();
    enemies_.clear();
    // Also retire the animations: the enemy pool owns handles into the
    // animation manager, so clearing one without the other leaves orphan
    // sprites that still render (the frame showed 46 quads with 1 enemy).
    animations_.clear();
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
    scoring_.reset();
    items_.clear();
    last_killed_ = 0;
    last_lives_ = player_.lives();
    stats_.game_over = 0;
    stats_.deaths = 0;
    stats_.clear_frames = 0;
    stats_.boss_kill_frames = 0;
    stats_.frames_since_schedule = 0;
    stats_.stage_clear = 0;
    last_schedules_fired_ = 0;
    boss_spawned_ = false;
    stats_.boss_spawned = 0;
    stats_.bombs_used = 0;
    stats_.bombs_left = player_.bombs_remaining();
    stats_.sounds_played = 0;
    stats_.sounds_dropped = 0;
    stats_.score = 0;
    stats_.grazes = 0;
    stats_.score_capped = 0;
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
        if (bullet.delay > 0) continue;   // queued by op535, not visible yet

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
    // Items use front.anm's pickup icons. Without the bank loaded they fall
    // back to tinted squares, which keeps position and lifetime visible
    // without inventing art.
    const AnimationBank* front_bank = nullptr;
    for (const AnimationBank& bank : banks_) {
        if (bank.name == "front.anm") { front_bank = &bank; break; }
    }
    for (u32 i = 0; i < ITEM_CAPACITY; ++i) {
        const Item& item = items_.items()[i];
        if (!item.alive) continue;

        AnmQuad quad;
        const float half = 11.0f;
        quad.x[0] = float(raster_.width()) * 0.5f + item.x - half;
        quad.y[0] = float(raster_.height()) * 0.5f + item.y - half;
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
        // Life fragments read as pink hearts, stars as green.
        quad.tint = (item.kind == ItemKind::LifeFragment)
                        ? Rgba8{255, 96, 160, 255}
                        : Rgba8{96, 255, 128, 255};
        quad.blend = AnmBlendMode::Alpha;
        quad.nearest = true;

        if (front_bank && !front_bank->textures.empty() &&
            item.sprite < front_bank->resource.sprites.size()) {
            const AnmSprite& sprite = front_bank->resource.sprites[item.sprite];
            quad.u[0] = sprite.u0;
            quad.v[0] = sprite.v0;
            quad.u[1] = sprite.u1;
            quad.v[1] = sprite.v0;
            quad.u[2] = sprite.u1;
            quad.v[2] = sprite.v1;
            quad.u[3] = sprite.u0;
            quad.v[3] = sprite.v1;
            if (sprite.texture < front_bank->textures.size()) {
                quad.texture = &front_bank->textures[sprite.texture];
            }
        }

        raster_.draw(quad);
        ++quads;
    }
    stats_.quads = quads;
    stats_.pixels_written = raster_.pixels_written;
    stats_.live_animations = animations_.live_count();
}

void Application::load_sound_effects() {
    if (!sounds_.samples().empty()) return;   // already loaded

    // The names the game actually needs. Loading all 38 would waste memory on
    // menu sounds this build never triggers.
    static const char* kNames[] = {
        "se_tan00.wav",     // player shot
        "se_graze.wav",     // graze
        "se_item00.wav",    // item pickup
        "se_pldead00.wav",  // player death
        "se_enep00.wav",    // enemy death
        "se_damage00.wav",  // enemy hit
        "se_powerup.wav",   // power up
        "se_extend.wav",    // extra life
    };

    for (const char* name : kNames) {
        std::size_t entry = archive_.entries.size();
        for (std::size_t i = 0; i < archive_.entries.size(); ++i) {
            if (archive_.entries[i].name == name) { entry = i; break; }
        }
        if (entry >= archive_.entries.size()) continue;

        std::string sound_error;
        std::vector<u8> blob = archive_.extract(archive_bytes_, entry, &sound_error);
        if (blob.empty()) continue;
        sounds_.load(name, std::move(blob));
    }
    stats_.sounds_loaded = sounds_.stats().loaded;
}
bool Application::attach_bgm(std::vector<u8> bgm_bytes) {
    if (bgm_bytes.size() < BGM_ZWAV_HEADER) return false;
    if (!(bgm_bytes[0] == 'Z' && bgm_bytes[1] == 'W' && bgm_bytes[2] == 'A' &&
          bgm_bytes[3] == 'V')) {
        return false;
    }

    bgm_bytes_ = std::move(bgm_bytes);
    bgm_.attach_stream(bgm_bytes_.data(), bgm_bytes_.size());

    // The index lives in th185.dat, not in the BGM file.
    load_bgm_index();
    stats_.bgm_tracks = bgm_.stats().tracks;
    return bgm_.stats().tracks > 0;
}
void Application::load_bgm_index() {
    if (bgm_.stats().tracks > 0) return;   // already parsed

    // thbgm.fmt is a small table inside th185.dat; parsing it does not need the
    // 255 MB stream, so the index is available even before attach_bgm().
    std::size_t entry = archive_.entries.size();
    for (std::size_t i = 0; i < archive_.entries.size(); ++i) {
        if (archive_.entries[i].name == "thbgm.fmt") { entry = i; break; }
    }
    if (entry >= archive_.entries.size()) return;

    std::string fmt_error;
    std::vector<u8> fmt_blob = archive_.extract(archive_bytes_, entry, &fmt_error);
    if (fmt_blob.empty()) return;
    bgm_.parse_index(std::move(fmt_blob));
}
bool Application::play_bgm(u32 index) {
    const bool ok = bgm_.play(index);
    stats_.bgm_track = bgm_.current_track();
    stats_.bgm_playing = bgm_.is_playing() ? 1u : 0u;
    return ok;
}

void Application::set_difficulty(u32 difficulty) {
    // Out-of-range tiers fall back to 0 rather than propagating an invalid
    // value into the score-cap table, which only has 5 real entries.
    difficulty_ = is_valid_difficulty(difficulty) ? difficulty : 0u;
    ecl_.set_difficulty(difficulty_);
    scoring_.configure(difficulty_, stage_index_);
    stats_.score_cap = scoring_.cap();
    stats_.difficulty = difficulty_;
}

bool Application::load_stage(u32 stage_index) {
    if (!session_.load_stage(stage_index, &error_)) return false;
    stage_index_ = stage_index;
    if (!register_stage_scripts(stage_index)) return false;

    // Score cap depends on difficulty and stage (Difficulty::score_cap_for_stage
    // mirrors the original's `imul eax, [0x4D1004]`). Difficulty is fixed at 0
    // (Normal) until the shell can select one.
    ecl_.set_difficulty(difficulty_);
    scoring_.configure(difficulty_, stage_index);
    scoring_.reset();

    // Sound effects are ordinary RIFF/WAVE files inside the archive, so they
    // load with the same extractor as everything else. Loading is idempotent
    // per name: a stage reload does not duplicate samples.
    load_sound_effects();
    stats_.sounds_loaded = sounds_.stats().loaded;
    load_bgm_index();
    stats_.bgm_tracks = bgm_.stats().tracks;
    last_killed_ = 0;
    // Publish immediately: a caller that reads the cap right after load_stage
    // must not see the previous stage's value (stats_ is otherwise only synced
    // inside tick).
    stats_.score = scoring_.score();
    stats_.score_cap = scoring_.cap();
    stats_.grazes = 0;
    stats_.score_capped = 0;

    // Drop the previous scene's live objects BEFORE the banks are replaced:
    // their sprites reference textures that are about to be swapped out.
    // Order matters -- clearing after load_scene_banks() would retire the
    // animations the new banks just created.
    animations_.clear();
    bullets_.clear();
    enemies_.clear();
    // Each scene names its own animation bank; load it so the picture matches
    // the stage rather than whatever was loaded before.
    load_scene_banks();
    // No synthetic wave: the ECL scene controller now schedules the real
    // enemies through op1013, so seeding a test wave would only pollute the
    // formation with enemies that are not in the stage data.
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
        ecl_.pending_templates = std::move(runner.context.pending_templates);
        ecl_.wave_blocks = std::move(runner.context.wave_blocks);
        ecl_.bullet_requests = std::move(runner.context.bullet_requests);
        ecl_.delays_seen = std::move(runner.context.delays_seen);
        ecl_.requests_total = runner.context.requests_total;
        ecl_.last_wave_template = runner.context.last_wave_template;
        ecl_.schedules = std::move(runner.context.schedules);
        ecl_.recorded = std::move(runner.context.recorded);
        // The stage-signal flag is state the callee owns: op1017 raises it and
        // op1019 consumes it, and both live in main's coroutine. Without these
        // three lines the flag never leaves the runner copy, so `sets` and
        // `waits` stayed at 0 no matter how long the scene ran.
        ecl_.spawn_commits = runner.context.spawn_commits;
        ecl_.scene_syncs = runner.context.scene_syncs;
        ecl_.scene_sync_count = runner.context.scene_sync_count;
        ecl_.wait_value = runner.context.wait_value;
        ecl_.wait_values = runner.context.wait_values;
        ecl_.angle_normalize_ref = runner.context.angle_normalize_ref;
        ecl_.angle_normalizes = runner.context.angle_normalizes;
        ecl_.named_tail_length = runner.context.named_tail_length;
        ecl_.named_tail_target = std::move(runner.context.named_tail_target);
        ecl_.named_tail_calls = runner.context.named_tail_calls;
        ecl_.float_block = runner.context.float_block;
        ecl_.float_blocks = runner.context.float_blocks;
        ecl_.effect_spawn_mode = runner.context.effect_spawn_mode;
        ecl_.effect_spawn_scale = runner.context.effect_spawn_scale;
        ecl_.effect_spawns = runner.context.effect_spawns;
        ecl_.angle_to_a = runner.context.angle_to_a;
        ecl_.angle_to_b = runner.context.angle_to_b;
        ecl_.angle_to_c = runner.context.angle_to_c;
        ecl_.angle_to_d = runner.context.angle_to_d;
        ecl_.angle_to_e = runner.context.angle_to_e;
        ecl_.angle_to_calls = runner.context.angle_to_calls;
        ecl_.pattern_ext = runner.context.pattern_ext;
        ecl_.pattern_exts = runner.context.pattern_exts;
        ecl_.pattern_count = runner.context.pattern_count;
        ecl_.pattern_counts = runner.context.pattern_counts;
        ecl_.frame_value = runner.context.frame_value;
        ecl_.frame_values = runner.context.frame_values;
        ecl_.boss_extra_a = runner.context.boss_extra_a;
        ecl_.boss_extra_b = runner.context.boss_extra_b;
        ecl_.boss_extra_c = runner.context.boss_extra_c;
        ecl_.boss_extras = runner.context.boss_extras;
        ecl_.boss_config_radius = runner.context.boss_config_radius;
        ecl_.boss_config_flags = runner.context.boss_config_flags;
        ecl_.boss_configs = runner.context.boss_configs;
        ecl_.attack_slots = runner.context.attack_slots;
        ecl_.spawn_count_a = runner.context.spawn_count_a;
        ecl_.spawn_count_b = runner.context.spawn_count_b;
        ecl_.spawn_counts = runner.context.spawn_counts;
        ecl_.spawn_rect_w = runner.context.spawn_rect_w;
        ecl_.spawn_rect_h = runner.context.spawn_rect_h;
        ecl_.spawn_rects = runner.context.spawn_rects;
        ecl_.boss_wait_marks = runner.context.boss_wait_marks;
        ecl_.angle_table = runner.context.angle_table;
        ecl_.angle_tables = runner.context.angle_tables;
        ecl_.pattern_extra_frames = runner.context.pattern_extra_frames;
        ecl_.pattern_extra_mode = runner.context.pattern_extra_mode;
        ecl_.pattern_extra_scale = runner.context.pattern_extra_scale;
        ecl_.pattern_extra_tail = runner.context.pattern_extra_tail;
        ecl_.pattern_extras = runner.context.pattern_extras;
        ecl_.pattern_aux_a = runner.context.pattern_aux_a;
        ecl_.pattern_aux_b = runner.context.pattern_aux_b;
        ecl_.pattern_aux_c = runner.context.pattern_aux_c;
        ecl_.pattern_aux_count = runner.context.pattern_aux_count;
        ecl_.spawn_box_x = runner.context.spawn_box_x;
        ecl_.spawn_box_y = runner.context.spawn_box_y;
        ecl_.spawn_box_flag = runner.context.spawn_box_flag;
        ecl_.spawn_boxes = runner.context.spawn_boxes;
        ecl_.spawn_param_a = runner.context.spawn_param_a;
        ecl_.spawn_param_b = runner.context.spawn_param_b;
        ecl_.spawn_param_sets = runner.context.spawn_param_sets;
        ecl_.script_marks = runner.context.script_marks;
        ecl_.named_call_length = runner.context.named_call_length;
        ecl_.named_call_target = std::move(runner.context.named_call_target);
        ecl_.named_call_exts = runner.context.named_call_exts;
        ecl_.named_effect_length = runner.context.named_effect_length;
        ecl_.named_effect_target = std::move(runner.context.named_effect_target);
        ecl_.named_effect_calls = runner.context.named_effect_calls;
        ecl_.boss_timer_value = runner.context.boss_timer_value;
        ecl_.boss_timer_values = runner.context.boss_timer_values;
        ecl_.position_x = runner.context.position_x;
        ecl_.position_y = runner.context.position_y;
        ecl_.position_pairs = runner.context.position_pairs;
        ecl_.boss_phase = runner.context.boss_phase;
        ecl_.boss_phases = runner.context.boss_phases;
        ecl_.branch_actions = runner.context.branch_actions;
        ecl_.branch_action_count = runner.context.branch_action_count;
        ecl_.attack_prepares = runner.context.attack_prepares;
        ecl_.timed_value_frames = runner.context.timed_value_frames;
        ecl_.timed_value = runner.context.timed_value;
        ecl_.timed_values = runner.context.timed_values;
        ecl_.effect_slot = runner.context.effect_slot;
        ecl_.effect_slot_calls = runner.context.effect_slot_calls;
        ecl_.bullet_pattern_flag = runner.context.bullet_pattern_flag;
        ecl_.bullet_pattern_mode = runner.context.bullet_pattern_mode;
        ecl_.bullet_pattern_a = runner.context.bullet_pattern_a;
        ecl_.bullet_pattern_b = runner.context.bullet_pattern_b;
        ecl_.bullet_patterns = runner.context.bullet_patterns;
        ecl_.bullet_launch_flag = runner.context.bullet_launch_flag;
        ecl_.bullet_launch_speed = runner.context.bullet_launch_speed;
        ecl_.bullet_launch_a = runner.context.bullet_launch_a;
        ecl_.bullet_launch_b = runner.context.bullet_launch_b;
        ecl_.bullet_launches = runner.context.bullet_launches;
        ecl_.boss_frame = runner.context.boss_frame;
        ecl_.boss_frame_length = runner.context.boss_frame_length;
        ecl_.boss_frame_lengths = runner.context.boss_frame_lengths;
        ecl_.boss_attack_a = runner.context.boss_attack_a;
        ecl_.boss_attack_b = runner.context.boss_attack_b;
        ecl_.boss_attack_timers = runner.context.boss_attack_timers;
        ecl_.boss_frame_enters = runner.context.boss_frame_enters;
        ecl_.boss_timer_a = runner.context.boss_timer_a;
        ecl_.boss_timer_b = runner.context.boss_timer_b;
        ecl_.boss_timer_sets = runner.context.boss_timer_sets;
        ecl_.boss_index = runner.context.boss_index;
        ecl_.boss_index_sets = runner.context.boss_index_sets;
        ecl_.stage_signal = runner.context.stage_signal;
        ecl_.stage_signal_sets = runner.context.stage_signal_sets;
        ecl_.stage_signal_waits = runner.context.stage_signal_waits;
        ecl_.last_timer_value = runner.context.last_timer_value;

        // Everything else the interpreter writes. These were missing until
        // ecl_merge_check.py compared the struct against this list: the
        // counters looked like they never moved, which is the same symptom as
        // wave_blocks and stage_signal before them.
        ecl_.timer_writes = runner.context.timer_writes;
        ecl_.difficulty_mask = runner.context.difficulty_mask;
        ecl_.call_depth = runner.context.call_depth;
        ecl_.pending_bullet = runner.context.pending_bullet;
        ecl_.query_count = runner.context.query_count;
        ecl_.last_query_reference = runner.context.last_query_reference;
        ecl_.last_query_field = runner.context.last_query_field;
        ecl_.set_count = runner.context.set_count;
        ecl_.last_set_a = runner.context.last_set_a;
        ecl_.last_set_b = runner.context.last_set_b;
        ecl_.last_position_x = runner.context.last_position_x;
        ecl_.last_position_y = runner.context.last_position_y;
        ecl_.position_sets = runner.context.position_sets;
        ecl_.comments = std::move(runner.context.comments);
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
    const u32 bombs_before = player_.stats().bombs;
    player_.update(held, &bullets_);

    // A bomb press clears the screen: every hostile bullet is cancelled (and
    // scores), and every enemy takes damage. The edge is detected by comparing
    // the cumulative counter, so holding the button does not repeat.
    if (player_.stats().bombs > bombs_before) {
        if (player_.take_bomb()) {
            u32 cancelled = 0;
            for (u32 i = 0; i < BULLET_CAPACITY; ++i) {
                Bullet& bullet = bullets_.mutable_bullet(i);
                if (!bullet.alive || !bullet.hostile) continue;
                bullets_.release(i);
                ++cancelled;
            }
            if (cancelled > 0) scoring_.add_bullet_cancel(cancelled);
            enemies_.damage_all(1);
            ++stats_.bombs_used;
        }
    }
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
        if (bullet.delay > 0) continue;   // still queued, not in the air yet
        const float dx = bullet.x - player_.x();
        const float dy = bullet.y - player_.y();
        if (dx * dx + dy * dy > hitbox * hitbox) continue;
        bullets_.release(i);
        if (player_.take_hit()) ++stats_.player_hits_taken;
    }
    // Score: kills are counted by comparing the pool's cumulative total
    // against what we last saw, so a wave that dies in one frame still scores
    // every enemy.
    const u32 killed_now = enemies_.stats().killed;
    if (killed_now > last_killed_) {
        for (u32 i = last_killed_; i < killed_now; ++i) {
            scoring_.add_enemy_kill();
            sounds_.play("se_enep00.wav", 0.7f, 3);
            // Drop at the dying enemy's last position. The pool does not keep
            // dead enemies, so use the most recent live one as an anchor; a
            // frame with several kills drops several items near that point.
            float drop_x = 0.0f;
            float drop_y = -120.0f;
            for (u32 e = 0; e < ENEMY_CAPACITY; ++e) {
                const Enemy& enemy = enemies_.enemies()[e];
                if (!enemy.alive) continue;
                drop_x = enemy.x;
                drop_y = enemy.y;
                break;
            }
            // Every fourth kill is a life fragment, the rest are stars.
            const u32 n = i + 1;
            const ItemKind kind = (n % LIFE_FRAGMENTS_PER_LIFE == 0)
                                    ? ItemKind::LifeFragment : ItemKind::PointStar;
            items_.spawn(drop_x, drop_y, kind);
        }
        last_killed_ = killed_now;
    }

    // Items fall and are collected. Each pickup applies its own effect: a
    // star scores, a life fragment counts toward the next life.
    item_pickups_.clear();
    items_.update(player_.x(), player_.y(), &item_pickups_);
    u32 frags_before = items_.stats().life_fragments;
    for (const ItemPool::Pickup& pickup : item_pickups_) {
        sounds_.play("se_item00.wav", 0.6f, 3);
        if (pickup.kind == ItemKind::LifeFragment) {
            // Every LIFE_FRAGMENTS_PER_LIFE-th fragment grants a life. The
            // counter lives in ItemPool, so compare the crossing point.
            const u32 after = items_.stats().life_fragments;
            const u32 before = frags_before;
            frags_before = after;
            const bool crossed = (before / LIFE_FRAGMENTS_PER_LIFE) !=
                                (after / LIFE_FRAGMENTS_PER_LIFE);
            if (crossed) player_.add_life();
        } else if (pickup.points > 0) {
            scoring_.add(pickup.points);
        }
    }
    stats_.items_alive = items_.stats().alive;
    stats_.items_collected = items_.stats().collected;
    stats_.life_fragments = items_.stats().life_fragments;
    stats_.lives_gained = player_.lives_gained();

    // Graze: a hostile bullet inside the graze ring but outside the hitbox
    // counts once, then is marked so it cannot be counted again.
    const float graze_r = GRAZE_RADIUS;
    const float graze_inner = player_.hitbox_radius();
    for (u32 i = 0; i < BULLET_CAPACITY; ++i) {
        Bullet& bullet = bullets_.mutable_bullet(i);
        if (!bullet.alive || !bullet.hostile) continue;
        if (bullet.delay > 0 || bullet.grazed) continue;
        const float dx = bullet.x - player_.x();
        const float dy = bullet.y - player_.y();
        const float d2 = dx * dx + dy * dy;
        if (d2 <= graze_r * graze_r && d2 > graze_inner * graze_inner) {
            bullet.grazed = true;
            scoring_.add_graze();
            sounds_.play("se_graze.wav", 0.4f, 6);
        }
    }

    // Stage progression.
    //
    // Both `main` and `BossNNtest` end in the same shape:
    //
    //   op42 -9896        push builtin slot 104
    //   op13 -> skip      if zero, jump to the raiser
    //   op1019            ...otherwise WAIT here
    //   op1017            ...and this arm raises the signal
    //   op23 1000 / push 1 / JumpIfNonzero    idle loop
    //
    // So slot 104 is the stage-clear flag, and the ONLY thing that can end a
    // stage is something outside the script raising it. In the original that
    // is the boss being defeated.
    //
    // Earlier attempts raised it from "the field has been empty for N frames".
    // Measured why that is wrong: on stage 0 the gap between waves is 4 frames,
    // so a 120-frame window never closed, and a 30-frame window reported clear
    // while two enemies were still alive. The field being empty is not the rule.
    //
    // The boss kill is. `boss_index` is set by op1024 (one per bossNN.ecl), so a
    // non-zero boss_index means a boss script is live; when the enemy count
    // reaches zero with a boss live, the boss is down.
    // Spawn the boss entity the first time op1024 reports a boss index.
    //
    // Without an entity the boss script runs but nothing can kill it, so the
    // stage can never finish. `boss_index` is set exactly once per boss file
    // (op1024), which makes it the natural spawn trigger.
    // Spawn once per stage; a defeated boss must not come back.
    if (ecl_.boss_index != 0 && !boss_spawned_ && !stats_.stage_clear) {
        boss_spawned_ = true;
        // Upper field, centred. The card's own position opcodes are not wired
        // yet, so this uses the conventional boss placement.
        const u32 slot = enemies_.spawn_boss(0.0f, -140.0f, BOSS_STARTING_HP, 0);
        stats_.boss_spawned = slot == 0xFFFFFFFFu ? 0u : 1u;
    }

    const bool boss_live = enemies_.boss_alive();
    const bool field_empty = enemies_.stats().alive == 0;

    // Waves must have stopped arriving. `schedules_fired` only grows, so a
    // frame without growth means the scene controller is not scheduling.
    if (stats_.schedules_fired != last_schedules_fired_) {
        last_schedules_fired_ = stats_.schedules_fired;
        stats_.frames_since_schedule = 0;
    } else {
        ++stats_.frames_since_schedule;
    }
    const bool waves_quiet =
        stats_.schedules_fired > 0 &&
        stats_.frames_since_schedule >= BOSS_DOWN_CONFIRM_FRAMES;

    // All three must hold at once: a boss is live, the field has been empty
    // for the confirmation window, and no new wave has been scheduled.
    //
    // Measured why each is needed: boss_index alone fired during the gap
    // between ordinary waves (clear=1 with 22 enemies still due); field-empty
    // alone fires between every wave; waves-quiet alone never becomes true
    // while the controller is looping.
    if (boss_live && field_empty && waves_quiet) {
        ++stats_.boss_kill_frames;
    } else {
        stats_.boss_kill_frames = 0;
    }
    if (stats_.boss_kill_frames >= BOSS_DOWN_CONFIRM_FRAMES &&
        !stats_.stage_clear) {
        // Raise builtin slot 104 -- the value op1017 reads.
        ecl_.builtins[STAGE_CLEAR_SLOT] = 1;
        stats_.stage_clear = 1;
    }
    // Death and respawn.
    //
    // take_hit() already decrements the life and starts the invulnerability
    // window, so a respawn is just a position reset. Detecting it by comparing
    // the life count keeps Player free of a "dying" state that the original
    // does not model either.
    const u32 lives_now = player_.lives();
    if (lives_now < last_lives_) {
        ++stats_.deaths;
        sounds_.play("se_pldead00.wav", 0.8f, 30);
        if (lives_now == 0) {
            stats_.game_over = 1;
        } else {
            player_.respawn();
        }
    }
    last_lives_ = lives_now;
    stats_.player_lives = player_.lives();
    stats_.enemy_shots = enemies_.stats().enemy_shots;
    stats_.score = scoring_.score();
    stats_.score_cap = scoring_.cap();
    stats_.grazes = scoring_.stats().grazes;
    stats_.score_capped = scoring_.stats().capped;
    stats_.bombs_left = player_.bombs_remaining();
    stats_.player_x = player_.x();
    stats_.player_y = player_.y();
    stats_.player_focused = player_.focused();
    stats_.player_shots = player_.stats().shots;
    stats_.player_bombs = player_.stats().bombs;
    stats_.bullets_alive = bullets_.stats().alive;
    stats_.bullets_fired = player_.stats().bullets_fired;
    stats_.sounds_played = sounds_.stats().played;
    stats_.sounds_dropped = sounds_.stats().dropped;
    stats_.sounds_throttled = sounds_.stats().throttled;

    // Advance the mixer by one frame's worth of samples.
    //
    // Without this the voices never progress and the pool fills up: a test
    // measured played=64 (the cap) with 88 drops because nothing consumed the
    // samples. There is no audio device in the WASM build yet, so the buffer is
    // mixed and discarded -- but the voice lifecycle has to run for the counts
    // to mean anything.
    {
        constexpr u32 MIX_RATE = 22050;
        constexpr u32 FRAMES_PER_TICK = MIX_RATE / 60;   // 367
        mix_buffer_.resize(std::size_t(FRAMES_PER_TICK) * 2);
        sounds_.mix(mix_buffer_.data(), FRAMES_PER_TICK);   // clears, then adds
        sounds_.advance_frame();
        bgm_.mix(mix_buffer_.data(), FRAMES_PER_TICK);      // adds only
        stats_.bgm_track = bgm_.current_track();
        stats_.bgm_playing = bgm_.is_playing() ? 1u : 0u;
    }

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
