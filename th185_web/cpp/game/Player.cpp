#include "Player.hpp"

namespace th185 {

namespace {

// Script indices in pl01.anm. Measured from the data: every script starts with
// 302/304/311 then a run of 300 (sprite selection), and the ones that loop use
// opcode 200. The idle/move distinction is by sprite content, so the mapping
// below is a reasonable default rather than a verified table.
constexpr u32 PLAYER_SCRIPT_IDLE = 0;
constexpr u32 PLAYER_SCRIPT_MOVE = 1;

inline float clampf(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

}  // namespace

bool Player::bind(AnmManager* manager, const AnmResource* resource) {
    if (!manager || !resource || resource->script_list.empty()) return false;
    manager_ = manager;
    resource_ = resource;
    select_script(PLAYER_SCRIPT_IDLE);
    return handle_ != 0xFFFFFFFFu;
}

void Player::select_script(u32 script_index) {
    if (!manager_ || !resource_) return;
    if (script_index >= resource_->script_list.size()) return;
    if (script_ == script_index && handle_ != 0xFFFFFFFFu) return;

    // Retire the previous instance: two player animations on screen at once
    // would double-draw and drift apart as their timers diverge.
    if (handle_ != 0xFFFFFFFFu) manager_->kill(handle_);
    handle_ = manager_->create(resource_, i32(script_index), 0, 0xFFFFFFFFu,
                               x_, y_);
    script_ = script_index;
}

void Player::respawn() {
    x_ = 0.0f;
    y_ = PLAY_START_Y;
    invulnerable_ = PLAYER_INVULNERABLE_FRAMES;
    shot_cooldown_ = 0;
}

void Player::add_life() {
    // The series caps lives at 8 for display reasons; going past that would
    // overflow the HUD rather than help the player.
    constexpr u32 MAX_LIVES = 8;
    if (lives_ >= MAX_LIVES) return;
    ++lives_;
    ++lives_gained_;
}
void Player::add_bomb() {
    // Same cap as lives: the original's bomb stock tops out at 8.
    if (bombs_left_ < 8) {
        ++bombs_left_;
        ++bombs_gained_;
    }
}

bool Player::take_bomb() {
    if (bombs_left_ == 0) return false;
    --bombs_left_;
    // A bomb grants the same invulnerability window as a hit, so it can be
    // used as an escape rather than only as damage.
    invulnerable_ = PLAYER_INVULNERABLE_FRAMES;
    return true;
}

bool Player::take_hit() {
    if (dead() || invulnerable_ > 0) return false;
    --lives_;
    invulnerable_ = PLAYER_INVULNERABLE_FRAMES;
    ++stats_.hits_taken;
    if (lives_ == 0) ++stats_.deaths;
    return true;
}

void Player::update(u32 held, BulletPool* bullets) {
    ++stats_.frames;
    if (invulnerable_ > 0) --invulnerable_;

    focused_ = (held & ButtonFocus) != 0;
    const float speed = focused_ ? PLAYER_FOCUS_SPEED : PLAYER_SPEED;
    if (focused_) ++stats_.focused_frames;

    // Diagonal movement is normalised so holding two directions is not faster
    // than one. The original does the same; without it, corners feel wrong.
    float dx = 0.0f;
    float dy = 0.0f;
    if (held & ButtonLeft) dx -= 1.0f;
    if (held & ButtonRight) dx += 1.0f;
    if (held & ButtonUp) dy -= 1.0f;
    if (held & ButtonDown) dy += 1.0f;

    if (dx != 0.0f && dy != 0.0f) {
        constexpr float INV_SQRT2 = 0.70710678f;
        dx *= INV_SQRT2;
        dy *= INV_SQRT2;
    }

    if (dx != 0.0f || dy != 0.0f) {
        x_ += dx * speed;
        y_ += dy * speed;
        ++stats_.moved_frames;
    }

    const float before_x = x_;
    const float before_y = y_;
    x_ = clampf(x_, PLAY_LEFT, PLAY_RIGHT);
    y_ = clampf(y_, PLAY_TOP, PLAY_BOTTOM);
    if (x_ != before_x || y_ != before_y) ++stats_.clamped_frames;

    // Rising edge counts as one bomb press.
    if ((held & ButtonBomb) && !(previous_held_ & ButtonBomb)) ++stats_.bombs;

    // Shooting is a HELD action with a cooldown, not an edge action: holding the
    // button must keep firing at a fixed rate. The edge counter is kept separate
    // so the existing input test still means one press.
    if ((held & ButtonShoot) && !(previous_held_ & ButtonShoot)) ++stats_.shots;
    if (held & ButtonShoot) {
        if (shot_cooldown_ == 0) {
            shot_cooldown_ = SHOT_INTERVAL_FRAMES;
            if (bullets) {
                // Two barrels, one either side of centre.
                bullets->spawn(x_ - SHOT_OFFSET, y_ - 12.0f, 0.0f, -SHOT_SPEED,
                               SHOT_SPRITE);
                bullets->spawn(x_ + SHOT_OFFSET, y_ - 12.0f, 0.0f, -SHOT_SPEED,
                               SHOT_SPRITE);
                stats_.bullets_fired += 2;
            }
        } else {
            --shot_cooldown_;
        }
    } else {
        shot_cooldown_ = 0;
    }

    previous_held_ = held;

    // Swap the animation when movement state changes.
    select_script((dx != 0.0f || dy != 0.0f) ? PLAYER_SCRIPT_MOVE
                                             : PLAYER_SCRIPT_IDLE);

    // Push the position into the running animation so it follows the player.
    if (AnmVm* vm = manager_->find(handle_)) {
        vm->translate_x = x_;
        vm->translate_y = y_;
    }
}

}  // namespace th185
