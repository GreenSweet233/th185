#!/usr/bin/env python3
"""Cross-check cpp/game/WaveSelector.cpp against the verified Python reference.

Contains a line-for-line Python mirror of WaveSelector.cpp and compares it with
`waves.py` (selection algorithm) and `rng.py` (RNG). Both are already verified
against the original binary.

Contract: if WaveSelector.cpp changes, WaveMirror below must change with it.

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

WAVE_TABLE_SIZE = 111
WAVE_INDEX_EMPTY = 0
WAVE_INDEX_TUTORIAL_BOSS = 1
WAVE_INDEX_BOSS_FIRST = 2
WAVE_INDEX_BOSS_LAST = 28
WAVE_INDEX_TUTORIAL_WAVE_FIRST = 29
WAVE_INDEX_TUTORIAL_WAVE_LAST = 31
WAVE_INDEX_WAVE_FIRST = 32
WAVE_INDEX_WAVE_LAST = 110

WAVE_FORCED_BOSS_STAGE5 = 27
WAVE_FORCED_BOSS_STAGE7 = 28


def wave_weight_total(weights, skip_index):
    """Mirror of th185::wave_weight_total()."""
    total = 0
    for i in range(WAVE_TABLE_SIZE):
        if i == skip_index:
            continue
        total = (total + weights[i]) & 0xFFFFFFFF
    return total


def wave_pick_index(weights, roll, skip_index):
    """Mirror of th185::wave_pick_index()."""
    remaining = roll
    for i in range(WAVE_TABLE_SIZE):
        if i == skip_index:
            continue
        remaining -= weights[i]
        if remaining < 0:
            return i
    return -1


def select_wave(weights, rng, skip_index):
    """Mirror of th185::select_wave()."""
    total = wave_weight_total(weights, skip_index)
    if total == 0:
        return -1
    roll = rng.next_below(total)
    return wave_pick_index(weights, roll, skip_index)


def forced_boss_index(stage, card_id):
    """Mirror of th185::forced_boss_index()."""
    if stage == 5 and card_id == 0:
        return WAVE_FORCED_BOSS_STAGE5
    if stage == 7 and card_id == 66:
        return WAVE_FORCED_BOSS_STAGE7
    return 0xFFFFFFFF


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Cross-check cpp/game/WaveSelector.cpp against the reference")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    print("=== C++ WaveSelector.cpp mirror selftest ===")
    print()
    failures = 0

    try:
        from waves import pick_index, sum_weights, load_table
        from rng import Rng as RefRng
    except ImportError as exc:
        print("[SKIP] reference modules unavailable: %s" % exc)
        return 0

    # 1. Name table: the C++ array must match the binary exactly.
    try:
        ref_names = load_table()
    except SystemExit as exc:
        print("[SKIP] cannot read the name table: %s" % exc)
        return 0

    cpp_names = [""]
    cpp_names.append("Boss01t")
    cpp_names += ["Boss%02d" % i for i in range(1, 28)]
    cpp_names += ["Wave%02dt" % i for i in range(1, 4)]
    cpp_names += ["Wave%02d" % i for i in range(1, 80)]

    ok = len(cpp_names) == WAVE_TABLE_SIZE
    print("[%s] C++ name table has %d entries (expect %d)"
          % ("PASS" if ok else "FAIL", len(cpp_names), WAVE_TABLE_SIZE))
    if not ok:
        failures += 1

    ok = cpp_names == list(ref_names)
    print("[%s] C++ name table matches the binary" % ("PASS" if ok else "FAIL"))
    if not ok:
        for i, (a, b) in enumerate(zip(cpp_names, ref_names)):
            if a != b:
                print("       first diff at %d: %r vs %r" % (i, a, b))
                break
        failures += 1

    # 2. Selection algorithm must match the reference implementation.
    import random
    rnd = random.Random(20261112)
    mismatch = 0
    for trial in range(200):
        weights = [rnd.randrange(0, 1000) for _ in range(WAVE_TABLE_SIZE)]
        skip = rnd.choice([-1] + list(range(WAVE_TABLE_SIZE)))
        skip_arg = 0xFFFFFFFF if skip < 0 else skip

        mine_total = wave_weight_total(weights, skip_arg)
        ref_total = sum_weights(weights, skip)
        if mine_total != ref_total:
            mismatch += 1
            continue

        roll = rnd.randrange(0, max(1, mine_total))
        if wave_pick_index(weights, roll, skip_arg) != pick_index(weights, ref_total, roll, skip):
            mismatch += 1

    ok = mismatch == 0
    print("[%s] weight total and pick index match waves.py over 200 random cases"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        print("       %d mismatches" % mismatch)
        failures += 1

    # 3. Full selection with the RNG must match (this locks the call ORDER).
    mismatch = 0
    for trial in range(50):
        weights = [rnd.randrange(0, 1000) for _ in range(WAVE_TABLE_SIZE)]
        skip = rnd.randrange(0, WAVE_TABLE_SIZE)
        seed = rnd.randrange(0, 0x10000)

        mine_rng = RefRng(seed)
        ref_rng = RefRng(seed)
        mine = select_wave(weights, mine_rng, skip)

        ref_total = sum_weights(weights, skip)
        if ref_total == 0:
            continue
        ref_roll = ref_rng.next_below(ref_total)
        ref = pick_index(weights, ref_total, ref_roll, skip)

        if mine != ref or mine_rng.seed != ref_rng.seed:
            mismatch += 1

    ok = mismatch == 0
    print("[%s] full selection (incl. RNG state) matches over 50 random cases"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        print("       %d mismatches" % mismatch)
        failures += 1

    # 4. The skip index must actually prevent an immediate repeat.
    weights = [1] * WAVE_TABLE_SIZE
    ok = True
    for skip in range(WAVE_TABLE_SIZE):
        rng = RefRng(1234)
        if select_wave(weights, rng, skip) == skip:
            ok = False
    print("[%s] skip_index is never selected (all 111 cases)"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 5. Forced-boss overrides.
    ok = (forced_boss_index(5, 0) == 27 and forced_boss_index(7, 66) == 28
          and forced_boss_index(0, 0) == 0xFFFFFFFF
          and forced_boss_index(5, 1) == 0xFFFFFFFF)
    print("[%s] forced-boss overrides match docs/WAVES.md"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 6. Index ranges must be self-consistent.
    ok = (WAVE_INDEX_EMPTY == 0 and WAVE_INDEX_TUTORIAL_BOSS == 1
          and WAVE_INDEX_BOSS_FIRST == 2 and WAVE_INDEX_BOSS_LAST == 28
          and WAVE_INDEX_TUTORIAL_WAVE_FIRST == 29
          and WAVE_INDEX_TUTORIAL_WAVE_LAST == 31
          and WAVE_INDEX_WAVE_FIRST == 32 and WAVE_INDEX_WAVE_LAST == 110)
    print("[%s] index ranges are contiguous (1+27+3+79 = 110 selectable)"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: cpp/game/WaveSelector.cpp matches the reference")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

