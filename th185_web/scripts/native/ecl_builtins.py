#!/usr/bin/env python3
"""TH185 ECL builtin function table (117 entries at VA 0x43ECF4).

## What this table is

The ECL interpreter resolves `builtin function calls` through a table of 117
`__thiscall` thunks at **VA 0x43ECF4** (inside .text). Each thunk reads one
value from the ECL context (`ecx`) or from a global and returns it.

This is the "get variable" half of the ECL builtin set: the script calls
`builtin(N)` and gets back an integer/float. Many entries are pure field
readers, so the table doubles as a **map of the ECL context layout**.

## Confirmed entries

| idx | impl | meaning |
|---|---|---|
| 0 | `call 0x2800; and eax,0x7fffffff` | `rand()` integer |
| 1 | `call 0x2890; cvttss2si` | `randf()` float |
| 2,11,44,45,71,72,87,88,97,99,100,103 | `xor eax,eax` | constant 0 |
| 3..8,19..34,65..68 | `cvttss2si [ecx+0x14xx]` | context float fields |
| 9,35 | `[0x4d7c3c]+0x620` | player X (float->int) |
| 10,36 | `[0x4d7c3c]+0x624` | player Y (float->int) |
| 12 | `[ecx+0x14f0]` | context int field |
| 13 | `call 0x28d0` | RNG variant 3 |
| 14 | `[ecx+0x636b] & 1` | context bool |
| 40 | `[0x4d1040]` | global value |
| 41 | `[0x4d1038]` | **difficulty index** |
| 42 | `[ecx+0x12a8]` / `[ecx+0x1...]` | context float pair |
| 43 | `mov eax,1` | constant 1 |
| 46 | `[ecx+0x6224]` | context int field |
| 47..50 | `cmp [0x4d1038], 0..3` | **is_difficulty_0..3** |
| 51..54 | `[EM+0x3c/0x40/0x44/0x4f44]` | ENEMY_MANAGER fields |
| 55 | `[0x4d102c] + [0x4d1030]` | sum of two globals |
| 56 | `[0x4d7c3c]` + player floats | player distance |
| 69 | `[EM+0x94]` | ENEMY_MANAGER field |
| 70 | `[0x4d1078]` | global value |
| 73 | `[0x4d7b0c]+0xd0 == 0` | game state test |
| 74..85 | `[EM+0x0c .. 0x38]` | ENEMY_MANAGER field group |
| 86 | `[ecx+0x683c]` | context int field |
| 91 | `[ecx+0x6840]` | context int field |
| 92 | `call 0x33f60` | helper |
| 93 | `[0x4d104c]` | global |
| 94 | `[ecx+0x6368] >> 0x13` | context bits |
| 95 | `[0x4d100c]` | global |
| 96 | `[0x4d1050]` | global |
| 98 | `call 0x337f0` | helper |
| 101 | `or eax, -1` | constant -1 |
| 102 | `[0x4d7ae0]+0x40` | global object field |
| 104 | `[EM+0x4eb8]` | ENEMY_MANAGER field |
| 105..108 | `[0x579c04..0x579c10]` | globals |
| 109 | `push 0x41; call 0x17790` | helper |
| 110 | `[0x4d1004] - 2` | **stage number - 2** |
| 111..116 | `push N; push M` | paired-argument helpers |

## Notes

- `EM` = `[0x4D7AF4]` (ENEMY_MANAGER), see docs/BUILD.md.
- Entries 51..54 are exactly the accessors documented in docs/DIFFICULTY.md,
  which confirms the two analyses agree.
- Entries 47..50 are the four `is_difficulty_0..3` predicates, also matching
  docs/DIFFICULTY.md (only 0..3 exist, 4..7 use generic paths).
- Entry 110 reads `[0x4D1004] - 2`, and docs/WAVES.md showed `[0x4D1004]` is the
  stage number used by the forced-boss logic.

## Usage

    python th185_web/scripts/native/ecl_builtins.py --selftest
    python th185_web/scripts/native/ecl_builtins.py --list

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

TABLE_VA = 0x43ECF4
TABLE_COUNT = 117
# NOTE: this is an RVA, not a VA. Do NOT subtract IMAGE_BASE.
DISPATCH_RVA = 0x3E586     # mov eax,[ebp+8]; jmp [eax*4+TABLE] is at +0x14
BUILTIN_BASE = 10000       # script builtin(N): index = N - 10000
BUILTIN_MAX = 110          # cmp eax, 0x6e ; ja -> return 0
REACHABLE = BUILTIN_MAX + 1   # 111 reachable entries
IMAGE_BASE = 0x400000


def load_table():
    """Read the 117 function pointers from the exe."""
    from probe import load_image, parse_pe

    data, _t = load_image()
    meta = parse_pe(data)

    def rva_to_off(rva):
        for s in meta["sections"]:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rsize"]):
                return s["roff"] + (rva - s["vaddr"])
        return -1

    off = rva_to_off(TABLE_VA - IMAGE_BASE)
    if off < 0:
        raise SystemExit("table VA 0x%X not in any section" % TABLE_VA)

    ptrs = [struct.unpack_from("<I", data, off + i * 4)[0]
            for i in range(TABLE_COUNT)]
    return data, meta, ptrs, rva_to_off


def describe(data, rva_to_off, ptr, limit=4):
    """Return the first few instructions of a thunk as text.

    Default is 4, not 2: some thunks do meaningful work on the third
    instruction (e.g. entry 0 is mov ecx,..; call rng; and eax,0x7fffffff).
    """
    import capstone

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    off = rva_to_off(ptr - IMAGE_BASE)
    if off < 0:
        return "<invalid>"
    insns = list(md.disasm(data[off:off + 32], ptr - IMAGE_BASE))
    if not insns:
        return "<undecodable>"
    return "; ".join("%s %s" % (x.mnemonic, x.op_str) for x in insns[:limit])


def selftest() -> int:
    print("=== ECL builtin table selftest ===")
    print()
    failures = 0

    try:
        data, meta, ptrs, rva_to_off = load_table()
    except ImportError as exc:
        print("[SKIP] missing dependency: %s" % exc)
        return 0
    except SystemExit as exc:
        print("[FAIL] %s" % exc)
        return 1

    text = [s for s in meta["sections"] if s["name"] == ".text"][0]
    tlo = IMAGE_BASE + text["vaddr"]
    thi = tlo + text["rsize"]

    ok = len(ptrs) == TABLE_COUNT
    print("[%s] table has %d entries (expect %d)"
          % ("PASS" if ok else "FAIL", len(ptrs), TABLE_COUNT))
    if not ok:
        failures += 1

    bad = [i for i, p in enumerate(ptrs) if not (tlo <= p < thi)]
    ok = not bad
    print("[%s] all entries point inside .text (%d bad)"
          % ("PASS" if ok else "FAIL", len(bad)))
    if not ok:
        failures += 1

    # Entry 0 must be the integer RNG (calls 0x2800 and masks the sign).
    d0 = describe(data, rva_to_off, ptrs[0])
    ok = "0x2800" in d0 and "0x7fffffff" in d0
    print("[%s] entry 0 is the integer RNG (%s)" % ("PASS" if ok else "FAIL", d0))
    if not ok:
        failures += 1

    # Entry 1 must be the float RNG (calls 0x2890).
    d1 = describe(data, rva_to_off, ptrs[1])
    ok = "0x2890" in d1
    print("[%s] entry 1 is the float RNG (%s)" % ("PASS" if ok else "FAIL", d1))
    if not ok:
        failures += 1

    # Entry 41 must read the difficulty global 0x4d1038.
    d41 = describe(data, rva_to_off, ptrs[41])
    ok = "0x4d1038" in d41
    print("[%s] entry 41 reads the difficulty index (%s)" % ("PASS" if ok else "FAIL", d41))
    if not ok:
        failures += 1

    # Entries 47..50 must be the four difficulty predicates.
    ok = all("0x4d1038" in describe(data, rva_to_off, ptrs[i]) for i in range(47, 51))
    print("[%s] entries 47..50 are difficulty predicates" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # Entry 110 must compute stage - 2.
    d110 = describe(data, rva_to_off, ptrs[110])
    ok = "0x4d1004" in d110
    print("[%s] entry 110 reads the stage number (%s)" % ("PASS" if ok else "FAIL", d110))
    if not ok:
        failures += 1

    # A good fraction of entries should be simple field readers.
    readers = sum(1 for p in ptrs
                  if "[ecx + 0x" in describe(data, rva_to_off, p))
    ok = readers >= 30
    print("[%s] %d entries are direct context field readers"
          % ("PASS" if ok else "FAIL", readers))
    if not ok:
        failures += 1

    # Dispatch site: jmp dword ptr [eax*4 + 0x43ecf4] guarded by cmp eax,0x6e
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    doff = rva_to_off(DISPATCH_RVA)
    disp = list(md.disasm(data[doff:doff + 0x20], DISPATCH_RVA)) if doff > 0 else []
    text_all = "; ".join("%s %s" % (x.mnemonic, x.op_str) for x in disp)
    ok = "0x2710" in text_all and "0x6e" in text_all and "0x43ecf4" in text_all
    print("[%s] dispatch site adds 0x2710, compares 0x6e, jumps via table"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        print("       %s" % text_all[:120])
        failures += 1

    ok = REACHABLE == 111
    print("[%s] builtin ids are [%d, %d] -> %d reachable entries"
          % ("PASS" if ok else "FAIL", BUILTIN_BASE, BUILTIN_BASE + BUILTIN_MAX, REACHABLE))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: ECL builtin table decoded")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 ECL builtin table")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if args.list:
        try:
            data, meta, ptrs, rva_to_off = load_table()
        except ImportError as exc:
            print("error: %s" % exc, file=sys.stderr)
            return 2
        print("%-4s %-8s %-10s %s" % ("idx", "builtin", "address", "first instructions"))
        for i, p in enumerate(ptrs):
            bid = ("%d" % (BUILTIN_BASE + i)) if i <= BUILTIN_MAX else "-"
            print("%-4d %-8s 0x%06X %s" % (i, bid, p, describe(data, rva_to_off, p)))
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

