#pragma once
// TH185 items: the pickups enemies drop when they die.
//
// ## Sprite source (measured)
//
// There is no `item.anm` in the archive. The pickup icons live in
// `front.anm`, which is the foreground/HUD bank:
//
//   sprite  7..10   32x40   life fragments (heart, four fill levels)
//   sprite 12..15   32x40   stars (grey / white / green variants)
//   sprite 16, 18   32x40   outlined stars
//   sprite 19       22x22   small green star (HUD icon)
//
// Those indices were confirmed by decoding front.anm's THTX atlas and
// looking at the pixels -- see th185_web/artifacts/front-sprites.png.
//
// ## What is inferred
//
// The drop RATES and the effect of each type. TH185 has no Replay, so there
// is nothing to diff a run against; the values below follow the series.

#include "Types.hpp"

#include <vector>

namespace th185 {

constexpr u32 ITEM_CAPACITY = 256;
constexpr float ITEM_FALL_SPEED = 1.2f;
constexpr float ITEM_MARGIN = 48.0f;

// Pickup radius. Generous compared with the player hitbox: items are meant to
// be collected, not dodged.
constexpr float ITEM_PICKUP_RADIUS = 20.0f;

enum class ItemKind : u32 {
    LifeFragment = 0,   // four of these make a life
    PointStar = 1,      // raw score
    PowerStar = 2,      // placeholder until the power system exists
    Count = 3,
};

// Sprite index in front.anm for each kind. Measured, not guessed.
constexpr u16 ITEM_SPRITE_LIFE = 7;
constexpr u16 ITEM_SPRITE_STAR = 12;
constexpr u16 ITEM_SPRITE_POWER = 14;

constexpr u32 ITEM_POINTS_STAR = 500;
constexpr u32 ITEM_POINTS_POWER = 1000;
constexpr u32 LIFE_FRAGMENTS_PER_LIFE = 4;

struct Item {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    u16 sprite = ITEM_SPRITE_STAR;
    ItemKind kind = ItemKind::PointStar;
    u16 age = 0;
    bool alive = false;
};

struct ItemStats {
    u32 spawned = 0;
    u32 collected = 0;
    u32 expired = 0;
    u32 alive = 0;
    u32 life_fragments = 0;
    u32 lives_gained = 0;
};

class ItemPool {
public:
    ItemPool();

    // Drops one item at (x, y). `kind` selects the effect and the sprite.
    bool spawn(float x, float y, ItemKind kind);

    // Falls, ages out, and collects against the player. `player_x/y` are the
    // player's centre. Returns how many items were collected this frame.
    u32 update(float player_x, float player_y);

    void clear();

    const Item* items() const { return items_; }
    static constexpr u32 capacity() { return ITEM_CAPACITY; }
    const ItemStats& stats() const { return stats_; }

private:
    Item items_[ITEM_CAPACITY];
    std::vector<u32> free_slots_;
    ItemStats stats_{};
};

}  // namespace th185
