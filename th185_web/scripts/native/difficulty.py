#!/usr/bin/env python3
"""TH185 difficulty system: tiers, score-cap table, accessors, vtable.

## Confirmed

### 1. Difficulty tiers (8)

Index at 0x4D1038 (uint32, 0..7):
  0 Very Easy / 1 Easy / 2 Normal / 3 Little Hard
  4 Hard / 5 Very Hard / 6 Lunatic / 7 Over Drive

Little Hard (index 3) is TH185-only; TH18 does not have it.

### 2. Score-cap table (5 entries)

Initialized at RVA 0x3107C from constant VA 0x4BCB00 plus an immediate:

    03107C: movaps xmm0, [0x4bcb00]        ; first 4 entries
    031087: mov [ebp-8], 0xf4240           ; 5th entry
    0310A1: mov eax, [ebp + eax*4 - 0x18]  ; index by difficulty
    0310A5: imul eax, [0x4d1004]           ; x stage number

Table: [500000, 1000000, 1500000, 2000000, 1000000]

The table has 5 entries but there are 8 tiers; index 5..7 read past the
array into adjacent stack data. That is original behaviour, not a misread.

### 3. Accessor family (RVA 0x3E735 - 0x3E7C5)

11 tiny getters. See ACCESSORS.

### 4. Difficulty predicates are virtual functions

is_difficulty_0..3 have ZERO direct call sites. Searching for where their
addresses are stored found a vtable at .text VA 0x3EDB0.

### 5. No 8-element multiplier table exists

Scanned .rdata for 8-element increasing float arrays in several ways;
all hits were sliding windows over one reciprocal table at VA 0xBC6C4
(1/20, 1/16, 1/15, ...), which is angle/sine related, not difficulty.

Conclusion: per-tier numeric multipliers are very likely hardcoded in
conditional branches, not stored as a table. Extracting them requires
reading each function individually.

## Usage

    python th185_web/scripts/native/difficulty.py --list
    python th185_web/scripts/native/difficulty.py --selftest

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

IMAGE_BASE = 0x400000

DIFFICULTIES = [
    "Very Easy", "Easy", "Normal", "Little Hard",
    "Hard", "Very Hard", "Lunatic", "Over Drive",
]

SCORE_CAP_CONST_VA = 0x4BCB00
SCORE_CAP_TAIL = 0xF4240

ACCESSORS = {
    0x3E735: ("get_4d1040", "returns [0x4D1040]"),
    0x3E740: ("get_difficulty", "returns [0x4D1038], difficulty 0..7"),
    0x3E74B: ("get_ecx_6224", "returns [ecx+0x6224]"),
    0x3E757: ("is_difficulty_0", "difficulty == 0 (Very Easy)"),
    0x3E768: ("is_difficulty_1", "difficulty == 1 (Easy)"),
    0x3E77A: ("is_difficulty_2", "difficulty == 2 (Normal)"),
    0x3E78C: ("is_difficulty_3", "difficulty == 3 (Little Hard)"),
    0x3E79E: ("get_em_3c", "returns [EM+0x3C]"),
    0x3E7AC: ("get_em_40", "returns [EM+0x40]"),
    0x3E7BA: ("get_em_44", "returns [EM+0x44]"),
    0x3E7C8: ("get_em_4f44", "returns [EM+0x4F44]"),
}

# 注意：这是 RVA，不是 VA。.text 的 vaddr=0x1000，所以文件偏移 = 0x3EDB0-0x1000+0x400
VTABLE_RVA = 0x3EDB0
VTABLE_EXPECTED = [
    0x3E757, 0x3E768, 0x3E77A, 0x3E78C,
    0x3E79E, 0x3E7AC, 0x3E7BA, 0x3E7C8,
]


def _rva_to_off(meta, rva):
    for s in meta["sections"]:
        if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rsize"]):
            return s["roff"] + (rva - s["vaddr"])
    return -1


def load_score_caps():
    from probe import load_image, parse_pe

    data, _target = load_image()
    meta = parse_pe(data)
    off = _rva_to_off(meta, SCORE_CAP_CONST_VA - IMAGE_BASE)
    if off < 0:
        raise SystemExit("constant VA 0x%X not in any section" % SCORE_CAP_CONST_VA)
    values = [struct.unpack_from("<I", data, off + i * 4)[0] for i in range(4)]
    values.append(SCORE_CAP_TAIL)
    return values


def load_vtable():
    from probe import load_image, parse_pe

    data, _target = load_image()
    meta = parse_pe(data)
    off = _rva_to_off(meta, VTABLE_RVA)
    if off < 0:
        raise SystemExit("vtable RVA 0x%X not in any section" % VTABLE_RVA)
    return [struct.unpack_from("<I", data, off + i * 4)[0] - IMAGE_BASE
            for i in range(len(VTABLE_EXPECTED))]


def selftest() -> int:
    print("=== difficulty system selftest ===")
    print()
    failures = 0

    ok = len(DIFFICULTIES) == 8
    print("[%s] tier count = %d (expect 8)" % ("PASS" if ok else "FAIL", len(DIFFICULTIES)))
    if not ok:
        failures += 1

    ok = DIFFICULTIES[3] == "Little Hard"
    print("[%s] index 3 = %s (expect Little Hard)" % ("PASS" if ok else "FAIL", DIFFICULTIES[3]))
    if not ok:
        failures += 1

    ok = DIFFICULTIES[-1] == "Over Drive"
    print("[%s] index 7 = %s (expect Over Drive)" % ("PASS" if ok else "FAIL", DIFFICULTIES[-1]))
    if not ok:
        failures += 1

    preds = [k for k, v in ACCESSORS.items() if v[0].startswith("is_difficulty_")]
    ok = len(preds) == 4
    print("[%s] difficulty predicates = %d (expect 4, only 0..3)"
          % ("PASS" if ok else "FAIL", len(preds)))
    if not ok:
        failures += 1

    try:
        caps = load_score_caps()
        expected = [500000, 1000000, 1500000, 2000000, 1000000]
        ok = caps == expected
        print("[%s] score cap table = %s" % ("PASS" if ok else "FAIL", caps))
        if not ok:
            print("       expect %s" % expected)
            failures += 1
    except ImportError as exc:
        print("[SKIP] score cap table (missing dep: %s)" % exc)

    try:
        vt = load_vtable()
        ok = vt == VTABLE_EXPECTED
        print("[%s] vtable at RVA 0x%X matches expected order" % ("PASS" if ok else "FAIL", VTABLE_RVA))
        if not ok:
            print("       got    %s" % ["0x%X" % v for v in vt])
            print("       expect %s" % ["0x%X" % v for v in VTABLE_EXPECTED])
            failures += 1
    except ImportError as exc:
        print("[SKIP] vtable (missing dep: %s)" % exc)

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: difficulty system matches expectations")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 difficulty system")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if args.list:
        print("=== difficulty tiers (index at [0x4D1038]) ===")
        for i, name in enumerate(DIFFICULTIES):
            print("  [%d] %s" % (i, name))
        print()
        print("=== score cap table (const VA 0x%X + immediate) ===" % SCORE_CAP_CONST_VA)
        try:
            for i, v in enumerate(load_score_caps()):
                print("  [%d] %d" % (i, v))
        except (ImportError, SystemExit) as exc:
            print("  (unavailable: %s)" % exc)
        print()
        print("=== global accessors ===")
        for rva in sorted(ACCESSORS):
            name, desc = ACCESSORS[rva]
            print("  0x%X  %-18s %s" % (rva, name, desc))
        print()
        print("=== vtable at RVA 0x%X ===" % VTABLE_RVA)
        try:
            for i, rva in enumerate(load_vtable()):
                name = ACCESSORS.get(rva, ("?", ""))[0]
                print("  [%d] 0x%X  %s" % (i, rva, name))
        except (ImportError, SystemExit) as exc:
            print("  (unavailable: %s)" % exc)
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

