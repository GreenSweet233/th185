#include "EnemyPool.hpp"

#include <cmath>

#include "Player.hpp"

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
    enemy.sprite = script;   // the caller passes the sprite index here
    enemy.script = script;
    enemy.handle = handle;
    // Stagger the first shot so a whole wave does not fire on the same frame.
    enemy.fire_cooldown = u16(30 + (index % 60));
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
        if (enemy.fire_cooldown > 0) --enemy.fire_cooldown;

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
    // Collect the live enemies ONCE.
    //
    // The inner loop used to scan all 256 enemy slots for every bullet. With
    // BULLET_CAPACITY = 4096 that is 1048576 iterations per frame even when
    // only a handful are alive, and on stage 5 (740 live bullets, 17 enemies)
    // it cost 110 ms per frame -- 390 seconds for a 1807-frame stage.
    u32 live[ENEMY_CAPACITY];
    u32 live_count = 0;
    for (u32 e = 0; e < ENEMY_CAPACITY; ++e) {
        if (enemies_[e].alive) live[live_count++] = e;
    }
    if (live_count == 0) return 0;

    // Walk the pool's own live list.
    //
    // This used to be `for (b = 0; b < BULLET_CAPACITY; ++b)` with an alive
    // check, i.e. 4096 iterations per frame for the ~740 bullets stage 5 keeps
    // on the field. BulletPool::update() already touches every slot once, so it
    // maintains the list for free.
    const std::vector<u32>& live_bullets = bullets->live_indices();
    for (const u32 b : live_bullets) {
        Bullet& bullet = bullets->mutable_bullet(b);
        // release() during this loop does not invalidate the list: it only
        // flips `alive`, and the list is rebuilt next frame.
        if (!bullet.alive) continue;
        // Enemy fire must not hit its own side. Without this guard the aimed
        // shots are consumed the moment they leave the muzzle -- which is why
        // the player took zero hits while enemies fired 18 times.
        if (bullet.hostile) continue;

        for (u32 k = 0; k < live_count; ++k) {
            const u32 e = live[k];
            Enemy& enemy = enemies_[e];
            // Cheap vertical reject before the full distance test. Bullets and
            // enemies both stay inside a 384x448 field, so |dy| > 64 rules out
            // every radius in use (16 for enemies, 32 for bosses).
            if (bullet.y - enemy.y > 64.0f || enemy.y - bullet.y > 64.0f) {
                continue;
            }
            // The list was built before this frame's bullets were walked, so an
            // earlier bullet may already have killed this enemy. Without this
            // check the corpse is hit again and stats_.alive goes negative
            // (measured: -8 on stage 5).
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

u32 EnemyPool::fire_at(BulletPool* bullets, float target_x, float target_y) {
    if (!bullets) return 0;
    u32 fired = 0;

    for (u32 i = 0; i < ENEMY_CAPACITY; ++i) {
        Enemy& enemy = enemies_[i];
        if (!enemy.alive) continue;
        if (enemy.fire_cooldown > 0) continue;

        // Reset the timer whether or not the shot succeeds, so a full bullet pool
        // does not make every enemy retry on every frame.
        enemy.fire_cooldown = 90;

        float dx = target_x - enemy.x;
        float dy = target_y - enemy.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 0.0001f)) {
            dx = 0.0f;
            dy = 1.0f;
        } else {
            dx /= length;
            dy /= length;
        }

        // A slow aimed shot: fast enough to matter, slow enough to dodge.
        constexpr float SPEED = 3.0f;
        constexpr u32 ENEMY_SHOT_COLOR = 0xFFFF6060u;
        if (bullets->spawn(enemy.x, enemy.y, dx * SPEED, dy * SPEED, 0,
                           ENEMY_SHOT_COLOR, true)) {
            ++fired;
            ++stats_.enemy_shots;
        }
    }
    return fired;
}

u32 EnemyPool::spawn_boss(float x, float y, i32 hp, u32 sprite) {
    if (free_slots_.empty()) return 0xFFFFFFFFu;
    const u32 index = free_slots_.back();
    free_slots_.pop_back();

    Enemy& enemy = enemies_[index];
    enemy = Enemy{};
    enemy.x = x;
    enemy.y = y;
    // A boss holds position: drifting would take it off-screen and the stage
    // could never be finished.
    enemy.vx = 0.0f;
    enemy.vy = 0.0f;
    enemy.radius = 32.0f;
    enemy.hp = hp;
    enemy.sprite = sprite;
    enemy.is_boss = true;
    enemy.alive = true;
    ++stats_.spawned;
    ++stats_.alive;
    return index;
}

bool EnemyPool::boss_alive() const {
    for (u32 i = 0; i < ENEMY_CAPACITY; ++i) {
        if (enemies_[i].alive && enemies_[i].is_boss) return true;
    }
    return false;
}
u32 EnemyPool::damage_all(i32 damage) {
    if (damage <= 0) return 0;
    u32 killed = 0;
    for (u32 i = 0; i < ENEMY_CAPACITY; ++i) {
        Enemy& enemy = enemies_[i];
        if (!enemy.alive) continue;
        enemy.hp -= damage;
        if (enemy.hp > 0) continue;
        enemy.alive = false;
        free_slots_.push_back(i);
        --stats_.alive;
        ++stats_.killed;
        ++killed;
    }
    return killed;
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
