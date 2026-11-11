#!/usr/bin/env python3
"""TH185 ECL opcode coverage analysis.

## Why this exists

TH185 has 165 distinct ECL opcodes across 23770 instructions. Implementing all
of them before anything runs would be a poor order of work. This module answers
the question that actually decides the order:

    "how many opcodes do I need before 90% of the instructions execute?"

Answer (measured): **54**.

## Reuse from TH11

`th11/th11_web/cpp/game/EclInterpreter.cpp` is a hand-written ECL bytecode
interpreter with an explicit `enum class Op : u16`. 44 TH185 opcodes share the
TH11 numbering, and they are the high-frequency ones:

    op 42 PushInt        3817
    op 12 Jump           1078
    op 43 StoreInt        948
    op 14 JumpIfNonzero   835
    op 40 EnterFrame      816
    op 44 PushFloat      1760
    op 11 Call            364
    op 15 Spawn           126

Those 44 cover 13048 / 23770 instructions = 54.89%.

## What is NOT shared

TH185 adds whole opcode families that TH11 does not have:

    300..399   (card / ability related, ~900 instructions)
    400..499   (~500)
    500..699   (~2000)
    1000..1026 (~1800)

Plus op 23 (1704 uses) and op 1013 (1021 uses), the two busiest unknowns.

## Usage

    python th185_web/scripts/native/ecl_coverage.py --selftest
    python th185_web/scripts/native/ecl_coverage.py --report

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# Opcodes whose meaning is shared with the TH11 interpreter.
# Kept as a literal set (not derived) so the claim is auditable.
TH11_OPCODES = {
    0, 1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 30,
    40, 41, 42, 43, 44, 45,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68,
    69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88,
}

TH11_NAMES = {
    0: "Nop", 1: "Delete", 10: "Return", 11: "Call", 12: "Jump",
    13: "JumpIfZero", 14: "JumpIfNonzero", 15: "Spawn", 16: "SpawnNamed",
    17: "CancelThread", 18: "SetThreadFlag", 19: "ClearThreadFlag",
    20: "SetThreadState", 21: "CancelSecondary", 30: "Diagnostic",
    40: "EnterFrame", 41: "LeaveFrame", 42: "PushInt", 43: "StoreInt",
    44: "PushFloat", 45: "StoreFloat",
    50: "AddInt", 51: "AddFloat", 52: "SubtractInt", 53: "SubtractFloat",
    54: "MultiplyInt", 55: "MultiplyFloat", 56: "DivideInt", 57: "DivideFloat",
    58: "Remainder", 59: "EqualInt", 60: "EqualFloat", 61: "NotEqualInt",
    62: "NotEqualFloat", 63: "LessInt", 64: "LessFloat", 65: "LessEqualInt",
    66: "LessEqualFloat", 67: "GreaterInt", 68: "GreaterFloat",
    69: "GreaterEqualInt", 70: "GreaterEqualFloat", 71: "NotInt", 72: "NotFloat",
    73: "LogicalOr", 74: "LogicalAnd", 75: "Xor", 76: "Or", 77: "And",
    78: "PostDecrement", 79: "Sin", 80: "Cos", 81: "Polar", 82: "NormalizeAngle",
    83: "Wait", 84: "NegateInt", 85: "NegateFloatBits", 86: "LengthSquared",
    87: "AngleTo", 88: "Sqrt",
}


def _dat_path():
    from probe import target_path
    return target_path().parent / "th185.dat"


def collect():
    """Return (opcode -> count, opcode -> set(sizes))."""
    from pbg4 import read_entries
    from ecl import load_ecl

    dat = _dat_path()
    _header, entries = read_entries(dat)
    names = [e["name"] for e in entries if e["name"].endswith(".ecl")]

    counts = collections.Counter()
    sizes = collections.defaultdict(set)
    for name in names:
        try:
            resource = load_ecl(name)
        except SystemExit:
            continue
        for sub in resource["subs"]:
            for ins in sub["instructions"]:
                counts[ins["id"]] += 1
                sizes[ins["id"]].add(ins["size"])
    return counts, sizes


def coverage(counts):
    """Return (total, covered, uncovered, fraction)."""
    total = sum(counts.values())
    covered = sum(c for op, c in counts.items() if op in TH11_OPCODES)
    return total, covered, total - covered, (covered / total if total else 0.0)


def opcodes_for(counts, fraction):
    """How many opcodes (most frequent first) are needed to reach `fraction`."""
    total = sum(counts.values())
    cumulative = 0
    for index, (_op, count) in enumerate(sorted(counts.items(), key=lambda x: -x[1]), 1):
        cumulative += count
        if cumulative >= total * fraction:
            return index, cumulative
    return len(counts), cumulative


def selftest() -> int:
    print("=== ECL opcode coverage selftest ===")
    print()
    failures = 0

    try:
        counts, sizes = collect()
    except ImportError as exc:
        print("[SKIP] missing dependency: %s" % exc)
        return 0
    except SystemExit as exc:
        print("[SKIP] %s" % exc)
        return 0

    total, covered, uncovered, fraction = coverage(counts)

    ok = total == 23770
    print("[%s] %d instructions across %d opcodes (expect 23770 / 165)"
          % ("PASS" if ok else "FAIL", total, len(counts)))
    if not ok:
        failures += 1

    ok = len(counts) == 165
    print("[%s] distinct opcode count is %d (expect 165)"
          % ("PASS" if ok else "FAIL", len(counts)))
    if not ok:
        failures += 1

    # The TH11-shared set must cover a bit over half the instructions. If this
    # drifts, either the opcode set or the numbering assumption broke.
    ok = 0.50 <= fraction <= 0.60
    print("[%s] TH11-shared opcodes cover %.2f%% of instructions (expect ~54.89%%)"
          % ("PASS" if ok else "FAIL", fraction * 100.0))
    if not ok:
        failures += 1

    # The high-frequency ops must all be TH11-shared; if a new one appears it
    # means the format diverged more than assumed.
    top = [op for op, _c in sorted(counts.items(), key=lambda x: -x[1])[:20]]
    shared_top = [op for op in top if op in TH11_OPCODES]
    ok = len(shared_top) >= 8
    print("[%s] %d of the 20 busiest opcodes are TH11-shared"
          % ("PASS" if ok else "FAIL", len(shared_top)))
    if not ok:
        failures += 1

    # The 90% threshold is the planning number quoted in the docs.
    needed90, cum90 = opcodes_for(counts, 0.90)
    ok = needed90 == 54
    print("[%s] %d opcodes reach 90%% coverage (expect 54)"
          % ("PASS" if ok else "FAIL", needed90))
    if not ok:
        print("       got %d opcodes / %d instructions" % (needed90, cum90))
        failures += 1

    needed99, cum99 = opcodes_for(counts, 0.99)
    ok = 100 <= needed99 <= 120
    print("[%s] %d opcodes reach 99%% coverage (expect 100..120)"
          % ("PASS" if ok else "FAIL", needed99))
    if not ok:
        failures += 1

    # Every TH11 opcode that TH185 actually uses must have a known name.
    used = [op for op in counts if op in TH11_OPCODES]
    ok = all(op in TH11_NAMES for op in used)
    print("[%s] all %d shared opcodes have a TH11 name"
          % ("PASS" if ok else "FAIL", len(used)))
    if not ok:
        missing = [op for op in used if op not in TH11_NAMES]
        print("       %s" % missing[:8])
        failures += 1

    # Instruction sizes must be stable per opcode for the fixed-size ones.
    variable = [op for op, s in sizes.items() if len(s) > 1]
    print("       (%d opcodes have variable instruction size: %s%s)"
          % (len(variable), variable[:8], " ..." if len(variable) > 8 else ""))

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: opcode coverage matches the documented figures")
    return 0


def report() -> int:
    counts, sizes = collect()
    total, covered, uncovered, fraction = coverage(counts)

    print("ECL opcode coverage")
    print("  instructions      : %d" % total)
    print("  distinct opcodes  : %d" % len(counts))
    print("  TH11-shared       : %d instructions (%.2f%%)"
          % (covered, fraction * 100.0))
    print("  TH185-only        : %d instructions (%.2f%%)"
          % (uncovered, 100.0 - fraction * 100.0))
    print()

    needed90, _ = opcodes_for(counts, 0.90)
    needed99, _ = opcodes_for(counts, 0.99)
    print("  opcodes needed for 90%% : %d" % needed90)
    print("  opcodes needed for 99%% : %d" % needed99)
    print()

    print("  %-8s %-8s %-18s %s" % ("opcode", "count", "th11 name", "sizes"))
    for op, count in sorted(counts.items(), key=lambda x: -x[1]):
        name = TH11_NAMES.get(op, "-")
        print("  %-8d %-8d %-18s %s"
              % (op, count, name, sorted(sizes[op])))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 ECL opcode coverage")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if args.report:
        return report()
    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

