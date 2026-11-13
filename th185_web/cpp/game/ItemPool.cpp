#include "ItemPool.hpp"

namespace th185 {

ItemPool::ItemPool() {
    free_slots_.reserve(ITEM_CAPACITY);
    for (u32 i = ITEM_CAPACITY; i > 0; --i) free_slots_.push_back(i - 1);
}

bool ItemPool::spawn(float x, float y, ItemKind kind) {
    if (free_slots_.empty()) return false;
    const u32 index = free_slots_.back();
    free_slots_.pop_back();

    Item& item = items_[index];
    item = Item{};
    item.x = x;
    item.y = y;
    item.vy = ITEM_FALL_SPEED;
    item.kind = kind;
    switch (kind) {
        case ItemKind::LifeFragment: item.sprite = ITEM_SPRITE_LIFE; break;
        case ItemKind::PowerStar:    item.sprite = ITEM_SPRITE_POWER; break;
        case ItemKind::PointStar:
        default:                     item.sprite = ITEM_SPRITE_STAR; break;
    }
    item.alive = true;
    ++stats_.spawned;
    ++stats_.alive;
    return true;
}

u32 ItemPool::update(float player_x, float player_y, std::vector<Pickup>* out) {
    u32 collected = 0;
    const float r2 = ITEM_PICKUP_RADIUS * ITEM_PICKUP_RADIUS;

    for (u32 i = 0; i < ITEM_CAPACITY; ++i) {
        Item& item = items_[i];
        if (!item.alive) continue;

        item.x += item.vx;
        item.y += item.vy;
        ++item.age;

        // Collection is checked before the off-screen test so an item that
        // reaches the player on the same frame it would leave is still
        // counted -- the pickup is the event that matters.
        const float dx = item.x - player_x;
        const float dy = item.y - player_y;
        if (dx * dx + dy * dy <= r2) {
            item.alive = false;
            free_slots_.push_back(i);
            --stats_.alive;
            ++stats_.collected;
            if (item.kind == ItemKind::LifeFragment) {
                ++stats_.life_fragments;
                if (stats_.life_fragments % LIFE_FRAGMENTS_PER_LIFE == 0) {
                    ++stats_.lives_gained;
                }
            }
            if (out) {
                Pickup pickup;
                pickup.kind = item.kind;
                switch (item.kind) {
                    case ItemKind::LifeFragment: pickup.points = 0; break;
                    case ItemKind::PowerStar:    pickup.points = ITEM_POINTS_POWER; break;
                    case ItemKind::PointStar:
                    default:                     pickup.points = ITEM_POINTS_STAR; break;
                }
                out->push_back(pickup);
            }
            ++collected;
            continue;
        }

        if (item.y > 224.0f + ITEM_MARGIN) {
            item.alive = false;
            free_slots_.push_back(i);
            --stats_.alive;
            ++stats_.expired;
        }
    }
    return collected;
}

void ItemPool::clear() {
    free_slots_.clear();
    for (u32 i = ITEM_CAPACITY; i > 0; --i) {
        items_[i - 1] = Item{};
        free_slots_.push_back(i - 1);
    }
    stats_ = ItemStats{};
}

}  // namespace th185
