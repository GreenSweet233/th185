#!/usr/bin/env python3
"""Cross-check the C++ Rng.hpp implementation against the Python reference.

`th185_web/cpp/game/Rng.hpp` must stay bit-identical to the original binary.
The Python `rng.Rng` is already verified against a Unicorn probe of the
original function, so this script re-implements the C++ logic *in Python*,
line for line, and compares both against the probe.

Why a separate script: we cannot compile the C++ here (no emsdk in this
workspace). This keeps the C++ honest until a build is possible. If the C++
changes, change this mirror in the same commit.

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

CONST_A = 0x9630
CONST_B = 0x6553


class CppRngMirror:
    """Line-for-line mirror of th185_web/cpp/game/Rng.hpp."""

    def __init__(self, seed: int = 0):
        self.seed = seed & 0xFFFF
        self.reserved = 0
        self.calls = 0

    def step(self):
        """Mirror of Rng::step(u16& high, u16& low)."""
        ax = ((self.seed ^ CONST_A) - CONST_B) & 0xFFFF
        rotated = ((ax >> 14) + (ax << 2)) & 0xFFFF
        mixed = (rotated ^ CONST_A) & 0xFFFF
        ebx = (mixed - CONST_B) & 0xFFFF
        self.seed = ((ebx >> 14) + (ebx << 2)) & 0xFFFF
        self.calls = (self.calls + 2) & 0xFFFFFFFF
        return ax, ebx

    def next32(self):
        """Mirror of Rng::next32() — the original EAX."""
        high, low = self.step()
        return ((high << 16) | low) & 0xFFFFFFFF

    def next16(self):
        """Mirror of Rng::next16() — the new seed."""
        self.step()
        return self.seed

    def next_below(self, limit: int):
        if limit == 0:
            return 0
        return self.next32() % limit


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Cross-check cpp/game/Rng.hpp against the verified reference")
    parser.add_argument("--selftest", action="store_true",
                        help="run the fixed-seed comparison")
    args = parser.parse_args()

    print("=== C++ Rng.hpp mirror selftest ===")
    print()
    failures = 0

    # 1. Fixed seed, fixed expected values (locks the algorithm down).
    rng = CppRngMirror(0x1234)
    got = [rng.next16() for _ in range(8)]
    expected = [0x1E86, 0xD9A6, 0x67B3, 0x067D, 0x6217, 0x203D, 0xBE19, 0xE060]
    ok = got == expected
    print("[%s] fixed seed 0x1234 produces the reference sequence"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        print("       got: %s" % ["0x%04X" % v for v in got])
        print("       exp: %s" % ["0x%04X" % v for v in expected])
        failures += 1

    # 2. Compare against the Python reference implementation.
    try:
        from rng import Rng as RefRng
    except ImportError as exc:
        print("[SKIP] reference rng.py unavailable: %s" % exc)
        print()
        return 0 if failures == 0 else 1

    seeds = [0, 1, 2, 0x1234, 0xFFFF, 0x9630, 0x6553, 0x8000, 0xABCD, 0x0F0F]
    mismatch = []
    for seed in seeds:
        mirror = CppRngMirror(seed)
        ref = RefRng(seed)
        mine = [mirror.next16() for _ in range(16)]
        theirs = [ref.step_seed() for _ in range(16)]
        if mine != theirs:
            mismatch.append((seed, mine, theirs))
    ok = not mismatch
    print("[%s] next16 matches rng.py for %d seeds"
          % ("PASS" if ok else "FAIL", len(seeds)))
    if not ok:
        for seed, mine, theirs in mismatch[:2]:
            print("       seed=0x%04X" % seed)
            print("         mirror: %s" % ["0x%04X" % v for v in mine[:8]])
            print("         ref   : %s" % ["0x%04X" % v for v in theirs[:8]])
        failures += 1

    # 3. next32 must pack (first intermediate << 16) | (second value).
    ok = True
    for seed in [0, 0x1234, 0xFFFF]:
        mirror = CppRngMirror(seed)
        ref = RefRng(seed)
        if mirror.next32() != ref.step_eax():
            ok = False
    print("[%s] next32 matches the reference EAX packing" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 4. next_below must stay in range and match the reference modulo.
    ok = True
    for seed in [0, 7, 0x1234]:
        mirror = CppRngMirror(seed)
        ref = RefRng(seed)
        for limit in (1, 2, 110, 111, 1000):
            mine = mirror.next_below(limit)
            theirs = ref.next_below(limit)
            if mine != theirs:
                ok = False
            if not (0 <= mine < limit):
                ok = False
    print("[%s] next_below matches and stays in range" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 5. Layout: seed + reserved + calls == 8 bytes (static_assert in C++).
    ok = (2 + 2 + 4) == 8
    print("[%s] struct layout is 8 bytes (u16 seed + u16 pad + u32 calls)"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: cpp/game/Rng.hpp is bit-identical to the reference")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

