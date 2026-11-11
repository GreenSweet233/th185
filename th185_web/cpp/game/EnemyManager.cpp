#include "EnemyManager.hpp"

namespace th185 {

void EnemyManager::reset() {
    counter.fill(0);
    weights_a.fill(0);
    weights_b.fill(0);
    skip_index = 0xFFFFFFFFu;
}

u32 EnemyManager::recompute(const std::array<u32, WAVE_TABLE_SIZE>& value_a,
                            const std::array<u32, WAVE_TABLE_SIZE>& value_b) {
    u32 eligible = 0;
    for (u32 i = 0; i < WAVE_TABLE_SIZE; ++i) {
        // weights_a[i] = value_a  (straight copy, RVA 0x3CF0E).
        weights_a[i] = value_a[i];

        // weights_b[i] = value_b / (count + 1) - count   (RVA 0x3CF15-0x3CF2A).
        //
        // The original divides a SIGNED value and then tests the sign:
        //   03CF20  idiv ecx
        //   03CF28  sub  eax, esi
        //   03CF31  jns  keep
        // A negative result means "this template has been drawn enough"; it is
        // stored but must not be treated as a positive weight. We store 0 so the
        // existing selection loop (which subtracts weights) behaves correctly.
        const u32 count = counter[i];
        const i32 numerator = i32(value_b[i]);
        const i32 divisor = i32(count + 1u);
        const i32 quotient = (divisor != 0) ? (numerator / divisor) : 0;
        const i32 result = quotient - i32(count);
        if (result < 0) {
            weights_b[i] = 0;
        } else {
            weights_b[i] = u32(result);
            ++eligible;
        }
    }
    return eligible;
}

void EnemyManager::note_drawn(u32 index) {
    if (index >= WAVE_TABLE_SIZE) return;
    // RVA 0x3D243: inc dword ptr [eax + esi*4 + 0x168]
    ++counter[index];
    skip_index = index;
}

bool EnemyManager::is_eligible(u32 index) const {
    if (index >= WAVE_TABLE_SIZE) return false;
    return weights_a[index] != 0 || weights_b[index] != 0;
}

}  // namespace th185
