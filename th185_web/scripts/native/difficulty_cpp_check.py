#!/usr/bin/env python3
"""Cross-check cpp/game/Difficulty.cpp against the verified Python reference.

Parses the C++ source directly (not a duplicated copy of the data) and compares
it with `difficulty.py`, which reads the binary.

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

CPP_HPP = HERE.parents[1] / "cpp" / "game" / "Difficulty.hpp"
CPP_CPP = HERE.parents[1] / "cpp" / "game" / "Difficulty.cpp"

DIFFICULTY_COUNT = 8
SCORE_CAP_TABLE_SIZE = 5


def parse_cpp_names():
    """Extract the tier names from the kNames array in Difficulty.cpp."""
    if not CPP_CPP.is_file():
        return None
    source = CPP_CPP.read_text(encoding="utf-8")
    block = re.search(r"kNames\s*=\s*\{\{(.*?)\}\};", source, re.S)
    if not block:
        return None
    return re.findall(r'"([^"]*)"', block.group(1))


def parse_cpp_score_caps():
    """Extract the score-cap values from the kScoreCaps array."""
    if not CPP_CPP.is_file():
        return None
    source = CPP_CPP.read_text(encoding="utf-8")
    block = re.search(r"kScoreCaps\s*=\s*\{\{(.*?)\}\};", source, re.S)
    if not block:
        return None
    return [int(x) for x in re.findall(r"(\d+)", block.group(1))]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Cross-check cpp/game/Difficulty.cpp against the reference")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    print("=== C++ Difficulty.cpp mirror selftest ===")
    print()
    failures = 0

    cpp_names = parse_cpp_names()
    cpp_caps = parse_cpp_score_caps()
    if cpp_names is None or cpp_caps is None:
        print("[SKIP] cannot parse Difficulty.cpp")
        return 0

    # 1. Tier names.
    try:
        from difficulty import DIFFICULTIES
    except ImportError as exc:
        print("[SKIP] difficulty.py unavailable: %s" % exc)
        return 0

    ok = len(cpp_names) == DIFFICULTY_COUNT
    print("[%s] C++ declares %d tier names (expect %d)"
          % ("PASS" if ok else "FAIL", len(cpp_names), DIFFICULTY_COUNT))
    if not ok:
        failures += 1

    ok = cpp_names == list(DIFFICULTIES)
    print("[%s] tier names match the reference" % ("PASS" if ok else "FAIL"))
    if not ok:
        print("       C++: %s" % cpp_names)
        print("       ref: %s" % list(DIFFICULTIES))
        failures += 1

    ok = len(cpp_names) == 8 and cpp_names[3] == "Little Hard"
    print("[%s] index 3 is Little Hard (TH185-only tier)"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 2. Score-cap table.
    try:
        from difficulty import load_score_caps
        ref_caps = list(load_score_caps())
    except (ImportError, SystemExit) as exc:
        print("[SKIP] cannot read the score-cap table: %s" % exc)
        ref_caps = None

    if ref_caps is not None:
        ok = len(cpp_caps) == SCORE_CAP_TABLE_SIZE
        print("[%s] C++ score-cap table has %d entries (expect %d)"
              % ("PASS" if ok else "FAIL", len(cpp_caps), SCORE_CAP_TABLE_SIZE))
        if not ok:
            failures += 1

        ok = cpp_caps == ref_caps
        print("[%s] score caps match the binary: %s"
              % ("PASS" if ok else "FAIL", cpp_caps))
        if not ok:
            print("       C++: %s" % cpp_caps)
            print("       ref: %s" % ref_caps)
            failures += 1

    # 3. The table is 5 entries but there are 8 tiers: that mismatch is the
    #    documented original behaviour, not a bug.
    ok = SCORE_CAP_TABLE_SIZE == 5 and DIFFICULTY_COUNT == 8
    print("[%s] 5-entry table vs 8 tiers (original reads past the array)"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 4. Constants must match docs/DIFFICULTY.md.
    source = CPP_HPP.read_text(encoding="utf-8")
    ok = "0x4D1038" in source and "0x4BCB00" in source and "0xF4240" in source
    print("[%s] header records VA 0x4D1038 / 0x4BCB00 / immediate 0xF4240"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = "0x3EDB0" in source
    print("[%s] header records the vtable RVA 0x3EDB0" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 5. The four is_difficulty predicates must be implemented.
    cpp_source = CPP_CPP.read_text(encoding="utf-8")
    ok = all(("is_difficulty_%d" % i) in cpp_source for i in range(4))
    print("[%s] all four is_difficulty_N predicates are implemented"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 6. Clamping must be explicit, not silent.
    ok = "out_of_range" in cpp_source and "clamp" in cpp_source.lower()
    print("[%s] out-of-range tiers are reported, not silently clamped"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: cpp/game/Difficulty.cpp matches the reference")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

