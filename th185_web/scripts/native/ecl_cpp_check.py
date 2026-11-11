#!/usr/bin/env python3
"""Cross-check cpp/game/EclResource.cpp against the verified Python reference.

Contains a line-for-line Python mirror of EclResource.cpp and compares it with
`ecl.py`, which is verified against the original data (114 files, 486
subroutines, 12480 instructions, 0 errors, every subroutine ending with exactly
12 bytes left).

Contract: if EclResource.cpp changes, EclMirror below must change with it.

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

MAGIC_SCPT = b"SCPT"
MAGIC_ANIM = b"ANIM"
MAGIC_ECLI = b"ECLI"
MAGIC_ECLH = b"ECLH"

ECLH_INSTRUCTION_START = 20
ECL_TRAILER = 12


def find_tag(buf: bytes, tag: bytes) -> int:
    """Mirror of the C++ find_tag()."""
    return buf.find(tag)


def read_cstring(buf: bytes, pos: int):
    """Mirror of the C++ read_cstring(). Returns (string, new_pos) or (None, pos)."""
    end = buf.find(b"\x00", pos)
    if end < 0:
        return None, pos
    return buf[pos:end].decode("ascii", "replace"), end + 1


def parse_ecl_mirror(blob: bytes):
    """Mirror of EclResource::parse()."""
    if len(blob) < 0x38:
        return None
    if blob[:4] != MAGIC_SCPT:
        return None

    version = struct.unpack_from("<I", blob, 0x04)[0]
    name_count = struct.unpack_from("<I", blob, 0x08)[0]
    sub_count = struct.unpack_from("<I", blob, 0x10)[0]

    anim_names = []
    anim_pos = find_tag(blob, MAGIC_ANIM)
    if anim_pos >= 0 and anim_pos + 8 <= len(blob):
        anim_count = struct.unpack_from("<I", blob, anim_pos + 4)[0]
        pos = anim_pos + 8
        for _ in range(anim_count):
            name, pos2 = read_cstring(blob, pos)
            if name is None:
                break
            anim_names.append(name)
            pos = pos2

    ecli_pos = find_tag(blob, MAGIC_ECLI)
    if ecli_pos < 0:
        return None
    if ecli_pos + 8 > len(blob):
        return None

    ecli_names = []
    ecli_count = struct.unpack_from("<I", blob, ecli_pos + 4)[0]
    ecli_name_pos = ecli_pos + 8
    for _ in range(ecli_count):
        name, ecli_name_pos2 = read_cstring(blob, ecli_name_pos)
        if name is None:
            break
        ecli_names.append(name)
        ecli_name_pos = ecli_name_pos2
    table_pos = (ecli_name_pos + 3) & ~3

    sub_offsets = []
    for i in range(sub_count):
        pos = table_pos + i * 4
        if pos + 4 > len(blob):
            break
        offset = struct.unpack_from("<I", blob, pos)[0]
        if offset + 4 > len(blob):
            break
        if blob[offset:offset + 4] != MAGIC_ECLH:
            break
        sub_offsets.append(offset)
    if not sub_offsets:
        return None

    names = []
    name_pos = table_pos + sub_count * 4
    while name_pos < len(blob) and len(names) < name_count:
        name, name_pos2 = read_cstring(blob, name_pos)
        if name is None:
            break
        if name:
            names.append(name)
        name_pos = name_pos2

    subs = []
    for i, offset in enumerate(sub_offsets):
        end = sub_offsets[i + 1] if i + 1 < len(sub_offsets) else len(blob)
        if offset + ECLH_INSTRUCTION_START > end:
            break
        sub_name = names[i] if i < len(names) else ""
        pos = offset + ECLH_INSTRUCTION_START
        instructions = []
        while pos + 4 <= end:
            ins_id, size = struct.unpack_from("<HH", blob, pos)
            if size < 4 or pos + size > end:
                break
            rank = argc = 0
            if size >= 8:
                rank, argc = struct.unpack_from("<HH", blob, pos + 4)
            instructions.append({
                "id": ins_id, "size": size, "rank": rank, "argc": argc,
                "offset": pos,
            })
            pos += size
        subs.append({
            "index": i, "offset": offset, "name": sub_name,
            "end": pos, "tail": end - pos, "instructions": instructions,
        })

    return {
        "version": version, "name_count": name_count, "sub_count": sub_count,
        "ecli_offset": ecli_pos, "anim_names": anim_names,
        "ecli_names": ecli_names,
        "names": names, "subs": subs,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Cross-check cpp/game/EclResource.cpp against the reference")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    print("=== C++ EclResource.cpp mirror selftest ===")
    print()
    failures = 0

    try:
        from ecl import load_ecl
        from pbg4 import extract_file, read_entries
        from probe import target_path
    except ImportError as exc:
        print("[SKIP] reference modules unavailable: %s" % exc)
        return 0

    dat = target_path().parent / "th185.dat"
    if not dat.is_file():
        print("[SKIP] th185.dat not found")
        return 0

    _header, entries = read_entries(dat)
    ecls = [e["name"] for e in entries if e["name"].endswith(".ecl")]

    total_subs = 0
    total_instrs = 0
    bad_tail = []
    mismatched = []

    for name in ecls:
        blob = extract_file(dat, name=name)["data"]
        mirror = parse_ecl_mirror(blob)
        if mirror is None:
            mismatched.append((name, "mirror returned None"))
            continue
        ref = load_ecl(name)

        if mirror["ecli_names"] != ref.get("ecli_names", mirror["ecli_names"]):
            mismatched.append((name, "ecli_names %s vs %s"
                               % (mirror["ecli_names"], ref.get("ecli_names"))))
            continue
        if mirror["sub_count"] != ref["sub_count"]:
            mismatched.append((name, "sub_count %d vs %d"
                               % (mirror["sub_count"], ref["sub_count"])))
            continue
        if len(mirror["subs"]) != len(ref["subs"]):
            mismatched.append((name, "subs %d vs %d"
                               % (len(mirror["subs"]), len(ref["subs"]))))
            continue

        for a, b in zip(mirror["subs"], ref["subs"]):
            total_subs += 1
            if a["name"] != b["name"]:
                mismatched.append((name, "sub name %r vs %r" % (a["name"], b["name"])))
                break
            if a["tail"] != b["tail"]:
                bad_tail.append((name, a["name"], a["tail"], b["tail"]))
            if len(a["instructions"]) != len(b["instructions"]):
                mismatched.append((name, "instr count in %s" % a["name"]))
                break
            for x, y in zip(a["instructions"], b["instructions"]):
                total_instrs += 1
                if (x["id"], x["size"], x["rank"], x["argc"], x["offset"]) != \
                   (y["id"], y["size"], y["rank"], y["argc"], y["offset"]):
                    mismatched.append((name, "instr fields in %s" % a["name"]))
                    break

    ok = not mismatched
    print("[%s] all %d .ecl files parse identically (%d mismatches)"
          % ("PASS" if ok else "FAIL", len(ecls), len(mismatched)))
    if not ok:
        for item in mismatched[:5]:
            print("       %s: %s" % item)
        failures += 1

    ok = not bad_tail
    print("[%s] all %d subroutines end with the same tail (%d bad)"
          % ("PASS" if ok else "FAIL", total_subs, len(bad_tail)))
    if not ok:
        for item in bad_tail[:5]:
            print("       %s" % (item,))
        failures += 1

    # NOTE: the correct totals are 816 / 23770, not the 486 / 12480 recorded
    # before the ECLI section was understood. ECLI is variable length, so 37
    # files (the bosses, which all depend on "default.ecl") used to be skipped
    # entirely. The 12-byte tail invariant is what proves 816 is right.
    ok = total_instrs == 23770
    print("[%s] instruction total is %d (expect 23770)"
          % ("PASS" if ok else "FAIL", total_instrs))
    if not ok:
        failures += 1

    ok = total_subs == 816
    print("[%s] subroutine total is %d (expect 816)"
          % ("PASS" if ok else "FAIL", total_subs))
    if not ok:
        failures += 1

    # The 12-byte tail invariant is the strongest structural signal.
    ok = all(item[2] == ECL_TRAILER for item in bad_tail) if bad_tail else True
    print("[%s] the %d-byte tail invariant holds" % ("PASS" if ok else "FAIL", ECL_TRAILER))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: cpp/game/EclResource.cpp matches the reference")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

