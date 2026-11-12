#pragma once
// TH185 bullet pool.
//
// ## Why a fixed pool
//
// A danmaku game spawns and retires thousands of bullets per second. Allocating
// per bullet would fragment the heap and make frame time unpredictable, so this
// is a flat array with a free list -- the same shape the original uses.
//
// ## What is here vs what is missing
//
// Implemented: spawning with a velocity, straight-line motion, off-screen
// retirement, and the sprite index each bullet draws with.
//
// Missing: collision (no player hitbox, no enemy), curved/accelerating motion,
// the ECL-driven bullet patterns, and the 600-611 geometry family. Those are
// separate systems; this one only has to be correct about motion and lifetime.
//
// ## Units
//
// Positions and velocities are in the same animation units the ANM scripts use
// (the playfield is roughly -192..192 by -224..224). A velocity of 8 means
// 8 units per frame at 60 Hz.

#include "Types.hpp"

#include <vector>

namespace th185 {

constexpr u32 BULLET_CAPACITY = 4096;

// Retire a bullet once it is this far outside the playfield.
constexpr float BULLET_MARGIN = 64.0f;
constexpr float BULLET_LEFT = -192.0f - BULLET_MARGIN;
constexpr float BULLET_RIGHT = 192.0f + BULLET_MARGIN;
constexpr float BULLET_TOP = -224.0f - BULLET_MARGIN;
constexpr float BULLET_BOTTOM = 224.0f + BULLET_MARGIN;

struct Bullet {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    // Sprite index inside the bullet atlas.
    u16 sprite = 0;
    // Tint, ARGB8888. 0xFFFFFFFF = unchanged.
    u32 color = 0xFFFFFFFFu;
    // Frames this bullet has lived. Useful for animation and for debugging.
    u16 age = 0;
    bool alive = false;
};

struct BulletStats {
    u32 spawned = 0;
    u32 retired = 0;
    u32 alive = 0;
    u32 peak_alive = 0;
    u32 pool_full = 0;
};

class BulletPool {
public:
    BulletPool();

    // Spawns one bullet. Returns false when the pool is exhausted.
    bool spawn(float x, float y, float vx, float vy, u16 sprite,
               u32 color = 0xFFFFFFFFu);

    // Advances every live bullet one frame and retires the ones that left.
    void update();

    // Retires everything (stage restart).
    void clear();

    const std::vector<Bullet>& bullets() const { return bullets_; }
    const BulletStats& stats() const { return stats_; }

private:
    std::vector<Bullet> bullets_;
    std::vector<u32> free_slots_;
    BulletStats stats_{};
};

}  // namespace th185
