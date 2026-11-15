#pragma once

// How many consecutive enemy-free frames count as a cleared field. One empty
// frame happens between waves, so a clear needs a sustained gap.
namespace th185 {

// Defined in game/Hud.hpp. Declared here because Application.hpp has no
// includes of its own; Application.cpp pulls the definition in.
class Hud;

constexpr unsigned CLEAR_CONFIRM_FRAMES = 30;

// Builtin slot that holds the stage-clear flag. op42 -9896 reads it, and
// op1017/op1019 in main and BossNNtest branch on it.
constexpr unsigned STAGE_CLEAR_SLOT = 104;

// Frames the field must stay empty (with a boss live) before a kill counts.
constexpr unsigned BOSS_DOWN_CONFIRM_FRAMES = 30;

// Boss health fallback.
//
// The real value comes from op511 (measured 7000, spell accumulator 49000).
// This constant is only used when a script never sets health.
constexpr int BOSS_STARTING_HP = 7000;

// Upper bound on one op611 emission. The recorded op605 counts are 0/8/16,
// so this only clips pathological repeats.
constexpr unsigned MAX_BULLETS_PER_REQUEST = 64;

// Requests one frame may realise.
//
// A wave loop body emits per iteration when op23 cannot wait; stage 0 produced
// 1638 requests in one tick. The per-request cap does not help there because
// each request is small -- it is the COUNT that explodes.
constexpr unsigned MAX_REQUESTS_PER_FRAME = 32;

// Button bits, matching th185::PlayerButton and the shell's BUTTON table.
// These were briefly written in a different order than PlayerButton, which
// made the pause menu's "confirm" fire on the wrong key.
constexpr unsigned BUTTON_SHOT = 1u << 0;
constexpr unsigned BUTTON_BOMB = 1u << 1;
constexpr unsigned BUTTON_FOCUS = 1u << 2;
constexpr unsigned BUTTON_SPECIAL = 1u << 3;
constexpr unsigned BUTTON_UP = 1u << 4;
constexpr unsigned BUTTON_DOWN = 1u << 5;
constexpr unsigned BUTTON_LEFT = 1u << 6;
constexpr unsigned BUTTON_RIGHT = 1u << 7;

// What clearing a scene grants. The original ties slot count and the cost cap
// to progress ("通过踏破场景和击败新 BOSS 提升卡槽数量与 Cost 上限"); the exact
// increments are this build's choice, sized so a 200-cost card becomes
// equippable after four clears.
constexpr unsigned PROGRESS_SLOTS_PER_CLEAR = 1;
constexpr unsigned PROGRESS_COST_PER_CLEAR = 25;
}  // namespace th185
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

#include <map>
#include <set>
#include <string>
#include <vector>

namespace th185::app {

// Items on the pause screen. The original has a full menu with options and
// key config; this build offers the three actions a player actually needs
// mid-stage.
enum class PauseItem : u32 {
    Resume = 0,
    Restart = 1,
    Quit = 2,
    Count = 3,
};

enum class Phase : u32 {
    Idle = 0,
    Running = 1,
    Paused = 2,
    Failed = 3,
    // The stage is complete and the market is open. The original returns to
    // the scene select after the purchases; this build stops here so the
    // shell can show the result and the next load_stage resets it.
    Cleared = 4,
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
    u32 background_quads = 0;
        u32 hud_quads = 0;
    u32 pixels_written = 0;
    u32 live_animations = 0;
    // Last button bitmask received from the shell.
    u32 held_buttons = 0;
    u32 held_buttons_prev = 0;
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
    // Consecutive frames with no live enemy. A single empty frame happens
    // between waves, so a clear needs several.
    u32 clear_frames = 0;
    // Frame at which the stage was cleared, and the end-of-stage bookkeeping.
    u32 clear_frame = 0;
    u32 banked = 0;
    u32 market_open = 0;
    u32 progress_slots = 0;
    u32 progress_cost_cap = 0;
    u32 boss_kill_frames = 0;
    u32 boss_spawned = 0;
    u32 frames_since_schedule = 0;
    u32 schedules_expected = 0;
    u32 last_frame_requests = 0;
    u32 card_extra_lives = 0;
    u32 card_extra_bombs = 0;
    float card_shot_bonus = 0.0f;
    // Instructions per active script, refreshed every tick.
    std::vector<u32> script_instrs;
    u32 stage_clear = 0;
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
    bool tick(u32 held, bool do_render = true);

