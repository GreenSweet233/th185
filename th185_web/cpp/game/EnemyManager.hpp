#pragma once
// TH185 EnemyManager: the runtime state that wave selection depends on.
//
// Wave selection is NOT driven by a static weight table. The weights are
// computed at runtime from three parallel 111-entry arrays that live inside
// EnemyManager (VA 0x4D7AF4). The layout was recovered from the fill code at
// RVA 0x3CEF2-0x3CF31 and the counter increment at RVA 0x3D243:
//
//   EM + 0x168   uint32 counter[111]     // times each template was drawn
//   EM + 0x324   uint32 weights_a[111]   // weight set A
//   EM + 0x4E0   uint32 weights_b[111]   // weight set B
//   EM + 0x4F4C  uint32 skip_index       // last drawn index
//
// The three arrays are exactly 0x1BC (444 = 111 * 4) bytes apart, which is
// what pins the entry count at 111.
//
// Weight formula (from the disassembly, see docs/WAVES.md 10):
//
//   weights_a[i] = value_a;
//   count        = counter[i];
//   weights_b[i] = value_b / (count + 1) - count;   // negative -> not eligible
//
// `counter` is incremented every time template i is drawn (RVA 0x3D243), which
// is what makes heavily drawn templates less likely over time.
//
// IMPORTANT: `value_a` and `value_b` come from two accessor calls (RVA 0x40090)
// that read a session object we have not reconstructed. They are therefore an
// INPUT here, not something this class can derive. Everything else -- the
// counter bookkeeping, the formula, the negative-result filter -- is exact.

#include "Types.hpp"
#include "WaveSelector.hpp"

#include <array>

namespace th185 {

constexpr u32 EM_COUNTER_OFFSET = 0x168;
constexpr u32 EM_WEIGHTS_A_OFFSET = 0x324;
constexpr u32 EM_WEIGHTS_B_OFFSET = 0x4E0;
constexpr u32 EM_SKIP_INDEX_OFFSET = 0x4F4C;

// EnemyManager state relevant to wave selection.
struct EnemyManager {
    std::array<u32, WAVE_TABLE_SIZE> counter{};    // +0x168
    std::array<u32, WAVE_TABLE_SIZE> weights_a{};  // +0x324
    std::array<u32, WAVE_TABLE_SIZE> weights_b{};  // +0x4E0
    u32 skip_index = 0xFFFFFFFFu;                  // +0x4F4C

    // Reset all selection state (mirrors the zero-fill at RVA 0x242B7 / 0x3CEBB).
    void reset();

    // Recompute weights from the per-template inputs.
    //   value_a[i] feeds weights_a[i] directly.
    //   value_b[i] feeds weights_b[i] via value_b/(count+1) - count,
    //   and a negative result marks the template ineligible (weights_b = 0).
    // Returns the number of templates that ended up eligible.
    u32 recompute(const std::array<u32, WAVE_TABLE_SIZE>& value_a,
                  const std::array<u32, WAVE_TABLE_SIZE>& value_b);

    // Record that `index` was drawn: increments the counter and stores the
    // skip index. Mirrors RVA 0x3D243 (inc counter[i]) + the skip write.
    void note_drawn(u32 index);

    // True when the template still has non-zero weight in either set.
    bool is_eligible(u32 index) const;
};

}  // namespace th185
