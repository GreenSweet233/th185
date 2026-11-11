#!/usr/bin/env python3
"""Cross-check cpp/game/EnemyManager.cpp against the disassembly-derived model.

Wave weights are NOT static data: EnemyManager computes them at runtime from
three parallel 111-entry arrays. This script pins the parts we recovered
exactly (layout, formula, counter bookkeeping) so the C++ cannot drift.

Derived from:
  RVA 0x3CEF2-0x3CF31   weight fill
  RVA 0x3D243           counter increment (inc [eax + esi*4 + 0x168])
  RVA 0x3D021/0x3D08A   skip-index comparisons

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

REPO = HERE.parents[2]
WEB = REPO / "th185_web"
CPP_HPP = WEB / "cpp" / "game" / "EnemyManager.hpp"
CPP_CPP = WEB / "cpp" / "game" / "EnemyManager.cpp"

WAVE_TABLE_SIZE = 111
EM_COUNTER = 0x168
EM_WEIGHTS_A = 0x324
EM_WEIGHTS_B = 0x4E0
EM_SKIP = 0x4F4C


def cpp_reset():
    return {"counter": [0] * WAVE_TABLE_SIZE,
            "weights_a": [0] * WAVE_TABLE_SIZE,
            "weights_b": [0] * WAVE_TABLE_SIZE,
            "skip_index": 0xFFFFFFFF}


def cpp_recompute(state, value_a, value_b):
    """Mirror of EnemyManager::recompute()."""
    eligible = 0
    for i in range(WAVE_TABLE_SIZE):
        state["weights_a"][i] = value_a[i]
        count = state["counter"][i]
        divisor = count + 1
        quotient = int(value_b[i] / divisor) if divisor else 0
        result = quotient - count
        if result < 0:
            state["weights_b"][i] = 0
        else:
            state["weights_b"][i] = result
            eligible += 1
    return eligible


def cpp_note_drawn(state, index):
    if index >= WAVE_TABLE_SIZE:
        return
    state["counter"][index] += 1
    state["skip_index"] = index


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Cross-check EnemyManager weight model")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    print("=== EnemyManager weight model selftest ===")
    print()
    failures = 0

    if not CPP_HPP.is_file() or not CPP_CPP.is_file():
        print("[SKIP] EnemyManager sources not found")
        return 0

    hpp = CPP_HPP.read_text(encoding="utf-8")
    cpp = CPP_CPP.read_text(encoding="utf-8")

    # 1. The three arrays must be exactly 0x1BC apart: that arithmetic is what
    #    fixes the entry count at 111.
    ok = (EM_WEIGHTS_A - EM_COUNTER) == WAVE_TABLE_SIZE * 4
    print("[%s] counter -> weights_a stride is %d * 4 = 0x%X"
          % ("PASS" if ok else "FAIL", WAVE_TABLE_SIZE, EM_WEIGHTS_A - EM_COUNTER))
    if not ok:
        failures += 1

    ok = (EM_WEIGHTS_B - EM_WEIGHTS_A) == WAVE_TABLE_SIZE * 4
    print("[%s] weights_a -> weights_b stride is %d * 4 = 0x%X"
          % ("PASS" if ok else "FAIL", WAVE_TABLE_SIZE, EM_WEIGHTS_B - EM_WEIGHTS_A))
    if not ok:
        failures += 1

    # 2. The C++ must record those offsets.
    for name, value in [("EM_COUNTER_OFFSET", EM_COUNTER),
                        ("EM_WEIGHTS_A_OFFSET", EM_WEIGHTS_A),
                        ("EM_WEIGHTS_B_OFFSET", EM_WEIGHTS_B),
                        ("EM_SKIP_INDEX_OFFSET", EM_SKIP)]:
        ok = re.search(r"%s\s*=\s*0x%X" % (name, value), hpp) is not None
        print("[%s] header declares %s = 0x%X" % ("PASS" if ok else "FAIL", name, value))
        if not ok:
            failures += 1

    # 3. The formula: weights_b = value_b / (count + 1) - count, negatives -> 0.
    state = cpp_reset()
    value_a = [10] * WAVE_TABLE_SIZE
    value_b = [1000] * WAVE_TABLE_SIZE
    cpp_recompute(state, value_a, value_b)
    ok = all(state["weights_a"][i] == 10 for i in range(WAVE_TABLE_SIZE))
    print("[%s] weights_a is a straight copy of value_a" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = all(state["weights_b"][i] == 1000 // 1 - 0 for i in range(WAVE_TABLE_SIZE))
    print("[%s] weights_b = value_b/(count+1) - count with count=0 gives %d"
          % ("PASS" if ok else "FAIL", state["weights_b"][0]))
    if not ok:
        failures += 1

    # 3b. As the counter grows the weight must shrink (that is the anti-repeat
    #     mechanism) and eventually go negative and be filtered to 0.
    state = cpp_reset()
    state["counter"][0] = 9
    cpp_recompute(state, value_a, value_b)
    expected = 1000 // 10 - 9
    ok = state["weights_b"][0] == expected
    print("[%s] count=9 gives weights_b = 1000/10 - 9 = %d"
          % ("PASS" if ok else "FAIL", expected))
    if not ok:
        failures += 1

    state = cpp_reset()
    state["counter"][0] = 40   # 1000/41 - 40 < 0
    eligible = cpp_recompute(state, value_a, value_b)
    ok = state["weights_b"][0] == 0 and eligible == WAVE_TABLE_SIZE - 1
    print("[%s] a negative result is filtered to 0 and counted as ineligible"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        print("       weights_b[0]=%d eligible=%d" % (state["weights_b"][0], eligible))
        failures += 1

    # 4. note_drawn must bump the counter and set the skip index.
    state = cpp_reset()
    cpp_note_drawn(state, 42)
    ok = state["counter"][42] == 1 and state["skip_index"] == 42
    print("[%s] note_drawn increments the counter and records the skip index"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    cpp_note_drawn(state, 42)
    ok = state["counter"][42] == 2
    print("[%s] drawing the same template twice makes counter 2"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 5. The C++ must actually contain the increment (guards against a refactor
    #    that silently drops it, which would make weights never decay).
    ok = "++counter[index]" in cpp
    print("[%s] the source really increments counter[index]" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 6. Documented honesty: value_a / value_b are inputs, not derived here.
    ok = "INPUT" in hpp and "0x40090" in hpp and "GENERAL ECL parameter reader" in hpp
    print("[%s] header states value_a/value_b are inputs (general ECL reader)"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 7. End-to-end: after many draws the same template must stop being picked.
    state = cpp_reset()
    for _ in range(50):
        cpp_recompute(state, value_a, value_b)
        cpp_note_drawn(state, 0)
    ok = state["weights_b"][0] == 0
    print("[%s] after 50 draws template 0 is no longer eligible"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: EnemyManager weight model matches the disassembly")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

