#include "MagicCircle.hpp"

namespace th185 {

namespace {

i32 clamp_percent(i32 value, i32 low, i32 high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

}  // namespace

void MagicCircle::apply_cards(i32 reload_percent, i32 break_percent) {
    // A negative reload percent shortens the cooldown; the original clamps so a
    // stack of PACHE cannot make it instant.
    const i32 clamped = clamp_percent(reload_percent, -80, 0);
    const u32 base = MAGIC_CIRCLE_RELOAD_FRAMES;
    // percent is negative, so this subtracts a fraction of the base.
    const u32 shortened = u32((i32(base) * (100 + clamped)) / 100);
    cooldown_total_ = shortened < 8u ? 8u : shortened;

    // A positive break percent widens the radius.
    const i32 widened = clamp_percent(break_percent, 0, 200);
    radius_ = MAGIC_CIRCLE_RADIUS * (1.0f + float(widened) / 100.0f);
}

bool MagicCircle::activate() {
    if (active_ || cooldown_ != 0) return false;
    active_ = true;
    age_ = 0;
    ++stats_.activations;
    return true;
}

void MagicCircle::update() {
    if (active_) {
        ++age_;
        ++stats_.frames_active;
        if (age_ >= MAGIC_CIRCLE_FRAMES) {
            active_ = false;
            age_ = 0;
            cooldown_ = cooldown_total_;
        }
        return;
    }
    if (cooldown_ > 0) --cooldown_;
}

u32 MagicCircle::sprite() const {
    // Eight frames across the expansion. The circle is drawn at its largest in
    // the middle of the animation, which is what the frame index walks.
    if (MAGIC_CIRCLE_SPRITE_COUNT == 0) return MAGIC_CIRCLE_SPRITE_FIRST;
    const u32 frame = age_ % MAGIC_CIRCLE_SPRITE_COUNT;
    return MAGIC_CIRCLE_SPRITE_FIRST + frame;
}

void MagicCircle::reset() {
    active_ = false;
    age_ = 0;
    cooldown_ = 0;
    cooldown_total_ = MAGIC_CIRCLE_RELOAD_FRAMES;
    radius_ = MAGIC_CIRCLE_RADIUS;
    stats_ = MagicCircleStats{};
}

}  // namespace th185