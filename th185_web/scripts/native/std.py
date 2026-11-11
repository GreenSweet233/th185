#!/usr/bin/env python3
"""TH185 .std stage layout parser.

## What .std is

A `.std` file describes the layout of one scene: its ANM background, the wave
groups, and the entity placements. There are 9 of them (world01t + world01..08),
matching the 9 scenes.

**Important distinction**: the `.std` files are NOT the wave template pool.
The 111-entry wave name table (VA 0x4B5D50, see docs/WAVES.md) is a separate
global table. `.std` holds per-scene layout data.

## Format (reverse-engineered from real data)

    +0x00  uint16  wave_count      // number of wave slots in this scene
    +0x02  uint16  group_count     // number of entity groups
    +0x04  uint32  entity_offset   // absolute offset of the entity table
    +0x08  uint32  ???             // observed: 500..2704, semantics TBD
    +0x0C  uint32  reserved        // always 0
    +0x10  char[]  anm_name        // NUL-terminated, e.g. "world01t.anm"
    ...
    +0x90  uint32  wave_offsets[wave_count]   // absolute offsets, stride 60
    ...
    +entity_offset: entity table

## Wave entry (60 bytes each, at wave_offsets[i])

    +0x00  uint32  marker      // 0x00010000, 0x00060001, 0x00020004 ...
                               // low 16 bits look like an index
    +0x10  float   f1          // observed 256.0 / 300.0
    +0x14  float   f2          // observed 256.0 / 1024.0
    +0x18  float   f3          // observed 256.0 / 512.0
    +0x1C  uint32  flags       // observed 0x001C0000
    +0x20  uint32  index       // 0, 1, 2, ...
    +0x30  float   g1, g2      // same values as f1/f2
    +0x38  uint32  marker2     // observed 0x0004FFFF

## Entity entry (16 bytes each, at entity_offset)

    +0x00  uint32  kind        // high byte 0x01, e.g. 0x01000000
                               //   bits 16..23 = type, bits 0..15 = index
    +0x04  float   x
    +0x08  float   y
    +0x0C  float   z

Verified on world01t.std: 88 entity entries with plausible coordinates
(-256..960), and 7 wave entries with 60-byte stride.

## Usage

    python th185_web/scripts/native/std.py --list
    python th185_web/scripts/native/std.py --scene world01t
    python th185_web/scripts/native/std.py --selftest

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

HEADER_SIZE = 16
ANM_NAME_MAX = 64
WAVE_OFFSET_TABLE = 0x90
WAVE_ENTRY_SIZE = 60
GROUP_ENTRY_SIZE = 28
ENTITY_SIZE = 16


def _dat_path():
    from probe import target_path
    return target_path().parent / "th185.dat"


def parse_std(blob):
    """Parse one .std blob into a dict."""
    if len(blob) < HEADER_SIZE:
        raise SystemExit("std blob too small")
    wave_count, group_count, entity_off, unknown = struct.unpack("<HHII", blob[:12])

    name_raw = blob[HEADER_SIZE:HEADER_SIZE + ANM_NAME_MAX]
    anm_name = name_raw.split(b"\x00")[0].decode("ascii", "replace")

    # Wave offset table
    wave_offsets = []
    for i in range(wave_count):
        pos = WAVE_OFFSET_TABLE + i * 4
        if pos + 4 > len(blob):
            break
        off = struct.unpack_from("<I", blob, pos)[0]
        if off >= len(blob):
            break
        wave_offsets.append(off)

    waves = []
    for i, off in enumerate(wave_offsets):
        if off + WAVE_ENTRY_SIZE > len(blob):
            break
        marker, = struct.unpack_from("<I", blob, off)
        f1, f2, f3 = struct.unpack_from("<3f", blob, off + 0x10)
        flags, = struct.unpack_from("<I", blob, off + 0x1C)
        index, = struct.unpack_from("<I", blob, off + 0x20)
        marker2, = struct.unpack_from("<I", blob, off + 0x38)
        waves.append({
            "offset": off,
            "marker": marker,
            "f1": f1, "f2": f2, "f3": f3,
            "flags": flags,
            "index": index,
            "marker2": marker2,
        })

    # --- 28-byte entity-group table ---
    #
    # Sits between the last wave entry and the entity table, with a VARIABLE
    # number of entries (0 for world01t..03, 15 for world04, 5 for world05,
    # 7 for world06..08; 41 in total). The last wave entry does NOT necessarily
    # end where the entity table begins, so this span must be walked explicitly.
    groups_start = waves[-1]["offset"] + WAVE_ENTRY_SIZE if waves else entity_off
    groups = []
    if entity_off > groups_start:
        span = entity_off - groups_start
        if span % GROUP_ENTRY_SIZE == 0:
            for i in range(span // GROUP_ENTRY_SIZE):
                base = groups_start + i * GROUP_ENTRY_SIZE
                gtype, = struct.unpack_from("<I", blob, base + 0x00)
                gx, gy = struct.unpack_from("<2f", blob, base + 0x04)
                reserved, = struct.unpack_from("<I", blob, base + 0x0C)
                sa, sb = struct.unpack_from("<2f", blob, base + 0x10)
                marker, = struct.unpack_from("<I", blob, base + 0x18)
                groups.append({
                    "offset": base,
                    "type": gtype,
                    "x": gx, "y": gy,
                    "reserved": reserved,
                    "size_a": sa, "size_b": sb,
                    "marker": marker,
                })

    # Entity table
    entities = []
    pos = entity_off
    while pos + ENTITY_SIZE <= len(blob):
        kind, = struct.unpack_from("<I", blob, pos)
        if (kind >> 24) != 0x01:
            break
        x, y, z = struct.unpack_from("<3f", blob, pos + 4)
        entities.append({
            "offset": pos,
            "kind": kind,
            "type": (kind >> 16) & 0xFF,
            "index": kind & 0xFFFF,
            "x": x, "y": y, "z": z,
        })
        pos += ENTITY_SIZE

    # Command stream: starts at header+0x08 value, with a 4-byte preamble.
    commands = []
    cmd_pos = unknown + 4
    while cmd_pos + 4 <= len(blob):
        cid, size = struct.unpack_from("<HH", blob, cmd_pos)
        if cid == 0 and size == 0:
            break
        if size < 4 or cmd_pos + size > len(blob):
            break
        commands.append({
            "offset": cmd_pos,
            "id": cid,
            "size": size,
            "payload": blob[cmd_pos + 4:cmd_pos + size],
        })
        cmd_pos += size

    return {
        "groups": groups,
        "commands": commands,
        "command_end": cmd_pos,
        "wave_count": wave_count,
        "group_count": group_count,
        "entity_offset": entity_off,
        "unknown": unknown,
        "anm_name": anm_name,
        "waves": waves,
        "entities": entities,
    }


def load_scene(name):
    """Extract and parse one .std by scene name (e.g. "world01t")."""
    from pbg4 import extract_file

    dat = _dat_path()
    filename = name if name.endswith(".std") else name + ".std"
    info = extract_file(dat, name=filename)
    return parse_std(info["data"])


def selftest() -> int:
    print("=== .std parser selftest ===")
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

    stds = [e["name"] for e in entries if e["name"].endswith(".std")]
    ok = len(stds) == 9
    print("[%s] found %d .std files (expect 9)" % ("PASS" if ok else "FAIL", len(stds)))
    if not ok:
        failures += 1

    try:
        parsed = {name: load_scene(name) for name in stds}
    except SystemExit as exc:
        print("[FAIL] parse error: %s" % exc)
        return 1

    ok = all(p["anm_name"].endswith(".anm") for p in parsed.values())
    print("[%s] every scene has a valid .anm name" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = all(len(p["waves"]) == p["wave_count"] for p in parsed.values())
    print("[%s] wave count matches the offset table for every scene"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # NOTE: the field at +0x20 is NOT a sequential index. Real data shows
    # sequences like [0,1,2,2,2,9,10] (world03) and [1,0,5,4] (world04),
    # so it is a wave type / slot id, not an ordinal.
    values = {w["index"] for p in parsed.values() for w in p["waves"]}
    ok = len(values) > 1
    print("[%s] wave slot ids vary (observed values: %s)"
          % ("PASS" if ok else "FAIL", sorted(values)[:8]))
    if not ok:
        failures += 1

    total_ent = sum(len(p["entities"]) for p in parsed.values())
    ok = total_ent > 0
    print("[%s] parsed %d entity entries across all scenes"
          % ("PASS" if ok else "FAIL", total_ent))
    if not ok:
        failures += 1

    # Real data contains y values up to ~4352 (world04/world06), so the
    # plausible range is wider than one might guess.
    ok = all(-8192.0 < e["x"] < 8192.0 and -8192.0 < e["y"] < 8192.0
             and -8192.0 < e["z"] < 8192.0
             for p in parsed.values() for e in p["entities"])
    print("[%s] all entity coordinates are plausible (|v| < 8192)"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # Command stream: every scene must consume the file except a fixed 16-byte tail.
    ok = all(p["commands"] and len(p["command_end_bytes"]) == 16
             for p in parsed.values()) if False else None
    leftovers = []
    for name in stds:
        p = parsed[name]
        from pbg4 import extract_file
        total = len(extract_file(dat, name=name)["data"])
        leftovers.append(total - p["command_end"])
    ok = all(v == 16 for v in leftovers)
    print("[%s] command stream ends with exactly 16 bytes left in every scene"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        print("       leftovers: %s" % leftovers)
        failures += 1

    cmd_ids = sorted({c["id"] for p in parsed.values() for c in p["commands"]})
    ok = len(cmd_ids) >= 10
    print("[%s] observed %d distinct command ids: %s"
          % ("PASS" if ok else "FAIL", len(cmd_ids), cmd_ids))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: all 9 scenes parsed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 .std stage layout parser")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--scene")
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

    stds = [e["name"] for e in entries if e["name"].endswith(".std")]

    if args.list:
        print("%-16s %6s %6s %8s %8s %8s"
              % ("scene", "waves", "groups", "ent_off", "unknown", "entities"))
        for name in stds:
            p = load_scene(name)
            print("%-16s %6d %6d %8d %8d %8d"
                  % (name, p["wave_count"], p["group_count"],
                     p["entity_offset"], p["unknown"], len(p["entities"])))
        return 0

    if args.scene:
        p = load_scene(args.scene)
        print("scene        : %s" % args.scene)
        print("anm          : %s" % p["anm_name"])
        print("wave count   : %d" % p["wave_count"])
        print("group count  : %d" % p["group_count"])
        print("entity offset: %d" % p["entity_offset"])
        print()
        print("waves:")
        for w in p["waves"]:
            print("  [%2d] @0x%04X marker=0x%08X f=(%.0f, %.0f, %.0f) flags=0x%08X"
                  % (w["index"], w["offset"], w["marker"],
                     w["f1"], w["f2"], w["f3"], w["flags"]))
        print()
        print("entities (%d):" % len(p["entities"]))
        for e in p["entities"][:40]:
            print("  @0x%04X type=%d idx=%-3d pos=(%8.1f, %8.1f, %8.1f)"
                  % (e["offset"], e["type"], e["index"], e["x"], e["y"], e["z"]))
        if len(p["entities"]) > 40:
            print("  ... %d more" % (len(p["entities"]) - 40))
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

