#pragma once
// TH185 application shell: the smallest thing that is still a real program.
//
// ## Why this exists
//
// Every C++ module so far is a decoder or a state machine that a harness drives.
// There is no application: nothing owns the archive, nothing advances a frame,
// nothing exposes an interface to the web shell. The HTML at
// sdl-runtime/th185.html already expects `/app/shell.mjs`, which expects the
// exports declared at the bottom of this file.
//
// ## Scope, stated plainly
//
// This is an APPLICATION, not a game yet. It does:
//
//   * own the archive and load a stage
//   * own a GameSession and an EnemyManager
//   * advance a fixed-step frame clock
//   * run ECL subroutines
//   * own an ANM manager and render its tree into an RGBA8 framebuffer
//   * expose init/tick/pause/restart/frame to the shell
//
// It does NOT play audio or accept input, and the renderer is software (no
// GPU). `tick()` therefore produces real pixels but not the original game's
// pixels: the transform, blend and sampling are implemented from th15's
// behaviour, but nobody has compared the output against a capture.
//
// The point of building it now is that it forces the interfaces to exist and
// gives the web shell something real to call. Rendering can then be added
// behind the same surface.

#include "../game/AnmManager.hpp"
#include "../game/AnmResource.hpp"
#include "../game/AnmTexture.hpp"
#include "../game/Archive.hpp"
#include "../game/EclContext.hpp"
#include "../game/EclInterpreter.hpp"
#include "../game/EclResource.hpp"
#include "../game/EnemyManager.hpp"
#include "../game/GameSession.hpp"
#include "../game/Rng.hpp"
#include "../game/SoftwareRaster.hpp"
#include "../game/StageScript.hpp"
#include "Types.hpp"

#include <string>
#include <vector>

namespace th185::app {

enum class Phase : u32 {
    Idle = 0,
    Running = 1,
    Paused = 2,
    Failed = 3,
};

struct FrameStats {
    u32 frame = 0;
    u32 ticks = 0;
    u32 ecl_instructions = 0;
    u32 ecl_faults = 0;
    u32 ecl_unsupported = 0;
    u32 max_stack_depth = 0;
    // Rendering.
    u32 quads = 0;
    u32 pixels_written = 0;
    u32 live_animations = 0;
};

class Application {
public:
    // Loads the archive from `bytes` and prepares a stage. Returns false and
    // fills error() on failure.
    bool initialize(std::vector<u8> archive_bytes);

    // Advances one logic tick. `held` is a bitmask of held buttons.
    // Returns false on a fatal error.
    bool tick(u32 held);

    // Enters Running. Idle -> Running is the shell's "start" action.
    bool start();

    bool pause();
    bool resume();
    bool restart();

    // Loads `stage_index` (0..8) and starts its ECL subroutines.
    bool load_stage(u32 stage_index);

    const std::string& error() const { return error_; }
    Phase phase() const { return phase_; }
    const FrameStats& stats() const { return stats_; }
    const GameSession& session() const { return session_; }

    // Number of subroutines currently registered and runnable.
    u32 runnable_subroutines() const { return runnable_; }

    // The framebuffer `tick()` last produced, as tightly packed RGBA8.
    // Empty until the first tick after start().
    const std::vector<u8>& framebuffer() const { return raster_.pixels(); }
    u32 framebuffer_width() const { return raster_.width(); }
    u32 framebuffer_height() const { return raster_.height(); }

    // Draws every live animation into the framebuffer without advancing time.
    // Exposed so a shell can present a paused frame.
    void render();

private:
    bool register_stage_scripts(u32 stage_index);
    bool load_animation_bank(const char* name);

    Archive archive_;
    std::vector<u8> archive_bytes_;
    GameSession session_;
    EclContext ecl_;
    std::vector<std::string> active_scripts_;

    // Animation state. `animation_textures_` must outlive the manager because
    // AnmVm holds pointers into it.
    AnmResource animation_;
    AnmManager animations_;
    std::vector<DecodedImage> animation_textures_;
    SoftwareRaster raster_;
    Rng rng_;

    Phase phase_ = Phase::Idle;
    FrameStats stats_{};
    std::string error_;
    u32 runnable_ = 0;
    u32 stage_index_ = 0;
};

// Global instance used by the exported C functions.
Application& instance();

}  // namespace th185::app
