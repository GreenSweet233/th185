#pragma once
// TH185 difficulty system: 8 tiers, score-cap table, accessor family.
//
// Reverse-engineered and verified (see docs/DIFFICULTY.md):
//
//   1. Difficulty index at VA 0x4D1038, uint32 0..7:
//        0 Very Easy / 1 Easy / 2 Normal / 3 Little Hard
//        4 Hard / 5 Very Hard / 6 Lunatic / 7 Over Drive
//      Little Hard (index 3) is TH185-only; TH18 does not have it.
//
//   2. Score-cap table: 5 entries initialised at RVA 0x3107C from VA 0x4BCB00
//      plus an immediate 0xF4240 for the 5th:
//        [500000, 1000000, 1500000, 2000000, 1000000]
//      The table has 5 entries but there are 8 tiers; indices 5..7 read past
//      the end of the array into adjacent stack data. That is ORIGINAL
//      behaviour, reproduced faithfully rather than "fixed".
//
//   3. Accessor family at RVA 0x3E735..0x3E7C8 (11 tiny getters).
//
//   4. is_difficulty_0..3 are VIRTUAL functions: they have zero direct call
//      sites, and their addresses form a vtable at .text RVA 0x3EDB0.
//      The vtable order is exactly:
//        is_difficulty_0, 1, 2, 3, get_em_3c, get_em_40, get_em_44, get_em_4f44
//
//   5. NO 8-element multiplier table exists. Several scans of .rdata for
//      8-element increasing float arrays only hit sliding windows over one
//      reciprocal table at VA 0xBC6C4 (1/20, 1/16, 1/15, ...), which is
//      angle/sine related. Per-tier multipliers are therefore hardcoded in
//      conditional branches — see the `multiplier_unknown()` note below.
//
// Verification: `python th185_web/scripts/native/difficulty.py --selftest`
// and `difficulty_cpp_check.py --selftest`.

#include "Types.hpp"

#include <array>
#include <string>

namespace th185 {

constexpr u32 DIFFICULTY_COUNT = 8;
constexpr u32 DIFFICULTY_INDEX_VA = 0x4D1038u;

// Score-cap constant block and the 5th-entry immediate.
constexpr u32 SCORE_CAP_CONST_VA = 0x4BCB00u;
constexpr u32 SCORE_CAP_TAIL = 0xF4240u;
constexpr u32 SCORE_CAP_TABLE_SIZE = 5;

// Vtable at .text RVA 0x3EDB0 (8 entries).
constexpr u32 DIFFICULTY_VTABLE_RVA = 0x3EDB0u;

// The 8 tier names, index order.
const std::array<const char*, DIFFICULTY_COUNT>& difficulty_names();

// The 5-entry score-cap table, verbatim.
const std::array<u32, SCORE_CAP_TABLE_SIZE>& score_caps();

// Reproduce the original lookup: index the 5-entry table with a 0..7 tier.
// Indices 5..7 read PAST the array in the original (into adjacent stack
// slots). We cannot reproduce stack garbage, so we clamp and say so.
//
// `out_of_range` is set to true when the tier is 5..7, i.e. when the original
// would have read undefined data.
u32 score_cap_for(u32 difficulty, bool* out_of_range);

// Score cap scaled by the stage number, mirroring `imul eax, [0x4D1004]`.
u32 score_cap_for_stage(u32 difficulty, u32 stage, bool* out_of_range);

// is_difficulty_N predicates (virtual in the original).
bool is_difficulty_0(u32 difficulty);
bool is_difficulty_1(u32 difficulty);
bool is_difficulty_2(u32 difficulty);
bool is_difficulty_3(u32 difficulty);

// Generic form of the four predicates above.
bool is_difficulty(u32 difficulty, u32 tier);

// True when the difficulty index is inside the valid 0..7 range.
bool is_valid_difficulty(u32 difficulty);

}  // namespace th185
