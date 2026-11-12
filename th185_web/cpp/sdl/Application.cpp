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
    // A missing animation bank is not fatal: the logic side still works, and
    // the shell can show an empty frame.
    load_animation_bank("enemy.anm");
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
        if (all_implemented) {
            active_scripts_.push_back(sub.name);
            ++runnable_;
        }
        ecl_.add_subroutine(sub.name, std::move(code));
        ecl_.subroutines[sub.name].code_base = code_base;
    }
    return true;
}

bool Application::load_animation_bank(const char* name) {
    // Load one .anm as the animation source and decode all its textures.
    // A stage normally uses several banks; one is enough for the shell to
    // show something real while the rest is wired up.
    std::size_t entry = archive_.entries.size();
    for (std::size_t i = 0; i < archive_.entries.size(); ++i) {
        if (archive_.entries[i].name == name) { entry = i; break; }
    }
    if (entry == archive_.entries.size()) return false;

    std::vector<u8> blob = archive_.extract(archive_bytes_, entry, &error_);
    if (blob.empty()) return false;
    if (!animation_.parse(std::move(blob), &error_)) return false;

    animation_textures_.clear();
    animation_textures_.reserve(animation_.textures.size());
    for (const AnmTexture& texture : animation_.textures) {
        animation_textures_.push_back(decode_texture(
            texture.format, texture.width, texture.height,
            animation_.data.data() + texture.offset + 16, texture.size));
    }

    // Start every script so the frame has content. A real stage would pick
    // specific ones; for now the point is that the pipeline runs end to end.
    animations_ = AnmManager{};
    const u32 limit = u32(animation_.script_list.size() < 32 ?
                          animation_.script_list.size() : 32);
    const u32 columns = 8;
    for (u32 s = 0; s < limit; ++s) {
        const float column = float(s % columns);
        const float row = float(s / columns);
        animations_.create(&animation_, i32(s), 0, 0xFFFFFFFFu,
                           (column - float(columns - 1) * 0.5f) * 64.0f,
                           (row - 1.5f) * 64.0f);
    }
    return true;
}

void Application::render() {
    if (raster_.width() == 0) {
        raster_.resize(640, 480);
    }
    // Opaque dark clear: a transparent framebuffer composites to white in
    // every viewer and hides the geometry.
    raster_.clear(Rgba8{24, 24, 48, 255});
    stats_.quads = animations_.render(
        raster_,
        animation_textures_.empty() ? nullptr : animation_textures_.data(),
        u32(animation_textures_.size()),
        float(raster_.width()) * 0.5f,
        float(raster_.height()) * 0.5f);
    stats_.pixels_written = raster_.pixels_written;
    stats_.live_animations = animations_.live_count();
}

bool Application::load_stage(u32 stage_index) {
    if (!session_.load_stage(stage_index, &error_)) return false;
    stage_index_ = stage_index;
    return register_stage_scripts(stage_index);
}

bool Application::tick(u32 held) {
    (void)held;  // No input subsystem yet.
    if (phase_ == Phase::Failed) return false;
    if (phase_ != Phase::Running) return true;

    ++stats_.ticks;

    // Run every runnable subroutine once per tick with a bounded step budget.
    // This is where a real frame loop would also render, mix audio and poll
    // input; none of those exist yet.
    for (const std::string& name : active_scripts_) {
        const auto found = ecl_.subroutines.find(name);
        if (found == ecl_.subroutines.end()) continue;

        EclInterpreter runner;
        runner.context = ecl_;
        runner.run(found->second.code, 4096, found->second.code_base);
        ecl_ = runner.context;

        stats_.ecl_instructions += runner.stats().instructions;
        stats_.ecl_faults += runner.stats().fault_count;
        stats_.ecl_unsupported += runner.stats().unsupported_count;
        if (runner.stats().max_stack_depth > stats_.max_stack_depth) {
            stats_.max_stack_depth = runner.stats().max_stack_depth;
        }
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
