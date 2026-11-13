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
#include "../game/ItemPool.hpp"
#include "../game/Player.hpp"
#include "../game/BulletPool.hpp"
#include "../game/EnemyPool.hpp"
#include "../game/AnmResource.hpp"
#include "../game/AnmTexture.hpp"
#include "../game/Archive.hpp"
#include "../game/BgmPlayer.hpp"
#include "../game/EclContext.hpp"
#include "../game/EclInterpreter.hpp"
#include "../game/EclResource.hpp"
#include "../game/EnemyManager.hpp"
#include "../game/GameSession.hpp"
#include "../game/Rng.hpp"
#include "../game/SoftwareRaster.hpp"
#include "../game/ScoreSystem.hpp"
#include "../game/SoundBank.hpp"
#include "../game/StageScript.hpp"
#include "Types.hpp"

#include <set>
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
    u32 ecl_first_unsupported = 0;
    u32 max_stack_depth = 0;
    // Rendering.
    u32 quads = 0;
    u32 pixels_written = 0;
    u32 live_animations = 0;
    // Last button bitmask received from the shell.
    u32 held_buttons = 0;
    // Player.
    float player_x = 0.0f;
    float player_y = 0.0f;
    bool player_focused = false;
    u32 player_shots = 0;
    u32 player_bombs = 0;
    u32 bullets_alive = 0;
    u32 bullets_fired = 0;
    u32 enemies_alive = 0;
    u32 enemies_killed = 0;
    u32 collision_hits = 0;
    u32 player_hits_taken = 0;
    u32 player_lives = 0;
    u32 enemy_shots = 0;
    u32 score = 0;
    u32 score_cap = 0;
    u32 grazes = 0;
    u32 score_capped = 0;
    u32 difficulty = 0;
    u32 bombs_used = 0;
    u32 bombs_left = 0;
    u32 items_alive = 0;
    u32 items_collected = 0;
    u32 life_fragments = 0;
    u32 lives_gained = 0;
    u32 game_over = 0;
    u32 deaths = 0;
    u32 sounds_played = 0;
    u32 sounds_loaded = 0;
    u32 sounds_dropped = 0;
    u32 sounds_throttled = 0;
    u32 bgm_tracks = 0;
    u32 bgm_track = 0xFFFFFFFFu;
    u32 bgm_playing = 0;
    // ECL wave schedules consumed so far.
    u32 schedules_total = 0;
    u32 schedules_fired = 0;
};

class Application {
public:
    // Loads the archive from `bytes` and prepares a stage. Returns false and
    // fills error() on failure.
    bool initialize(std::vector<u8> archive_bytes);

    // Attaches the BGM stream (thbgm.dat). The bytes are borrowed, not copied:
    // the file is 255 MB and the player reads it in place.
    bool attach_bgm(std::vector<u8> bgm_bytes);
    bool play_bgm(u32 index);

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

    // Selects the run's difficulty (0..7). Affects the score cap and the
    // builtin slots the ECL scripts branch on.
    void set_difficulty(u32 difficulty);
    u32 difficulty() const { return difficulty_; }

    const std::string& error() const { return error_; }
    Phase phase() const { return phase_; }
    const FrameStats& stats() const { return stats_; }
    const GameSession& session() const { return session_; }

    // Names of the animation banks currently loaded, joined by '+'.
    const std::string& animation_bank() const { return animation_bank_; }

    const Player& player() const { return player_; }
    const Player& player_state() const { return player_; }
    const EclContext& ecl() const { return ecl_; }
    const StageScript& stage() const { return stage_; }
    const ScoreSystem& scoring() const { return scoring_; }
    const ItemPool& items() const { return items_; }
    const SoundBank& sounds() const { return sounds_; }
    const BgmPlayer& bgm() const { return bgm_; }
    const Archive& archive() const { return archive_; }
    const std::vector<u8>& archive_bytes() const { return archive_bytes_; }
    i32 last_wave_template() const { return ecl_.last_wave_template; }
    const EnemyPool& enemies() const { return enemies_; }
    const BulletPool& bullets() const { return bullets_; }

    // Number of subroutines currently registered and runnable.
    u32 runnable_subroutines() const { return runnable_; }
    u32 active_script_count() const { return u32(active_scripts_.size()); }

    // The framebuffer `tick()` last produced, as tightly packed RGBA8.
    // Empty until the first tick after start().
    const std::vector<u8>& framebuffer() const { return raster_.pixels(); }
    u32 framebuffer_width() const { return raster_.width(); }
    u32 framebuffer_height() const { return raster_.height(); }

    // Draws every live animation into the framebuffer without advancing time.
    // Exposed so a shell can present a paused frame.
    void render();

    // Spawns a wave of drifting targets. Exposed so a test can set up a
    // deterministic scenario instead of waiting for ECL to do it.
    u32 spawn_test_wave(u32 count);

    // Clears bullets and enemies and puts the player back at centre. Tests need
    // a known starting state; without this each phase inherits the previous
    // phase's position and the collision test measured nothing.
    void reset_scenario();

private:
    bool register_stage_scripts(u32 stage_index);
    void load_sound_effects();
    void load_bgm_index();
    bool load_animation_bank(const char* name, u32 script_limit);
    // Loads the background bank named by the scene, then the enemy bank.
    void load_scene_banks();
    // Turns recorded op1013 schedules into actual enemies.
    void consume_schedules();

    Archive archive_;
    std::vector<u8> archive_bytes_;
    GameSession session_;
    EclContext ecl_;
    std::vector<std::string> active_scripts_;

    // Animation state. `animation_textures_` must outlive the manager because
    // AnmVm holds pointers into it.
    // One .anm per bank. A scene needs at least two: the background
    // (worldNN.anm, a single sprite) and the enemies (enemy.anm).
    struct AnimationBank {
        AnmResource resource;
        std::vector<DecodedImage> textures;
        std::string name;
    };
    std::vector<AnimationBank> banks_;
    AnmManager animations_;
    Player player_;
    BulletPool bullets_;
    EnemyPool enemies_;
    ItemPool items_;
    SoundBank sounds_;
    BgmPlayer bgm_;
    std::vector<u8> bgm_bytes_;
    // Scratch buffer for one frame of mixed stereo audio.
    std::vector<float> mix_buffer_;
    // Scratch buffer reused each frame; avoids a per-frame allocation.
    std::vector<ItemPool::Pickup> item_pickups_;
    ScoreSystem scoring_;
    // Enemy killed-count seen on the previous frame, so a multi-kill frame
    // scores every enemy instead of only the last one.
    u32 last_killed_ = 0;
    // Life count seen last frame, so a death can be detected without a
    // "dying" flag inside Player.
    u32 last_lives_ = PLAYER_STARTING_LIVES;
    // Fired schedule entries, keyed by (slot << 32 | time). A set rather than a
    // per-index bitmap because the interpreter re-records the same schedules
    // every tick (main is re-run from the start each frame).
    std::set<u64> fired_schedule_keys_;
    // Fired schedule entries, keyed by (slot << 32 | time). A set rather than a
    std::string animation_bank_;
    SoftwareRaster raster_;
    Rng rng_;

    Phase phase_ = Phase::Idle;
    FrameStats stats_{};
    std::string error_;
    u32 runnable_ = 0;
    u32 stage_index_ = 0;
    u32 difficulty_ = 0;
    // Parsed .std for the current stage. The group table holds the real
    // formation coordinates; op1013 slots index into it.
    StageScript stage_;
};

// Global instance used by the exported C functions.
Application& instance();

}  // namespace th185::app