    // Enters Running. Idle -> Running is the shell's "start" action.
    bool start();

    bool pause();
    // Pause-menu navigation. Movement is edge-triggered so holding a key does
    // not scroll the list.
    void pause_move(i32 delta);
    // Runs the selected item. Resume returns to play; Restart reloads the
    // stage; Quit returns to the scene-select phase.
    void pause_confirm();
    PauseItem pause_item() const { return pause_item_; }
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
    bool hud_bound() const;
    const GameSession& session() const { return session_; }

    // Names of the animation banks currently loaded, joined by '+'.
    const std::string& animation_bank() const { return animation_bank_; }

    const Player& player() const { return player_; }
    const Player& player_state() const { return player_; }
    const EclContext& ecl() const { return ecl_; }
    // Instructions the interpreter skipped for the current difficulty tier.
    u32 ecl_difficulty_skipped() const { return ecl_difficulty_skipped_; }
    u32 ecl_first_masked_opcode() const { return ecl_first_masked_opcode_; }
    u32 ecl_first_masked_rank() const { return ecl_first_masked_rank_; }
    u32 ecl_rank_ff() const { return ecl_rank_ff_; }
    u32 ecl_rank_other() const { return ecl_rank_other_; }
    // How many distinct bullet sprite indices are alive right now.
    u32 bullet_sprite_kinds() const;
    // Entities in the current stage's .std table (background layers).
    u32 stage_entity_count() const { return u32(session_.stage.entities.size()); }
    // Card inventory: owned cards, equipped loadout and the market wallet.
    CardInventory& cards() { return cards_; }
    const CardInventory& cards() const { return cards_; }
    // Names of the coroutines driven each frame, in execution order.
    const std::vector<std::string>& active_scripts() const {
        return active_scripts_;
    }
    const StageScript& stage() const { return stage_; }
    const ScoreSystem& scoring() const { return scoring_; }
    const ItemPool& items() const { return items_; }
    const SoundBank& sounds() const { return sounds_; }
    const BgmPlayer& bgm() const { return bgm_; }

    // One tick's worth of mixed stereo audio (22050/60 frames). The buffer is
    // owned by Application and rewritten every tick, so the pointer is valid
    // until the next tick.
    float* audio_frame() { return mix_buffer_.data(); }
    u32 audio_frame_samples() const { return u32(mix_buffer_.size()); }
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
    // Sprite index in enemy.anm for an enemy family name (see the .cpp for the
    // block layout, which is a measured property of the file).
    static u32 enemy_sprite_for_family(const std::string& family, u32 variant);
    // Turns recorded op1013 schedules into actual enemies.
    void consume_schedules();

    Archive archive_;
    std::vector<u8> archive_bytes_;
    GameSession session_;
    EclContext ecl_;
    PauseItem pause_item_ = PauseItem::Resume;
    u32 ecl_difficulty_skipped_ = 0;
    u32 ecl_first_masked_opcode_ = 0;
    u32 ecl_first_masked_rank_ = 0;
    u32 ecl_rank_ff_ = 0;
    u32 ecl_rank_other_ = 0;
    CardInventory cards_;
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
    // Owned through a pointer so this header needs no Hud definition.
    Hud* hud_ = nullptr;
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
    // Schedule count seen last frame, to detect when waves stop arriving.
    u32 last_schedules_fired_ = 0;
    // Set once the boss entity has been created for this stage.
    bool boss_spawned_ = false;
    // Fired schedule entries, keyed by (slot << 32 | time). A set rather than a
    // per-index bitmap because the interpreter re-records the same schedules
    // every tick (main is re-run from the start each frame).
    std::set<u64> fired_schedule_keys_;

    // Parsed wave templates, keyed by template index.
    //
    // Extraction and parsing used to run again on every firing: a wave bank is
    // a compressed archive entry plus an ECL parse, and stages 5/7 fire 80 of
    // them. Caching also avoids re-running each template's subroutines, which
    // was the real cost.
    std::map<i32, EclResource> wave_cache_;
    // Template indices whose subroutines have already emitted.
    std::set<i32> wave_run_once_;
    std::vector<u32> script_instrs_;
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
