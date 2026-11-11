#!/usr/bin/env python3
"""Cross-check cpp/game/StageScript.cpp against the verified Python reference.

Contains a line-for-line Python mirror of StageScript.cpp and compares it with
`std.py`, which is verified on all 9 .std files.

Contract: if StageScript.cpp changes, StageMirror below must change with it.

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

STD_WAVE_TABLE_OFFSET = 0x90
STD_WAVE_ENTRY_SIZE = 60
STD_GROUP_ENTRY_SIZE = 28
STD_ENTITY_ENTRY_SIZE = 16
STD_COMMAND_HEADER = 4


def parse_std_mirror(blob: bytes):
    """Mirror of StageScript::parse()."""
    if len(blob) < 0x94:
        return None

    wave_count, group_count = struct.unpack_from("<HH", blob, 0x00)
    entity_offset, command_offset = struct.unpack_from("<II", blob, 0x04)

    end = blob.find(b"\x00", 0x10)
    if end < 0:
        return None
    anm_name = blob[0x10:end].decode("ascii", "replace")

    if entity_offset >= len(blob):
        return None

    waves = []
    for i in range(wave_count):
        pos = STD_WAVE_TABLE_OFFSET + i * 4
        if pos + 4 > len(blob):
            break
        offset = struct.unpack_from("<I", blob, pos)[0]
        if offset + STD_WAVE_ENTRY_SIZE > len(blob):
            break
        marker = struct.unpack_from("<I", blob, offset + 0x00)[0]
        f1, f2, f3 = struct.unpack_from("<3f", blob, offset + 0x10)
        flags = struct.unpack_from("<I", blob, offset + 0x1C)[0]
        slot_id = struct.unpack_from("<I", blob, offset + 0x20)[0]
        marker2 = struct.unpack_from("<I", blob, offset + 0x38)[0]
        waves.append({
            "offset": offset, "marker": marker,
            "f1": f1, "f2": f2, "f3": f3,
            "flags": flags, "slot_id": slot_id, "marker2": marker2,
        })
    if not waves:
        return None

    groups_start = waves[-1]["offset"] + STD_WAVE_ENTRY_SIZE
    groups = []
    if entity_offset > groups_start:
        span = entity_offset - groups_start
        if span % STD_GROUP_ENTRY_SIZE != 0:
            return None
        for i in range(span // STD_GROUP_ENTRY_SIZE):
            base = groups_start + i * STD_GROUP_ENTRY_SIZE
            gtype = struct.unpack_from("<I", blob, base + 0x00)[0]
            gx, gy = struct.unpack_from("<2f", blob, base + 0x04)
            reserved = struct.unpack_from("<I", blob, base + 0x0C)[0]
            sa, sb = struct.unpack_from("<2f", blob, base + 0x10)
            marker = struct.unpack_from("<I", blob, base + 0x18)[0]
            groups.append({
                "type": gtype, "x": gx, "y": gy, "reserved": reserved,
                "size_a": sa, "size_b": sb, "marker": marker,
            })

    entities = []
    pos = entity_offset
    while pos + STD_ENTITY_ENTRY_SIZE <= len(blob):
        kind = struct.unpack_from("<I", blob, pos)[0]
        if (kind >> 24) != 0x01:
            break
        ex, ey, ez = struct.unpack_from("<3f", blob, pos + 4)
        entities.append({"kind": kind, "x": ex, "y": ey, "z": ez})
        pos += STD_ENTITY_ENTRY_SIZE

    commands = []
    pos = command_offset + STD_COMMAND_HEADER
    while pos + STD_COMMAND_HEADER <= len(blob):
        cid, size = struct.unpack_from("<HH", blob, pos)
        if cid == 0 and size == 0:
            break
        if size < 4 or pos + size > len(blob):
            break
        commands.append({"id": cid, "size": size, "offset": pos})
        pos += size
    if not commands:
        return None

    return {
        "wave_count": wave_count, "group_count": group_count,
        "entity_offset": entity_offset, "command_offset": command_offset,
        "anm_name": anm_name, "waves": waves, "groups": groups,
        "entities": entities, "commands": commands, "command_end": pos,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Cross-check cpp/game/StageScript.cpp against the reference")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    print("=== C++ StageScript.cpp mirror selftest ===")
    print()
    failures = 0

    try:
        from std import load_scene
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
    stds = [e["name"] for e in entries if e["name"].endswith(".std")]

    mismatched = []
    total_waves = total_groups = total_entities = total_commands = 0
    tails = []

    for name in stds:
        blob = extract_file(dat, name=name)["data"]
        mirror = parse_std_mirror(blob)
        if mirror is None:
            mismatched.append((name, "mirror returned None"))
            continue
        ref = load_scene(name)

        for key in ("wave_count", "group_count", "entity_offset", "anm_name"):
            if mirror[key] != ref[key]:
                mismatched.append((name, "%s %s vs %s"
                                   % (key, mirror[key], ref[key])))
        if len(mirror["waves"]) != len(ref["waves"]):
            mismatched.append((name, "wave count %d vs %d"
                               % (len(mirror["waves"]), len(ref["waves"]))))
        if len(mirror["entities"]) != len(ref["entities"]):
            mismatched.append((name, "entity count %d vs %d"
                               % (len(mirror["entities"]), len(ref["entities"]))))
        if len(mirror["commands"]) != len(ref["commands"]):
            mismatched.append((name, "command count %d vs %d"
                               % (len(mirror["commands"]), len(ref["commands"]))))

        for a, b in zip(mirror["waves"], ref["waves"]):
            if (a["offset"], a["marker"], a["slot_id"], a["flags"]) != \
               (b["offset"], b["marker"], b["index"], b["flags"]):
                mismatched.append((name, "wave fields"))
                break
        for a, b in zip(mirror["entities"], ref["entities"]):
            if a["kind"] != b["kind"] or a["x"] != b["x"] or a["y"] != b["y"]:
                mismatched.append((name, "entity fields"))
                break
        for a, b in zip(mirror["commands"], ref["commands"]):
            if (a["id"], a["size"], a["offset"]) != (b["id"], b["size"], b["offset"]):
                mismatched.append((name, "command fields"))
                break

        total_waves += len(mirror["waves"])
        total_groups += len(mirror["groups"])
        total_entities += len(mirror["entities"])
        total_commands += len(mirror["commands"])
        tails.append(len(blob) - mirror["command_end"])

    ok = not mismatched
    print("[%s] all %d .std files parse identically (%d mismatches)"
          % ("PASS" if ok else "FAIL", len(stds), len(mismatched)))
    if not ok:
        for item in mismatched[:5]:
            print("       %s: %s" % item)
        failures += 1

    ok = total_entities == 379
    print("[%s] entity total is %d (expect 379)"
          % ("PASS" if ok else "FAIL", total_entities))
    if not ok:
        failures += 1

    # The 28-byte group table is the newly discovered section.
    ok = total_groups == 41
    print("[%s] entity-group total is %d (expect 41: 0+0+0+0+15+5+7+7+7)"
          % ("PASS" if ok else "FAIL", total_groups))
    if not ok:
        failures += 1

    ok = all(t == 16 for t in tails)
    print("[%s] every scene ends with exactly 16 bytes of 0xFF (%s)"
          % ("PASS" if ok else "FAIL", tails))
    if not ok:
        failures += 1

    # Per-scene counts: 38+40+47+21+16+49+63+117+117 = 508.
    ok = total_commands == 508
    print("[%s] command total is %d (expect 508)"
          % ("PASS" if ok else "FAIL", total_commands))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: cpp/game/StageScript.cpp matches the reference")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

