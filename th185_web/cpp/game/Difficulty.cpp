#include "Difficulty.hpp"

namespace th185 {

namespace {

const std::array<const char*, DIFFICULTY_COUNT> kNames = {{
    "Very Easy",
    "Easy",
    "Normal",
    "Little Hard",
    "Hard",
    "Very Hard",
    "Lunatic",
    "Over Drive",
}};

// VA 0x4BCB00 (4 entries) + the immediate 0xF4240 for the 5th.
const std::array<u32, SCORE_CAP_TABLE_SIZE> kScoreCaps = {{
    500000,
    1000000,
    1500000,
    2000000,
    1000000,
}};

}  // namespace

const std::array<const char*, DIFFICULTY_COUNT>& difficulty_names() { return kNames; }

const std::array<u32, SCORE_CAP_TABLE_SIZE>& score_caps() { return kScoreCaps; }

u32 score_cap_for(u32 difficulty, bool* out_of_range) {
    // The original does `mov eax, [ebp + eax*4 - 0x18]` with eax in 0..7 and
    // only 5 slots allocated. Tiers 5..7 read adjacent stack data, so the
    // result is undefined. We cannot reproduce stack garbage; we clamp to the
    // last valid entry and report that the original was out of range.
    if (out_of_range) *out_of_range = difficulty >= SCORE_CAP_TABLE_SIZE;
    if (difficulty >= SCORE_CAP_TABLE_SIZE) {
        return kScoreCaps[SCORE_CAP_TABLE_SIZE - 1];
    }
    return kScoreCaps[difficulty];
}

u32 score_cap_for_stage(u32 difficulty, u32 stage, bool* out_of_range) {
    // RVA 0x310A1..0x310A5: load the cap, then `imul eax, [0x4D1004]`.
    const u32 cap = score_cap_for(difficulty, out_of_range);
    return cap * stage;
}

bool is_difficulty_0(u32 difficulty) { return difficulty == 0; }
bool is_difficulty_1(u32 difficulty) { return difficulty == 1; }
bool is_difficulty_2(u32 difficulty) { return difficulty == 2; }
bool is_difficulty_3(u32 difficulty) { return difficulty == 3; }

bool is_difficulty(u32 difficulty, u32 tier) {
    if (tier >= DIFFICULTY_COUNT) return false;
    return difficulty == tier;
}

bool is_valid_difficulty(u32 difficulty) { return difficulty < DIFFICULTY_COUNT; }

}  // namespace th185
