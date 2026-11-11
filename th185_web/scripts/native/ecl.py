#!/usr/bin/env python3
"""TH185 ECL (SCPT) script parser.

## Format

The 114 `.ecl` files use the "SCPT" container also used by TH18:

    +0x00  char[4] magic = "SCPT"
    +0x04  uint32  version   (observed 0x00100001)
    +0x08  uint32  name_count    (36 in default.ecl)
    +0x0C  uint32  reserved
    +0x10  uint32  sub_count     (34 in default.ecl)
    +0x14  uint32  reserved
    +0x18  uint32  reserved
    +0x1C  uint32  reserved
    +0x20  uint32  reserved
    +0x24  char[4] "ANIM"      // animation sub-section tag
    +0x28  uint32  reserved
    +0x2C  char[4] "ECLI"      // instruction sub-section tag
    +0x30  uint32  reserved
    +0x34  uint32  sub_offsets[sub_count]
    ...    char[]  name table  (name_count NUL-terminated strings)

Each `sub_offsets[i]` points at an `ECLH` block:

    +0x00  char[4] magic = "ECLH"
    +0x04  uint32  header_size = 16
    +0x08  uint32  0
    +0x0C  uint32  0
    +0x10  uint32  0
    +0x14  instructions[]     // 20 bytes after the ECLH tag

Instruction encoding (same shape as .std commands):

    +0x00  uint16  id
    +0x02  uint16  size       // total length including this header
    +0x04  uint16  rank/flags
    +0x06  uint16  param_count
    +0x08  uint32  payload[]

## Verification

All 34 ECLH blocks in default.ecl parse cleanly and every single one ends
exactly 12 bytes before the next block starts. 544 instructions total,
81 distinct opcodes. The 12-byte tail is a fixed per-subroutine trailer.

Subroutine names are plain ASCII and match the TH18 naming conventions
(BossDead, BossEscape, BossInterval, BossItem, DeadAttack1, DebugSkipFunc,
Ecl_EtBreak, ItemCarryLily, MainBossSpell, WaveSync, ...).

## Usage

    python th185_web/scripts/native/ecl.py --selftest
    python th185_web/scripts/native/ecl.py --list
    python th185_web/scripts/native/ecl.py --file default.ecl

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

MAGIC = b"SCPT"
SUBTAG = b"ECLH"
ECLH_HEADER = 20        # ECLH tag + 16 bytes of header fields
TRAILER = 12            # fixed tail after the last instruction in a block


_PROBE_CACHE = []


def _dat_path():
    from probe import target_path
    return target_path().parent / "th185.dat"


def _shared_probe():
    """Reuse a single Unicorn instance across extractions.

    Creating a Probe maps a 2 MB image and installs 174 import stubs, which
    dominates the runtime when extracting many files. The parser itself is
    stateless, so sharing one instance is safe here.
    """
    if not _PROBE_CACHE:
        from probe import Probe, load_image, parse_pe
        data, _t = load_image()
        _PROBE_CACHE.append(Probe(data, parse_pe(data)))
    return _PROBE_CACHE[0]


def parse_ecl(blob):
    """Parse one SCPT blob."""
    if len(blob) < 0x38 or blob[:4] != MAGIC:
        raise SystemExit("not an SCPT file (magic=%r)" % blob[:4])

    version, name_count, _r1, sub_count = struct.unpack_from("<IIII", blob, 0x04)

    sub_offsets = []
    for i in range(sub_count):
        pos = 0x34 + i * 4
        if pos + 4 > len(blob):
            break
        off = struct.unpack_from("<I", blob, pos)[0]
        if off + 4 > len(blob) or blob[off:off + 4] != SUBTAG:
            break
        sub_offsets.append(off)

    # Name table starts right after the offset table.
    names = []
    pos = 0x34 + sub_count * 4
    while pos < len(blob) and len(names) < name_count:
        start = pos
        while pos < len(blob) and blob[pos] != 0:
            pos += 1
        if pos >= len(blob):
            break
        raw = blob[start:pos]
        pos += 1
        if raw:
            names.append(raw.decode("ascii", "replace"))

    # Parse each ECLH block.
    subs = []
    bounds = sub_offsets + [len(blob)]
    for i, off in enumerate(sub_offsets):
        end = bounds[i + 1]
        pos = off + ECLH_HEADER
        instrs = []
        while pos + 4 <= end:
            opcode, size = struct.unpack_from("<HH", blob, pos)
            if size < 4 or pos + size > end:
                break
            rank, argc = struct.unpack_from("<HH", blob, pos + 4) if size >= 8 else (0, 0)
            instrs.append({
                "offset": pos,
                "id": opcode,
                "size": size,
                "rank": rank,
                "argc": argc,
                "payload": blob[pos + 8:pos + size],
            })
            pos += size
        subs.append({
            "index": i,
            "offset": off,
            "name": names[i] if i < len(names) else "",
            "end": pos,
            "tail": end - pos,
            "instructions": instrs,
        })

    return {
        "version": version,
        "name_count": name_count,
        "sub_count": sub_count,
        "names": names,
        "subs": subs,
    }


def load_ecl(name):
    from pbg4 import extract_file

    dat = _dat_path()
    filename = name if name.endswith(".ecl") else name + ".ecl"
    return parse_ecl(
        extract_file(dat, name=filename, probe=_shared_probe())["data"]
    )


def selftest() -> int:
    print("=== ECL (SCPT) parser selftest ===")
    print()
    failures = 0

    try:
        dat = _dat_path()
        if not dat.is_file():
            print("[SKIP] th185.dat not found")
            return 0
        from pbg4 import read_entries
        _h, entries = read_entries(dat)
    except ImportError as exc:
        print("[SKIP] missing dependency: %s" % exc)
        return 0

    ecls = [e["name"] for e in entries if e["name"].endswith(".ecl")]
    ok = len(ecls) == 114
    print("[%s] found %d .ecl files (expect 114)" % ("PASS" if ok else "FAIL", len(ecls)))
    if not ok:
        failures += 1

    try:
        p = load_ecl("default.ecl")
    except SystemExit as exc:
        print("[FAIL] parse error: %s" % exc)
        return 1

    ok = p["sub_count"] == 34 and len(p["subs"]) == 34
    print("[%s] default.ecl has %d subroutines (declared %d)"
          % ("PASS" if ok else "FAIL", len(p["subs"]), p["sub_count"]))
    if not ok:
        failures += 1

    ok = all(s["name"] for s in p["subs"])
    print("[%s] every subroutine has a name" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    tails = [s["tail"] for s in p["subs"]]
    ok = all(t == TRAILER for t in tails)
    print("[%s] every subroutine ends with exactly %d bytes left"
          % ("PASS" if ok else "FAIL", TRAILER))
    if not ok:
        print("       tails: %s" % tails)
        failures += 1

    total = sum(len(s["instructions"]) for s in p["subs"])
    ok = total > 500
    print("[%s] parsed %d instructions in default.ecl"
          % ("PASS" if ok else "FAIL", total))
    if not ok:
        failures += 1

    opcodes = sorted({ins["id"] for s in p["subs"] for ins in s["instructions"]})
    ok = len(opcodes) >= 50
    print("[%s] observed %d distinct opcodes" % ("PASS" if ok else "FAIL", len(opcodes)))
    if not ok:
        failures += 1

    # Spot-check a few well-known TH18-style subroutine names.
    names = {s["name"] for s in p["subs"]}
    expected = {"BossDead", "BossEscape", "WaveSync", "DebugSkipFunc"}
    ok = expected <= names
    print("[%s] TH18-style subroutine names present (found %s)"
          % ("PASS" if ok else "FAIL", sorted(expected & names)))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: SCPT/ECLH structure fully parsed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 ECL (SCPT) parser")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--file")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    try:
        dat = _dat_path()
        from pbg4 import read_entries
        _h, entries = read_entries(dat)
    except ImportError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2

    if args.list:
        ecls = [e for e in entries if e["name"].endswith(".ecl")]
        print("%-24s %10s %8s %8s" % ("file", "size", "subs", "instrs"))
        for e in ecls:
            try:
                p = load_ecl(e["name"])
                n = sum(len(s["instructions"]) for s in p["subs"])
                print("%-24s %10d %8d %8d" % (e["name"], e["size"], len(p["subs"]), n))
            except SystemExit:
                print("%-24s %10d %8s %8s" % (e["name"], e["size"], "-", "-"))
        return 0

    if args.file:
        p = load_ecl(args.file)
        print("file       : %s" % args.file)
        print("version    : 0x%08X" % p["version"])
        print("name count : %d" % p["name_count"])
        print("sub count  : %d" % p["sub_count"])
        print()
        for s in p["subs"]:
            print("  [%2d] %-24s @0x%04X  %3d instrs, tail=%d"
                  % (s["index"], s["name"], s["offset"],
                     len(s["instructions"]), s["tail"]))
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

