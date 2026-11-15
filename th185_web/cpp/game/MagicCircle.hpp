#pragma once
// TH185 magic circle.
//
// ## What it is
//
// TH185's defining mechanic. The player expands a circle around themselves;
// enemy bullets that overlap it are consumed and converted into bullet money,
// which is the only currency the black market accepts. Two cards tune it, and
// their descriptions in ability.txt state the behaviour:
//
//   PACHE  魔方陣の攻撃力を上げる     ＊複数購入可 枚数に応じて攻撃力が上がります
//   FLAN   魔方陣で敵弾を破壊できる   ＊複数購入可 枚数に応じて破壊量が増えます
//
// Those are the two HUD modifiers `M.Reload` and `M.Break`.
//
// ## Provenance
//
// The art is `pl01.anm` sprites 53..60: eight consecutive 64x64 frames, the
// only 64x64 sprites in the player atlas (the player body is 30x46). Eight
// frames is the animation length.
//
// The numbers below are CALIBRATION, not recovered constants: the binary has
// no table this build has located for the circle's radius or duration. They
// are chosen so the circle is useful without being free.

#include "Types.hpp"

namespace th185 {

// Frames the circle stays expanded.
constexpr u32 MAGIC_CIRCLE_FRAMES = 45;
// Frames before it can be expanded again.
constexpr u32 MAGIC_CIRCLE_RELOAD_FRAMES = 120;
// Radius in play-field units.
constexpr float MAGIC_CIRCLE_RADIUS = 44.0f;
// First sprite index in pl01.anm; the animation is 8 frames long.
constexpr u32 MAGIC_CIRCLE_SPRITE_FIRST = 53;
constexpr u32 MAGIC_CIRCLE_SPRITE_COUNT = 8;

struct MagicCircleStats {
    u32 activations = 0;
    u32 bullets_absorbed = 0;
    u32 frames_active = 0;
};

class MagicCircle {
public:
    // Applies the two card modifiers. `reload_percent` shortens the cooldown
    // (negative is faster); `break_percent` widens the radius.
    void apply_cards(i32 reload_percent, i32 break_percent);

    // Pressing the special button while ready expands the circle.
    // Returns true on the frame it activates.
    bool activate();

    // Advances one frame.
    void update();

    bool active() const { return active_; }
    bool ready() const { return cooldown_ == 0; }
    u32 cooldown() const { return cooldown_; }
    u32 cooldown_total() const { return cooldown_total_; }
    float radius() const { return radius_; }
    u32 age() const { return age_; }
    // Sprite index into pl01.anm for the current frame.
    u32 sprite() const;

    const MagicCircleStats& stats() const { return stats_; }

    void reset();

private:
    bool active_ = false;
    u32 age_ = 0;
    u32 cooldown_ = 0;
    u32 cooldown_total_ = MAGIC_CIRCLE_RELOAD_FRAMES;
    float radius_ = MAGIC_CIRCLE_RADIUS;
    MagicCircleStats stats_;
};

}  // namespace th185