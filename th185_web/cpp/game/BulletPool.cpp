#include "BulletPool.hpp"

namespace th185 {

BulletPool::BulletPool() {
    bullets_.resize(BULLET_CAPACITY);
    // Free list is built once and never resized: the pool is fixed.
    free_slots_.reserve(BULLET_CAPACITY);
    for (u32 i = BULLET_CAPACITY; i > 0; --i) {
        free_slots_.push_back(i - 1);
    }
}

bool BulletPool::spawn(float x, float y, float vx, float vy, u16 sprite,
                       u32 color, bool hostile, u16 delay) {
    if (free_slots_.empty()) {
        ++stats_.pool_full;
        return false;
    }
    const u32 index = free_slots_.back();
    free_slots_.pop_back();

    Bullet& bullet = bullets_[index];
    bullet.x = x;
    bullet.y = y;
    bullet.vx = vx;
    bullet.vy = vy;
    bullet.sprite = sprite;
    bullet.color = color;
    bullet.hostile = hostile;
    bullet.delay = delay;
    bullet.age = 0;
    bullet.alive = true;

    ++stats_.spawned;
    ++stats_.alive;
    if (stats_.alive > stats_.peak_alive) stats_.peak_alive = stats_.alive;
    return true;
}

void BulletPool::update() {
    // Iterate by index: retiring a bullet pushes to free_slots_, which must not
    // be observed by this loop (a slot freed now is reusable next frame, not
    // this one -- otherwise a bullet could be spawned into a slot the loop has
    // already passed).
    for (u32 i = 0; i < BULLET_CAPACITY; ++i) {
        Bullet& bullet = bullets_[i];
        if (!bullet.alive) continue;

        // A delayed bullet is invisible and immobile until its countdown runs
        // out. It must also not be tested for collisions: ECL fans stagger
        // their shots, and a waiting bullet sitting at the muzzle would
        // otherwise hit whatever is there.
        if (bullet.delay > 0) {
            --bullet.delay;
            continue;
        }

        bullet.x += bullet.vx;
        bullet.y += bullet.vy;
        ++bullet.age;

        if (bullet.x < BULLET_LEFT || bullet.x > BULLET_RIGHT ||
            bullet.y < BULLET_TOP || bullet.y > BULLET_BOTTOM) {
            bullet.alive = false;
            free_slots_.push_back(i);
            --stats_.alive;
            ++stats_.retired;
        }
    }
}

void BulletPool::release(u32 index) {
    if (index >= BULLET_CAPACITY) return;
    Bullet& bullet = bullets_[index];
    if (!bullet.alive) return;   // double release would corrupt the free list
    bullet.alive = false;
    free_slots_.push_back(index);
    --stats_.alive;
    ++stats_.retired;
}

void BulletPool::clear() {
    free_slots_.clear();
    for (u32 i = 0; i < BULLET_CAPACITY; ++i) {
        bullets_[i].alive = false;
        bullets_[i].age = 0;
        free_slots_.push_back(BULLET_CAPACITY - 1 - i);
    }
    stats_.alive = 0;
}

}  // namespace th185
