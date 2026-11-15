#pragma once
// TH185 player entity.
//
// ## Why this exists
//
// The application could accept input but had nowhere to put it: `tick(held)`
// recorded a bitmask and nothing consumed it. This file is the consumer.
//
// ## What it is NOT
//
// This is not the original player. TH185's player has shot types, options,
// graze, deathbombs, focus movement and a card system, none of which are here.
// What IS here is the movement and animation-selection core: eight-direction
// input, focus (half speed), field clamping, and picking a sprite from the
// player atlas. Those are the parts whose behaviour is directly observable
// from the input bitmask, so they can be implemented without guessing.
//
// ## Numbers
//
// `PL_SPEED` and `PL_FOCUS_SPEED` are the classic TH values (4.0 and 2.0
// units/frame at 60 Hz). TH185 is in the same engine family, but nobody has
// measured its exact values yet -- so they are named constants with this note
// rather than magic numbers buried in the update loop.

#include "AnmManager.hpp"
#include "BulletPool.hpp"
#include "AnmResource.hpp"
#include "Types.hpp"

namespace th185 {

// Buttons, matching the bitmask the shell sends.
enum PlayerButton : u32 {
    ButtonShoot = 1u << 0,
    ButtonBomb = 1u << 1,
    ButtonFocus = 1u << 2,
    ButtonSpecial = 1u << 3,
    ButtonUp = 1u << 4,
    ButtonDown = 1u << 5,
    ButtonLeft = 1u << 6,
    ButtonRight = 1u << 7,
};

// Playfield bounds in animation units. TH185 renders to a 640x480 surface and
// keeps the player inside a narrower box than the full canvas.
constexpr float PLAY_LEFT = -192.0f;
constexpr float PLAY_RIGHT = 192.0f;
constexpr float PLAY_TOP = -224.0f;
constexpr float PLAY_BOTTOM = 224.0f;
// Where the player starts and respawns: near the bottom, centred.
constexpr float PLAY_START_Y = 180.0f;

// Deathbomb window and invulnerability, in frames. After a hit the player is
// briefly invulnerable so a dense pattern cannot drain every life in one frame.
constexpr u32 PLAYER_INVULNERABLE_FRAMES = 90;
constexpr u32 PLAYER_STARTING_LIVES = 3;
// The player's own hitbox is much smaller than its sprite -- this is the whole
// point of a danmaku game's "grazing" feel. TH uses roughly 2 units.
constexpr float PLAYER_HITBOX_RADIUS = 2.0f;

// Shot power, in the original's units.
//
// TH20's player_state.cpp clamps the power field at 0x38 to 100..400 and
// loading.cpp quadruples the starting value on some stages
// (`mode == 2 || (stage >= 2 && stage != 7) ? power*4 : power`). Damage is
// capped by a table lookup that indexes on the power tier, so power is the
// multiplier that makes a 7000-HP boss killable.
constexpr i32 PLAYER_POWER_MIN = 100;
constexpr i32 PLAYER_POWER_MAX = 400;
constexpr i32 PLAYER_POWER_START = 100;
// Per-hit damage at power 100.
//
// This is a CALIBRATION, not a recovered constant. TH20's damage.cpp reads the
// cap from a parameter table indexed by power tier
// (`parameters[0x28 + tier*12]`), and that table has not been decoded.
//
// What is known: a boss has 7000 HP (op511) and a player lands ~4000 hits
// inside a 10000-frame stage. 7000/4000 = 1.75, so any value below 2 leaves the
// boss unkillable; 4 gives a comfortable margin (16000 damage).
constexpr i32 PLAYER_BASE_DAMAGE = 4;

constexpr float PLAYER_SPEED = 4.0f;
constexpr float PLAYER_FOCUS_SPEED = 2.0f;

// Player shot geometry. Two bullets per volley, offset left and right of the
// player centre, travelling straight up. TH185's real shot patterns are
// ECL-driven and not reconstructed yet, so this is a placeholder that at least
// exercises the bullet pipeline end to end.
constexpr float SHOT_OFFSET = 6.0f;
constexpr float SHOT_SPEED = 14.0f;
constexpr u32 SHOT_INTERVAL_FRAMES = 4;
constexpr u16 SHOT_SPRITE = 0;

struct PlayerStats {
    u32 frames = 0;
    u32 moved_frames = 0;
    u32 focused_frames = 0;
    u32 shots = 0;
    u32 bombs = 0;
    // Bombs in stock. TH185 starts with 3 like the rest of the series.
    u32 bombs_left = 3;
    u32 clamped_frames = 0;
    u32 bullets_fired = 0;
    u32 hits_taken = 0;
    u32 deaths = 0;
};

class Player {
public:
    // Binds to the player animation bank and spawns the idle script.
    bool bind(AnmManager* manager, const AnmResource* resource);

    // Consumes one frame of input. `held` is the bitmask from the shell.
    // Shots are spawned into `bullets`.
    void update(u32 held, BulletPool* bullets);

    // Applies one hit. Ignored while invulnerable. Returns true when a life was
    // actually lost.
    bool take_hit();

    // Puts the player back at the start position with the invulnerability
    // window active, keeping the life count. Called after a death.
    void respawn();

    // Consumes one bomb. Returns false when none are left.
    bool take_bomb();
    u32 bombs_remaining() const { return bombs_left_; }

    u32 lives() const { return lives_; }
    // Shot power, clamped like the original's 0x38 field.
    i32 power() const { return power_; }
    void add_power(i32 delta);
    // Damage one hit does: base scaled by the power tier.
    i32 shot_damage() const {
        return PLAYER_BASE_DAMAGE * (power_ / PLAYER_POWER_START);
    }

    // Grants one life. Capped so a fragment chain cannot overflow the HUD.
    void add_life();
    // Adds one bomb, capped like lives.
    void add_bomb();
    u32 bombs_gained() const { return bombs_gained_; }
    u32 lives_gained() const { return lives_gained_; }
    u32 invulnerable_frames() const { return invulnerable_; }
    bool dead() const { return lives_ == 0; }
    float hitbox_radius() const { return PLAYER_HITBOX_RADIUS; }

    float x() const { return x_; }
    float y() const { return y_; }
    bool focused() const { return focused_; }
    u32 handle() const { return handle_; }
    const PlayerStats& stats() const { return stats_; }

private:
    void select_script(u32 script_index);

    AnmManager* manager_ = nullptr;
    const AnmResource* resource_ = nullptr;
    u32 handle_ = 0xFFFFFFFFu;
    u32 script_ = 0xFFFFFFFFu;

    float x_ = 0.0f;
    float y_ = 192.0f;   // starts near the bottom, as in the original
    bool focused_ = false;
    u32 previous_held_ = 0;
    u32 shot_cooldown_ = 0;
    u32 lives_ = PLAYER_STARTING_LIVES;
    i32 power_ = PLAYER_POWER_START;
    u32 lives_gained_ = 0;
    u32 invulnerable_ = 0;
    u32 bombs_left_ = 3;
    u32 bombs_gained_ = 0;
    PlayerStats stats_{};
};

}  // namespace th185
