#include "Application.hpp"

#include <algorithm>

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
namespace {

// Which boss script belongs to the loaded stage.
//
// boss01.ecl pairs with world01, boss02 with world02, and so on; world01t
// (the tutorial) pairs with boss01t. Every other boss file must stay idle:
// each BossNNtest carries nine op1013 wave schedules of its own, so running
// them all keeps the field populated forever.
namespace {

// Identity of one firing: the schedule entry plus the index of the time slot.
//
// The entry is identified by its slot and its whole times[] array, so two
// scripts declaring the same (slot, time) stay distinct -- which they must,
// because they are two separate spawn events.
u64 schedule_key(const EclContext::SpawnSchedule& schedule, u32 index) {
    u64 h = 1469598103934665603ull;   // FNV-1a offset basis
    const auto mix = [&h](u64 value) {
        h ^= value;
        h *= 1099511628211ull;
    };
    mix(schedule.slot);
    for (u32 k = 0; k < 8; ++k) mix(schedule.times[k]);
    mix(index);
    return h;
}

}  // namespace
bool boss_script_matches(const std::string& name, u32 stage_index) {
    // BossNNtest  -> "BossNN" + "test"
    // BossNNttest -> "BossNNt" + "test"
    const std::string stem = name.substr(0, name.size() - 4);

    char wanted[16];
    if (stage_index == 0) {
        // world01t is the tutorial scene; its boss is boss01t.
        std::snprintf(wanted, sizeof(wanted), "Boss01t");
    } else {
        std::snprintf(wanted, sizeof(wanted), "Boss%02u", unsigned(stage_index));
    }
    return stem == wanted;
}

}  // namespace
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
        // boss01t.ecl is the tutorial boss (pairs with world01t). It is NOT
        // covered by the loop above, which is why Boss01ttest never appeared
        // in the subroutine list and the tutorial stage had no boss.
        wanted.push_back("boss01t.ecl");
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
        // main always runs. A boss controller runs ONLY when its file matches
        // the boss this stage selected.
        //
        // Running all 27 was wrong: every BossNNtest carries its own nine
        // op1013 wave schedules, so 28 controllers kept scheduling waves
        // forever and the field never emptied (measured: schedules_fired grew
        // 29 -> 37 -> 40 -> 46 -> 57 with no sign of stopping).
        //
        // The selection comes from CardManager's forced-boss rule, which is
        // already implemented: stage 5 with card 0 forces Boss26, stage 7 with
        // card 66 forces Boss27, otherwise the weighted draw picks one.
        if (sub.name == "main") {
            active_scripts_.push_back(sub.name);
            ++runnable_;
        } else if (is_boss_entry(sub.name) &&
                   boss_script_matches(sub.name, stage_index)) {
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

            // Key on the whole schedule entry plus the index within it.
            //
            // (slot, time) alone is NOT unique: stage 0 has 112 time points
            // but only 103 distinct (slot, time) pairs. The repeats are real --
            // the same slot fires twice at the same frame from two different
            // scripts (world01t and boss01t both schedule slot 16 at 1800, for
            // example). Collapsing them made schedules_fired stop at 103/112,
            // so `waves_quiet` never became true and stage 0 could never end.
            //
            // The entry's identity is (slot, its whole times[] array). Adding
            // the index inside that array keeps duplicate times within one
            // entry distinct too.
            const u64 key = schedule_key(schedule, i);
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


            // `group_size` is the spawn area (world05: 128, world08: 2048).
            // It is kept as the collision radius because that is what makes
            // the enemies hittable -- shrinking it to 16 dropped stage 0's
            // kills from 25 to 1. The DRAWING size is clamped separately in
            // render(), which is where the 8192x8192 quads came from.
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

            // Extract + parse once per template, then reuse.
            auto cached = wave_cache_.find(template_index);
            if (cached == wave_cache_.end()) {
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
                EclResource parsed;
                if (!parsed.parse(std::move(blob), &wave_error)) continue;
                cached = wave_cache_.emplace(template_index, std::move(parsed)).first;
            }
            const EclResource& wave_resource = cached->second;

            // Run every subprogram in the template ONCE, then remember it.
            //
            // These subroutines emit bullets; re-running them on every firing
            // of the same template multiplied the bullet count and the cost.
            // The set below makes each template's emission happen exactly once,
            // which is what a wave template is: a definition, not a per-frame
            // event.
            if (!wave_run_once_.insert(template_index).second) continue;
            // Run only the subroutines that actually EMIT.
            //
            // A wave bank holds ~8-10 subroutines and only 1-2 of them contain
            // op611; the rest are helpers (WaveNNLoop, WaveNNSub01, ...) that
            // the emitter reaches through Call. Running every subroutine
            // standalone meant 142 emitters fired at once on stage 5 -- 1136
            // bullets in a single frame, which then cost 70 ms per frame in the
            // per-bullet passes and 390 s for the stage.
            for (const EclSubroutine& sub : wave_resource.subs) {
                bool emits = false;
                for (const EclInstruction& ins : sub.instructions) {
                    if (ins.id == u16(EclOp::Op611BulletEmit)) {
                        emits = true;
                        break;
                    }
                }
                if (!emits) continue;
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

                // Same swap trick as tick(): a deep copy of EclContext here
                // ran once per SUBROUTINE per wave template. Stage 5 and 7
                // have the largest wave banks, which is why they were 15-25x
                // slower than the others (measured: 353 s for 1807 frames).
                EclInterpreter runner;
                std::swap(runner.context, ecl_);
                runner.run(code, 4000, code_base);
                std::swap(runner.context, ecl_);
            }
        }
    }
    ecl_.wave_blocks.clear();

    // Turn recorded bullet patterns into real bullets. Each request is one
    // emission: N bullets spread over [angle_lo, angle_hi] at the recorded
    // speed, aimed along the enemy's facing. Delays are per-bullet frame
    // counts, so a request fans out over time instead of all at once.
    // Cap how many requests one frame may realise.
    //
    // A wave loop body emits on every iteration when op23 cannot wait, so a
    // single tick produced 1638 requests on stage 0 (4094 bullets, pool full).
    // The per-request cap does not help because each request is small; the
    // count of requests is what explodes. 32 is well above what a normal
    // pattern needs in one frame and keeps the field playable.
    stats_.last_frame_requests = u32(ecl_.bullet_requests.size());
    if (ecl_.bullet_requests.size() > MAX_REQUESTS_PER_FRAME) {
        ecl_.bullet_requests.resize(MAX_REQUESTS_PER_FRAME);
    }
    for (const EclContext::BulletRequest& request : ecl_.bullet_requests) {
        // Bound the emission here, not only in the inner loop.
        //
        // `count` comes from op606 (max 32 in the data) but an unbounded
        // value reached 4094 on stage 0 frame 4 and filled the whole pool in
        // one tick. The inner guard alone was not enough because the loop
        // body re-reads `count` for the angle interpolation.
        const u32 raw_count =
            request.count > 0 ? request.count : u32(request.angles.size());
        const u32 count =
            raw_count > MAX_BULLETS_PER_REQUEST ? MAX_BULLETS_PER_REQUEST : raw_count;
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
            // Cap the emission per request.
            //
            // `count` comes from op605 and is 0/8/16 in the real data, but a
            // wave subroutine that runs more than once multiplies it: stage 5
            // reached 1137 hostile bullets from a single enemy, which cost
            // 380 ms per frame in the per-bullet passes. 64 is above every
            // observed single fan (16) and still bounds the worst case.
            if (i >= MAX_BULLETS_PER_REQUEST) break;
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
    // Apply the equipped card loadout. Lives and bombs are additive; the
    // shot bonus is published for the firing code to read.
    //
    // Only the CATEGORY -> effect mapping is this build's interpretation
    // (CardManager.cpp documents that); the categories themselves come from
    // the card table.
    for (u32 i = 0; i < cards_.extra_lives(); ++i) player_.add_life();
    for (u32 i = 0; i < cards_.extra_bombs(); ++i) player_.add_bomb();
    stats_.card_extra_lives = cards_.extra_lives();
    stats_.card_extra_bombs = cards_.extra_bombs();
    stats_.card_shot_bonus = cards_.shot_power_bonus();
    scoring_.add(cards_.starting_currency_bonus());
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
    // The recorded schedules belong to the stage that declared them. Leaving
    // them in place made the next stage fire the previous stage's timers as
    // well: after preloading stages 0-4, stage 5 started with 17 enemies
    // already on the field and then reached 42 enemies / 1139 bullets, which
    // cost 280 ms per frame (stage 5 alone: 35 s, after preload: 400 s).
    ecl_.schedules.clear();
    ecl_.wave_blocks.clear();
    ecl_.pending_templates.clear();
    ecl_.wave_templates.clear();
    fired_schedule_keys_.clear();
    stats_.schedules_fired = 0;
    stats_.schedules_expected = 0;
    // Template indices are per-stage, so the "already emitted" set and the
    // parsed cache must both be dropped. Leaving wave_run_once_ populated made
    // the next stage skip templates that happened to share an index with the
    // previous one, and every skipped firing then fell through to a fresh
    // extract+parse -- stage 5 measured 35 s alone but 355 s after stage 0-4.
    wave_run_once_.clear();
    wave_cache_.clear();

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
        // Half-extent of the drawn quad, clamped.
        //
        // radius doubles as the spawn-area size (world08: 2048), so drawing at
        // that scale produced quads up to 8192 px per side -- stage 5's render
        // time grew 0.8 ms -> 150 ms as enemies appeared, because each enemy
        // covered the whole framebuffer many times over. 32 px is the size of a
        // real enemy sprite; the collision radius stays untouched.
        constexpr float MAX_DRAW_HALF = 32.0f;
        const float half = enemy.radius < MAX_DRAW_HALF
                               ? enemy.radius
                               : MAX_DRAW_HALF;
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
    script_instrs_.clear();
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
        // Hand the interpreter the live context by SWAP, not by copy.
        //
        // This used to be `runner.context = ecl_`, a deep copy of a 133-field
        // struct that owns several vectors and maps -- once per script per
        // frame. Measured on stage 8: 404 ms per frame for a stage needing only
        // 712 frames, i.e. 288 s for that stage and 679 s for all nine, which
        // blew the CI budget.
        //
        // std::swap moves the internal pointers, so it is O(1) regardless of
        // how much the containers hold. It also removes the ~120-line field
        // merge below, which repeatedly dropped fields (wave_blocks,
        // stage_signal, boss_index_sets, coroutines).
        EclInterpreter runner;
        std::swap(runner.context, ecl_);
        runner.run_coroutine(name, found->second.code,
                             found->second.code_base);
        std::swap(runner.context, ecl_);
        // Per-script instruction count, so a script that burns its whole
        // budget every frame is visible instead of hiding in the total.
        script_instrs_.push_back(runner.stats().instructions);
        // Nothing to merge back: the interpreter ran against the live context.

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
            // Walk the live list, not all 4096 slots. Three such loops ran per
            // frame; together they cost 12288 iterations even when the field
            // held 20 bullets.
            for (const u32 i : bullets_.live_indices()) {
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
    // Walk the live list with a cheap vertical reject.
    //
    // A Y-bucket index was tried and reverted: `bucket_at` copies the matching
    // indices into a scratch vector, and on stages 7/8 the bullets cluster in a
    // few rows, so the copy cost more than the scan it replaced (stage 8 went
    // 246 s -> 426 s). The reject below skips the two multiplies for almost
    // every bullet without touching memory.
    for (const u32 i : bullets_.live_indices()) {
        Bullet& bullet = bullets_.mutable_bullet(i);
        if (!bullet.alive || !bullet.hostile) continue;
        if (bullet.delay > 0) continue;   // still queued, not in the air yet
        const float dy = bullet.y - player_.y();
        if (dy > 32.0f || dy < -32.0f) continue;
        const float dx = bullet.x - player_.x();
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
    for (const u32 i : bullets_.live_indices()) {
        Bullet& bullet = bullets_.mutable_bullet(i);
        if (!bullet.alive || !bullet.hostile) continue;
        if (bullet.delay > 0 || bullet.grazed) continue;
        const float dy = bullet.y - player_.y();
        if (dy > graze_r || dy < -graze_r) continue;
        const float dx = bullet.x - player_.x();
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

    // The boss must have EXISTED and be gone. Requiring it to still be alive
    // was backwards: the stage ends when the boss dies, so `boss_live` is
    // false exactly when the condition should hold.
    //
    // `boss_spawned_` is the "it existed" half; `!boss_alive()` is the "it is
    // gone" half. Measured on stage 1: schedules 72/72, enemies 0,
    // bossAlive 0 -- every other condition was already satisfied and only
    // this inverted test blocked the clear.
    const bool boss_defeated = boss_spawned_ && !enemies_.boss_alive();
    const bool field_empty = enemies_.stats().alive == 0;

    stats_.script_instrs = script_instrs_;

    // Waves must have stopped arriving. `schedules_fired` only grows, so a
    // frame without growth means the scene controller is not scheduling.
    if (stats_.schedules_fired != last_schedules_fired_) {
        last_schedules_fired_ = stats_.schedules_fired;
        stats_.frames_since_schedule = 0;
    } else {
        ++stats_.frames_since_schedule;
    }
    // Every schedule has a fixed list of due frames, so the total number of
    // firings is known up front: 8 per entry (a zero time means "unused").
    //
    // Measured on stage 0: 14 entries (5 from world01t, 9 from boss01t) with
    // 112 non-zero times between them. Waiting for schedules_fired to STOP
    // growing was wrong -- it grows in bursts and the 30-frame window closed
    // during a gap. Counting the total is exact.
    u32 expected_fires = 0;
    for (const EclContext::SpawnSchedule& schedule : ecl_.schedules) {
        for (u32 k = 0; k < 8; ++k) {
            if (schedule.times[k] != 0) ++expected_fires;
        }
    }
    stats_.schedules_expected = expected_fires;
    const bool waves_quiet =
        expected_fires > 0 && stats_.schedules_fired >= expected_fires;

    // All three must hold at once: a boss is live, the field has been empty
    // for the confirmation window, and no new wave has been scheduled.
    //
    // Measured why each is needed: boss_index alone fired during the gap
    // between ordinary waves (clear=1 with 22 enemies still due); field-empty
    // alone fires between every wave; waves-quiet alone never becomes true
    // while the controller is looping.
    if (boss_defeated && field_empty && waves_quiet) {
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
