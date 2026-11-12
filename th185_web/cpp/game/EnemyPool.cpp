#include "EnemyPool.hpp"

#include <cmath>

namespace th185 {

namespace {

// Enemies are retired well outside the field so a target drifting off-screen
// does not linger forever, but also does not vanish while still partly visible.
constexpr float ENEMY_MARGIN = 128.0f;
constexpr float ENEMY_LEFT = -192.0f - ENEMY_MARGIN;
constexpr float ENEMY_RIGHT = 192.0f + ENEMY_MARGIN;
constexpr float ENEMY_TOP = -224.0f - ENEMY_MARGIN;
constexpr float ENEMY_BOTTOM = 224.0f + ENEMY_MARGIN;

}  // namespace

EnemyPool::EnemyPool() {
    enemies_.resize(ENEMY_CAPACITY);
    free_slots_.reserve(ENEMY_CAPACITY);
    for (u32 i = ENEMY_CAPACITY; i > 0; --i) {
        free_slots_.push_back(i - 1);
    }
}

bool EnemyPool::spawn(float x, float y, float vx, float vy, float radius,
                      i32 hp, u32 script, u32 handle) {
    if (free_slots_.empty()) return false;
    const u32 index = free_slots_.back();
    free_slots_.pop_back();

    Enemy& enemy = enemies_[index];
    enemy.x = x;
    enemy.y = y;
    enemy.vx = vx;
    enemy.vy = vy;
    enemy.radius = radius > 0.0f ? radius : 16.0f;
    enemy.hp = hp > 0 ? hp : 1;
    enemy.script = script;
    enemy.handle = handle;
    enemy.alive = true;

    ++stats_.spawned;
    ++stats_.alive;
    if (stats_.alive > stats_.peak_alive) stats_.peak_alive = stats_.alive;
    return true;
}

void EnemyPool::update() {
    // Snapshot: retiring pushes to free_slots_, which spawn() may consume. Doing
    // so inside this loop would let a new enemy occupy a slot the loop has
    // already passed, so it would not move this frame.
    for (u32 i = 0; i < ENEMY_CAPACITY; ++i) {
        Enemy& enemy = enemies_[i];
        if (!enemy.alive) continue;

        enemy.x += enemy.vx;
        enemy.y += enemy.vy;

        if (enemy.x < ENEMY_LEFT || enemy.x > ENEMY_RIGHT ||
            enemy.y < ENEMY_TOP || enemy.y > ENEMY_BOTTOM) {
            enemy.alive = false;
            free_slots_.push_back(i);
            --stats_.alive;
        }
    }
}

u32 EnemyPool::resolve_collisions(BulletPool* bullets) {
    if (!bullets) return 0;
    u32 hits = 0;

    // Linear scan both ways. With 256 enemies and a few hundred bullets this is
    // a few tens of thousands of distance checks per frame -- cheap enough that
    // a spatial index would be premature.
    for (u32 b = 0; b < BULLET_CAPACITY; ++b) {
        Bullet& bullet = bullets->mutable_bullet(b);
        if (!bullet.alive) continue;

        for (u32 e = 0; e < ENEMY_CAPACITY; ++e) {
            Enemy& enemy = enemies_[e];
            if (!enemy.alive) continue;

            const float dx = bullet.x - enemy.x;
            const float dy = bullet.y - enemy.y;
            // Compare squared distances: a square root per test would be pure
            // cost, and the comparison is equivalent.
            const float limit = enemy.radius;
            if (dx * dx + dy * dy > limit * limit) continue;

            // Hit.
            // release() does the bookkeeping; setting alive here as well would
            // double-count the retirement.
            bullets->release(b);
            ++stats_.hits;
            ++stats_.bullets_consumed;
            ++hits;

            if (--enemy.hp <= 0) {
                enemy.alive = false;
                free_slots_.push_back(e);
                --stats_.alive;
                ++stats_.killed;
            }
            break;  // this bullet is spent
        }
    }
    return hits;
}

void EnemyPool::clear() {
    free_slots_.clear();
    for (u32 i = 0; i < ENEMY_CAPACITY; ++i) {
        enemies_[i].alive = false;
        free_slots_.push_back(ENEMY_CAPACITY - 1 - i);
    }
    stats_.alive = 0;
}

}  // namespace th185
