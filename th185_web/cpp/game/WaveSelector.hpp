#pragma once
// TH185 wave template selection — the game is central mechanic.
//
// Unlike TH11/TH18, TH185 does not have authored per-stage enemy layouts for
// the mid-stage waves. Instead each scene draws waves at random from a global
// pool of 110 templates, weighted. Getting this wrong (or getting the RNG call
// ORDER wrong) desynchronises the entire run, which is why AGENTS.md 铁律 5
// says the RNG must be bit-identical.
//
// Reverse-engineered from RVA 0x3CFFF-0x3D0E6 and verified (see docs/WAVES.md):
//
//   * name table at VA 0x4B5D50, 111 entries:
//       [0]      empty placeholder (never selectable in practice)
//       [1]      Boss01t        (tutorial boss)
//       [2..28]  Boss01..Boss27 (27 regular bosses)
//       [29..31] Wave01t..Wave03t (tutorial mid-stage)
//       [32..110] Wave01..Wave79  (79 regular mid-stage)
//     => 110 selectable templates (index 0 is a placeholder).
//
//   * weights live in EnemyManager (VA 0x4D7AF4):
//       +0x324   weight array A
//       +0x4E0   weight array B (Boss Rush path)
//       +0x4F4C  skip index
//
//   * selection is two passes, both skipping `skip_index` so the same template
//     never appears twice in a row:
//       pass 1: total = sum of weights (skipping skip_index)
//       roll    = rng.next_below(total)
//       pass 2: roll -= weights[i] until roll < 0; that i is the result
//
// Verification: `python th185_web/scripts/native/waves.py --selftest` /
// `--selftest-algo` / `--selftest-force`, plus `waves_cpp_check.py`.

#include "Rng.hpp"
#include "Types.hpp"

#include <array>
#include <string>
#include <vector>

namespace th185 {

constexpr u32 WAVE_TABLE_SIZE = 111;
constexpr u32 WAVE_NAME_TABLE_VA = 0x4B5D50u;

// Index ranges inside the name table.
constexpr u32 WAVE_INDEX_EMPTY = 0;
constexpr u32 WAVE_INDEX_TUTORIAL_BOSS = 1;
constexpr u32 WAVE_INDEX_BOSS_FIRST = 2;
constexpr u32 WAVE_INDEX_BOSS_LAST = 28;      // Boss01..Boss27
constexpr u32 WAVE_INDEX_TUTORIAL_WAVE_FIRST = 29;
constexpr u32 WAVE_INDEX_TUTORIAL_WAVE_LAST = 31;
constexpr u32 WAVE_INDEX_WAVE_FIRST = 32;
constexpr u32 WAVE_INDEX_WAVE_LAST = 110;

// Forced-boss constants (RVA 0x3D121 `mov esi,0x1B` / 0x3D151 `mov esi,0x1C`).
constexpr u32 WAVE_FORCED_BOSS_STAGE5 = 27;   // Boss26
constexpr u32 WAVE_FORCED_BOSS_STAGE7 = 28;   // Boss27

// The 111-entry template name table, in index order.
const std::array<std::string, WAVE_TABLE_SIZE>& wave_names();

// Weight storage. The game keeps two arrays of 111 u32 in EnemyManager.
struct WaveWeights {
    std::array<u32, WAVE_TABLE_SIZE> a{};   // EnemyManager + 0x324
    std::array<u32, WAVE_TABLE_SIZE> b{};   // EnemyManager + 0x4E0
    u32 skip_index = 0xFFFFFFFFu;           // EnemyManager + 0x4F4C
};

// Pass 1 of the original: sum the weights, skipping `skip_index`.
u32 wave_weight_total(const std::array<u32, WAVE_TABLE_SIZE>& weights,
                      u32 skip_index);

// Pass 2 of the original: subtract weights from `roll` until it goes negative.
// Returns the hit index, or -1 when nothing matches (should not happen when
// `roll` came from wave_weight_total).
i32 wave_pick_index(const std::array<u32, WAVE_TABLE_SIZE>& weights,
                    u32 roll, u32 skip_index);

// Full selection: sum -> RNG modulo -> subtract.
// Mirrors the original call sequence exactly (ONE rng.next_below() call).
i32 select_wave(const std::array<u32, WAVE_TABLE_SIZE>& weights, Rng& rng,
                u32 skip_index);

// Forced-boss override. The original hardcodes an index when the stage and
// card id match: stage 5 + card 0 -> Boss26, stage 7 + card 66 -> Boss27.
// Returns 0xFFFFFFFF when no override applies.
u32 forced_boss_index(u32 stage, u32 card_id);

}  // namespace th185
