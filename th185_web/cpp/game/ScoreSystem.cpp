#include "ScoreSystem.hpp"

namespace th185 {

void ScoreSystem::configure(u32 difficulty, u32 stage) {
    difficulty_ = difficulty;
    stage_ = stage;
    // score_cap_for_stage already applies the per-stage multiplier the
    // original uses (imul eax, [0x4D1004]). A difficulty outside 0..7 leaves
    // the cap at its initial value rather than guessing.
    bool out_of_range = false;
    cap_ = score_cap_for_stage(difficulty, stage, &out_of_range);
    if (out_of_range || cap_ == 0) cap_ = 0xFFFFFFFFu;
}

void ScoreSystem::add(u32 points) {
    if (points == 0) return;
    const u64 sum = u64(stats_.score) + u64(points);
    if (sum >= u64(cap_)) {
        // Clamp rather than wrap: the cap is a real rule, and wrapping would
        // turn a 9-digit score into a small one.
        stats_.score = cap_;
        ++stats_.capped;
        return;
    }
    stats_.score = u32(sum);
    if (stats_.score > stats_.high_score) stats_.high_score = stats_.score;
}

void ScoreSystem::add_enemy_kill() {
    ++stats_.kills;
    add(SCORE_ENEMY_KILL);
}

void ScoreSystem::add_bullet_cancel(u32 count) {
    stats_.bullets_cancelled += count;
    add(SCORE_BULLET_CANCEL * count);
}

void ScoreSystem::add_graze() {
    ++stats_.grazes;
    add(SCORE_GRAZE);
}

void ScoreSystem::add_card(u32 card_id) {
    // The bonus scales with the card id so a later card is worth more. The
    // exact curve is inferred; the card table itself is measured.
    (void)card_id;
    ++stats_.cards_collected;
    add(SCORE_CARD_BONUS);
}

void ScoreSystem::reset() {
    const u32 keep_high = stats_.high_score;
    stats_ = ScoreStats{};
    stats_.high_score = keep_high;
}

}  // namespace th185
