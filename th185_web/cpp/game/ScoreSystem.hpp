#pragma once
// TH185 scoring: the run's score, the difficulty-scaled cap, and the
// per-event values.
//
// ## What is established vs inferred
//
// ESTABLISHED (from the shipped data):
//   * the 5-entry score-cap table and its per-stage multiplier
//     (docs/DIFFICULTY.md, Difficulty.hpp).
//   * the save file's CR records store a u32 score per slot.
//
// INFERRED (not proven against the original):
//   * the per-event point values below. TH185 has no Replay, so there is no
//     recording to diff a run against; the numbers are conventional for the
//     series and are marked `inferred` in the ledger.
//
// The cap is the part that matters for correctness: it is read from the
// executable, and a run that exceeds it must clamp.

#include "Difficulty.hpp"
#include "Types.hpp"

namespace th185 {

// Point values, inferred. TH11 uses these magnitudes; TH185 has no published
// table, so they are a starting point rather than a measurement.
constexpr u32 SCORE_ENEMY_KILL = 100;
constexpr u32 SCORE_BULLET_CANCEL = 10;
constexpr u32 SCORE_GRAZE = 50;
constexpr u32 SCORE_CARD_BONUS = 1000;

// Grazing: the ring outside the hitbox that still counts as a near miss.
// The player's hitbox is 2 units, so 24 gives a generous band -- the series
// uses roughly this ratio.
constexpr float GRAZE_RADIUS = 24.0f;

struct ScoreStats {
    u32 score = 0;
    u32 high_score = 0;
    u32 kills = 0;
    u32 grazes = 0;
    u32 bullets_cancelled = 0;
    u32 cards_collected = 0;
    // Times the cap clamped a gain. A non-zero value means the run is at the
    // ceiling for its difficulty and stage, which is real behaviour rather
    // than an error.
    u32 capped = 0;
};

class ScoreSystem {
public:
    // `difficulty` is the 0..7 index; `stage` is the 0-based scene index.
    void configure(u32 difficulty, u32 stage);

    void add_enemy_kill();
    void add_bullet_cancel(u32 count = 1);
    void add_graze();
    void add_card(u32 card_id);

    // Adds raw points, clamped by the difficulty/stage cap.
    void add(u32 points);

    u32 score() const { return stats_.score; }
    u32 cap() const { return cap_; }
    bool at_cap() const { return stats_.score >= cap_; }
    const ScoreStats& stats() const { return stats_; }

    void reset();

private:
    ScoreStats stats_{};
    u32 cap_ = 0xFFFFFFFFu;
    u32 difficulty_ = 0;
    u32 stage_ = 0;
};

}  // namespace th185
