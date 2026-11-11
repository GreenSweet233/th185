// Harness: exercise the REAL C++ Rng and print sequences so the Python side
// Python side can diff it against the Unicorn probe of the original function.
//
// Built by scripts/build-cpp.mjs --harness and run under Node via WASM.
// This is the only way to validate the C++ RNG directly (rather than through a
// Python mirror), so it is worth keeping.

#include "Rng.hpp"

#include <cstdio>
#include <cstdlib>

using namespace th185;

static void emit_seed(u16 seed, int count) {
    Rng rng;
    rng.seed = seed;
    rng.calls = 0;
    std::printf("%04X:", unsigned(seed));
    for (int i = 0; i < count; ++i) {
        std::printf(" %04X", unsigned(rng.next16()));
    }
    std::printf("\n");
}

static void emit_eax(u16 seed, int count) {
    Rng rng;
    rng.seed = seed;
    rng.calls = 0;
    std::printf("EAX %04X:", unsigned(seed));
    for (int i = 0; i < count; ++i) {
        std::printf(" %08X", rng.next32());
    }
    std::printf("\n");
}

static void emit_below(u16 seed, u32 limit, int count) {
    Rng rng;
    rng.seed = seed;
    rng.calls = 0;
    std::printf("BELOW %04X %u:", unsigned(seed), unsigned(limit));
    for (int i = 0; i < count; ++i) {
        std::printf(" %u", unsigned(rng.next_below(limit)));
    }
    std::printf("\n");
}

int main() {
    const u16 seeds[] = {0x0000, 0x0001, 0x0002, 0x1234, 0xFFFF,
                         0x9630, 0x6553, 0x8000, 0xABCD, 0x0F0F};
    for (u16 seed : seeds) emit_seed(seed, 16);
    const u16 eaxSeeds[] = {0x0000, 0x1234, 0xFFFF};
    for (u16 seed : eaxSeeds) emit_eax(seed, 8);
    const u16 belowSeeds[] = {0x0000, 0x1234};
    for (u16 seed : belowSeeds) {
        emit_below(seed, 1, 4);
        emit_below(seed, 110, 8);
        emit_below(seed, 111, 8);
    }
    return 0;
}
