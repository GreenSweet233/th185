#pragma once
// TH185 enemy pool and bullet-vs-enemy collision.
//
// ## Scope
//
// This is NOT the game's enemy system. TH185 spawns enemies from ECL scripts,
// which drive their movement, firing and death. None of that is here.
//
// What IS here: a pool of targets that can be hit, and the collision test that
// connects the player's bullets to them. That is the smallest thing that makes
// shooting mean something, and it is verifiable on its own -- hit a target,
// the target loses HP, the bullet disappears.
//
// ## Hitbox
//
// Every target uses a CIRCLE hitbox. TH uses circles for enemies too; a circle
// is rotation-invariant, so a spinning enemy does not need a rebuilt hitbox.
// The radius is derived from the sprite size at spawn time.

#include "AnmManager.hpp"
#include "AnmResource.hpp"
#include "BulletPool.hpp"
#include "Types.hpp"

#include <vector>

namespace th185 {

constexpr u32 ENEMY_CAPACITY = 256;

struct Enemy {
    float x = 0.0f;
    float y = 0.0f;
    // Movement per frame. Enemies in this build drift in a straight line.
    float vx = 0.0f;
    float vy = 0.0f;
    // Circle hitbox.
    float radius = 16.0f;
    i32 hp = 1;
    // Frames until this enemy fires again. 0 means "fire now".
    u16 fire_cooldown = 0;
    // Which sprite of the enemy bank to draw. Selected by the wave template.
    u32 sprite = 0;
    // Which bank script drives it (for the animation instance).
    u32 script = 0;
    // The animation instance owned by this enemy, or UINT32_MAX.
    u32 handle = 0xFFFFFFFFu;
    // A boss is an enemy with a large HP pool that does not drift off-screen.
    // Only one is live at a time; its defeat is what ends the stage.
    bool is_boss = false;
    bool alive = false;
};

struct EnemyStats {
    u32 spawned = 0;
    u32 killed = 0;
    u32 alive = 0;
    u32 peak_alive = 0;
    u32 hits = 0;
    u32 bullets_consumed = 0;
    u32 enemy_shots = 0;
    u32 player_hits = 0;
};

class EnemyPool {
public:
    EnemyPool();

    // Spawns a drifting target. `radius` is the hitbox, not the sprite size.
    bool spawn(float x, float y, float vx, float vy, float radius, i32 hp,
               u32 script, u32 handle = 0xFFFFFFFFu);

    // Moves live enemies and retires the ones far outside the field.
    void update();

    // Tests every live bullet against every live enemy. A bullet that hits is
    // consumed; an enemy at zero HP dies. Returns the number of hits.
    // `damage` is how much one player bullet takes off. It comes from the
    // player's shot power, which is what makes a 7000-HP boss killable.
    u32 resolve_collisions(BulletPool* bullets, i32 damage = 1);

    // Fires from every live enemy into `bullets` as HOSTILE shots, aimed at
    // (target_x, target_y). Returns the number of shots spawned.
    u32 fire_at(BulletPool* bullets, float target_x, float target_y);

    void clear();

    // Applies `damage` to every live enemy, retiring the ones that reach 0 HP.
    // Returns how many died.
    u32 damage_all(i32 damage);

    // Spawns the stage boss at (x, y) with `hp` health. Returns its slot, or
    // 0xFFFFFFFF when the pool is full.
    u32 spawn_boss(float x, float y, i32 hp, u32 sprite);

    // True when a boss is alive.
    bool boss_alive() const;

    const std::vector<Enemy>& enemies() const { return enemies_; }
    const EnemyStats& stats() const { return stats_; }

private:
    std::vector<Enemy> enemies_;
    std::vector<u32> free_slots_;
    EnemyStats stats_{};
};

}  // namespace th185
