#pragma once
// TH185 RNG — bit-identical to the original.
//
// Reverse-engineered from RVA 0x2800 (see docs/RNG.md). The generator is a
// 16-bit xorshift variant with constants 0x9630 / 0x6553 — the same family as
// TH11 — but the original function packs TWO values into one return:
//
//     EAX = (edi << 16) | ebx
//
// where edi is the FIRST intermediate value (before the rotate) and ebx is the
// second. The new seed is ebx. That is why the call counter advances by 2.
//
// Per step, with A = 0x9630 and B = 0x6553:
//
//     ax  = (A ^ seed) - B                       ; edi (16-bit)
//     cx  = ((((edi >> 14) + edi * 4) & 0xFFFF) ^ A) - B   ; ebx (16-bit)
//     seed = ((ebx >> 14) + ebx * 4) & 0xFFFF
//
// NOTE the `^ A` in the middle step — omitting it produces a sequence that is
// off by one position, which is exactly the bug this comment prevents.
//
// Verification: 16 seeds checked bit-identical against a Unicorn probe
// executing the original function (th185_web/scripts/native/rng.py).
//
// CRITICAL: the RNG call ORDER must match the original exactly. Wave selection
// draws from a 110-entry template pool with weights, so a single extra or
// missing call desynchronises the whole run. See AGENTS.md 铁律 5.

#include "Types.hpp"

namespace th185 {

constexpr u16 RNG_CONST_A = 0x9630;
constexpr u16 RNG_CONST_B = 0x6553;

// Matches the original layout: a 16-bit seed followed by 16 bits of padding,
// with a call counter used only for diagnostics.
struct Rng {
    u16 seed = 0;
    u16 reserved = 0;
    u32 calls = 0;

    // One full step. Returns the pair the original packs into EAX.
    //   high = first intermediate value (edi), BEFORE the rotate
    //   low  = second value (ebx), which also becomes the new seed
    void step(u16& high, u16& low) noexcept {
        // ax = (A ^ seed) - B
        const u16 ax = u16(u16(seed ^ RNG_CONST_A) - RNG_CONST_B);
        high = ax;

        // cx = (((ax >> 14) + ax * 4) & 0xFFFF) ^ A, then - B
        // ax * 4 is a 32-bit lea in the original, so no 16-bit wrap before AND.
        const u16 rotated = u16(((u32(ax) >> 14) + (u32(ax) << 2)) & 0xFFFFu);
        const u16 mixed = u16(rotated ^ RNG_CONST_A);
        const u16 ebx = u16(mixed - RNG_CONST_B);
        low = ebx;

        // Advance the seed: (ebx >> 14) + ebx * 4
        seed = u16(((u32(ebx) >> 14) + (u32(ebx) << 2)) & 0xFFFFu);
        calls += 2;
    }

    // EAX as returned by the original function.
    u32 next32() noexcept {
        u16 high = 0, low = 0;
        step(high, low);
        return (u32(high) << 16) | u32(low);
    }

    // Advance one step and return the NEW SEED.
    //
    // Note this is NOT the same as the low half of next32(): the low half is
    // ebx, while the seed is ((ebx >> 14) + ebx * 4) & 0xFFFF — one more rotate.
    // The original stores that rotated value back into the context, so a probe
    // reading the seed after each call sees this sequence, not ebx.
    u16 next16() noexcept {
        u16 high = 0, low = 0;
        step(high, low);
        return seed;
    }

    // [0, 1) as float — mirrors the original float path (RVA 0x2890).
    float unit() noexcept { return float(double(next32()) * 0x1p-32); }

    // [-1, 1) as float.
    float signed_unit() noexcept { return float(double(next32()) * 0x1p-31 - 1.0); }

    // [0, limit) — matches the original `div` + take-remainder pattern at
    // RVA 0x3D068 (wave selection). Uses the full 32-bit EAX like `div` does.
    u32 next_below(u32 limit) noexcept {
        if (limit == 0) return 0;
        return next32() % limit;
    }
};

static_assert(sizeof(Rng) == 8, "Rng must match the original 8-byte layout");

// The original keeps a single global instance at VA 0x4D7AA8.
Rng& global_rng() noexcept;

}  // namespace th185

