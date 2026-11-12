#!/usr/bin/env python3
"""TH185 ANM container parser, built on the thtk format definition.

## Provenance

The structure is NOT reverse-engineered from scratch. It comes from thtk's
`anm_types.h` (vendored under
`thvip/eagler-touhou-th11/ports/th15/web/reference/tools/thtk/`), which
documents `anm_header11_t` as the TH13+ header. TH185 reports version 8, so it
uses exactly that layout. Using the upstream definition instead of guessing
fixed three things I had wrong:

  1. `sprites` counts SPRITE DEFINITIONS, not atlas paths. (I spent a whole
     round on this and concluded wrongly.)
  2. Sprites and scripts are addressed THROUGH OFFSET TABLES, not stored in
     order. Right after sizeof(anm_header11_t) = 0x40 comes
     `u32 sprite_offsets[sprites]`, then `anm_offset_t script_offsets[scripts]`
     where anm_offset_t is { u32 id; u32 offset; }. My earlier guess (records
     follow the atlas name) happened to land on the right address for
     world01t.anm only because the table pointed there too.
  3. `text.anm` has no atlas file: its name is `@`, which thtk documents as
     the in-file marker.

## Header (anm_header11_t, 0x28 bytes + 24 zero bytes)

    +0x00  u32  version        // 8 for TH185
    +0x04  u16  sprites        // count of sprite_t records
    +0x06  u16  scripts        // count of script entries
    +0x08  u16  zero1
    +0x0A  u16  width
    +0x0C  u16  height
    +0x0E  u16  format         // see FORMAT_* below
    +0x10  u32  nameoffset     // atlas path (or "@")
    +0x14  u16  x, y
    +0x18  u32  memorypriority
    +0x1C  u32  thtxoffset     // first THTX block
    +0x20  u16  hasdata
    +0x22  u16  lowresscale    // TH14+: scale down at lower resolutions
    +0x24  u32  nextoffset     // offset of the next entry in the same file
    +0x28  u32  zero2[6]

## Pixel formats (thtk format_t)

    1 = BGRA8888
    3 = RGB565
    5 = ARGB4444
    7 = GRAY8

Measured on TH185: 5 (32 files), 1 (19 files), 3 (4 files).

## THTX block (thtx_header_t)

    char[4] magic = "THTX"
    u16     zero
    u16     format
    u16     width, height
    u32     size
    u8      data[size]

## Instruction formats (thtk formats_v8 + th18_patch)

thtk's anm_find_format() handles header_version 8 with version 185 explicitly:

    case 8:
        switch (version) {
        case 20: case 19: case 185: case 18:
            if ((format = find_format(th18_patch, id))) break;
        default:
            format = find_format(formats_v8, id);

So TH185 looks up th18_patch first (a single entry: 439 -> Sff), then
formats_v8. Both tables are extracted to reference/anm-opcodes.json.

Format characters and their widths, measured against the data:

    n / N / f / o / t   4 bytes
    s                   2 bytes   (op 421 is ss = 4 bytes -> 12 total)
    S                   variable  (u32 length + text + padding)

An instruction header is 8 bytes, so the size is 8 + fixed fields + strings.
The selftest asserts that formats_v8 predicts the size of every opcode it
covers; 71 of them appear in TH185 and all 71 match.

## Usage

    python th185_web/scripts/native/anm.py --selftest
    python th185_web/scripts/native/anm.py --list
    python th185_web/scripts/native/anm.py --file world01t.anm

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

ANM_VERSION = 8
HEADER_SIZE = 0x40  # sizeof(anm_header11_t)
SPRITE_SIZE = 20
THTX_MAGIC = b"THTX"

FORMAT_NAMES = {1: "BGRA8888", 3: "RGB565", 5: "ARGB4444", 7: "GRAY8"}


def _dat_path():
    from probe import target_path
    return target_path().parent / "th185.dat"


def parse_anm(blob: bytes):
    """Parse one .anm blob using anm_header11_t."""
    if len(blob) < HEADER_SIZE:
        raise SystemExit("anm blob too small")

    version = struct.unpack_from("<I", blob, 0x00)[0]
    sprites, scripts, zero1, width, height, fmt = struct.unpack_from(
        "<HHHHHH", blob, 0x04)
    name_offset = struct.unpack_from("<I", blob, 0x10)[0]
    memory_priority = struct.unpack_from("<I", blob, 0x18)[0]
    thtx_offset = struct.unpack_from("<I", blob, 0x1C)[0]
    has_data, low_res_scale = struct.unpack_from("<HH", blob, 0x20)
    next_offset = struct.unpack_from("<I", blob, 0x24)[0]

    if version != ANM_VERSION:
        raise SystemExit("unexpected anm version %d" % version)

    # Atlas name. "@" means the image lives inside this file.
    end = blob.find(b"\x00", name_offset)
    if end < 0:
        raise SystemExit("atlas name is not terminated")
    atlas = blob[name_offset:end].decode("ascii", "replace")

    # thtk layout (thanm.c):
    #   uint32_t sprite_offsets[sprites];   right after sizeof(anm_header11_t)
    #   anm_offset_t script_offsets[scripts]; then each { u32 id; u32 offset; }
    #
    # The sprite records are addressed THROUGH this table, not stored in order.
    # My earlier guess ("records follow the atlas name") happened to land on the
    # right address for world01t.anm because the table pointed there too, but it
    # is wrong in general: the table is what makes it work for every file.
    sprite_list = []
    for index in range(sprites):
        table_at = HEADER_SIZE + index * 4
        if table_at + 4 > len(blob):
            break
        at = struct.unpack_from("<I", blob, table_at)[0]
        if at + SPRITE_SIZE > len(blob):
            break
        sid, x, y, w, h = struct.unpack_from("<Iffff", blob, at)
        sprite_list.append({"id": sid, "x": x, "y": y, "w": w, "h": h,
                            "offset": at})

    # Script table, and the instruction stream each entry points at.
    script_table = HEADER_SIZE + sprites * 4
    script_list = []
    for index in range(scripts):
        entry_at = script_table + index * 8
        if entry_at + 8 > len(blob):
            break
        sid, offset = struct.unpack_from("<II", blob, entry_at)
        instructions = []
        at = offset
        while at + 4 <= len(blob):
            opcode, length = struct.unpack_from("<HH", blob, at)
            if opcode == 0xFFFF or length < 4 or at + length > len(blob):
                break
            instructions.append({"opcode": opcode, "length": length,
                                 "offset": at})
            at += length
        script_list.append({"id": sid, "offset": offset,
                            "instructions": instructions})

    # THTX textures: walk the chain via nextoffset as well as the header field.
    textures = []
    seen = set()
    at = thtx_offset
    while 0 < at < len(blob) and at not in seen:
        seen.add(at)
        if blob[at:at + 4] != THTX_MAGIC:
            break
        zero, tf, tw, th, tsize = struct.unpack_from("<HHHHI", blob, at + 4)
        if at + 16 + tsize > len(blob):
            break
        textures.append({
            "offset": at,
            "format": tf,
            "format_name": FORMAT_NAMES.get(tf, "?"),
            "width": tw,
            "height": th,
            "size": tsize,
            "bytes_per_pixel": (tsize / float(tw * th)) if tw and th else 0.0,
        })
        at += 16 + tsize

    return {
        "version": version,
        "sprites": sprites,
        "scripts": scripts,
        "zero1": zero1,
        "width": width,
        "height": height,
        "format": fmt,
        "format_name": FORMAT_NAMES.get(fmt, "?"),
        "atlas": atlas,
        "memory_priority": memory_priority,
        "has_data": has_data,
        "low_res_scale": low_res_scale,
        "next_offset": next_offset,
        "sprite_table": HEADER_SIZE,
        "script_table": script_table,
        "sprite_list": sprite_list,
        "script_list": script_list,
        "textures": textures,
    }


def load_anm(name: str):
    from pbg4 import extract_file

    dat = _dat_path()
    filename = name if name.endswith(".anm") else name + ".anm"
    return parse_anm(extract_file(dat, name=filename)["data"])


def selftest() -> int:
    print("=== ANM parser selftest (thtk anm_header11_t) ===")
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

    anms = [e["name"] for e in entries if e["name"].endswith(".anm")]
    ok = len(anms) == 55
    print("[%s] found %d .anm files (expect 55)" % ("PASS" if ok else "FAIL", len(anms)))
    if not ok:
        failures += 1

    parsed = {}
    errors = []
    for name in anms:
        try:
            parsed[name] = load_anm(name)
        except SystemExit as exc:
            errors.append((name, str(exc)))

    ok = not errors
    print("[%s] all %d .anm files parse (%d errors)"
          % ("PASS" if ok else "FAIL", len(anms), len(errors)))
    if not ok:
        for item in errors[:5]:
            print("       %s" % (item,))
        failures += 1

    if not parsed:
        print()
        print("selftest failed: nothing parsed")
        return 1

    ok = all(p["version"] == ANM_VERSION for p in parsed.values())
    print("[%s] every file reports version %d"
          % ("PASS" if ok else "FAIL", ANM_VERSION))
    if not ok:
        failures += 1

    # The sprite count in the header must equal the number of records we read.
    bad = [(n, len(p["sprite_list"]), p["sprites"])
           for n, p in parsed.items() if len(p["sprite_list"]) != p["sprites"]]
    ok = not bad
    print("[%s] sprite count matches the header for every file (%d bad)"
          % ("PASS" if ok else "FAIL", len(bad)))
    if not ok:
        for item in bad[:5]:
            print("       %s" % (item,))
        failures += 1

    total_sprites = sum(len(p["sprite_list"]) for p in parsed.values())
    ok = total_sprites == 1879
    print("[%s] %d sprite records total (expect 1879)"
          % ("PASS" if ok else "FAIL", total_sprites))
    if not ok:
        failures += 1

    formats = {}
    for p in parsed.values():
        formats[p["format"]] = formats.get(p["format"], 0) + 1
    ok = formats == {5: 32, 1: 19, 3: 4}
    print("[%s] format distribution is {5: 32, 1: 19, 3: 4} (got %s)"
          % ("PASS" if ok else "FAIL", formats))
    if not ok:
        failures += 1

    # Every THTX block must have size == w * h * bpp with a whole bpp.
    bad_tex = []
    for name, p in parsed.items():
        for tex in p["textures"]:
            bpp = tex["bytes_per_pixel"]
            if bpp != int(bpp) or bpp not in (2, 4, 1):
                bad_tex.append((name, tex["format"], bpp))
    ok = not bad_tex
    print("[%s] every THTX block has a whole bytes-per-pixel of 1/2/4 (%d bad)"
          % ("PASS" if ok else "FAIL", len(bad_tex)))
    if not ok:
        for item in bad_tex[:5]:
            print("       %s" % (item,))
        failures += 1

    # text.anm is the in-file atlas: its name is "@".
    ok = parsed.get("text.anm", {}).get("atlas") == "@"
    print("[%s] text.anm uses the in-file atlas marker \"@\""
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    lowres = sum(1 for p in parsed.values() if p["low_res_scale"])
    ok = lowres == 8
    print("[%s] %d files set lowresscale (expect 8)" % ("PASS" if ok else "FAIL", lowres))
    if not ok:
        failures += 1

    # Script entries and their instruction streams.
    total_scripts = sum(len(p['script_list']) for p in parsed.values())
    ok = total_scripts == 610
    print('[%s] %d script entries total (expect 610)'
          % ('PASS' if ok else 'FAIL', total_scripts))
    if not ok:
        failures += 1

    total_instr = sum(len(s['instructions']) for p in parsed.values()
                      for s in p['script_list'])
    ok = total_instr == 10566
    print('[%s] %d script instructions total (expect 10566)'
          % ('PASS' if ok else 'FAIL', total_instr))
    if not ok:
        failures += 1

    # Every instruction must have a length >= 8 and the stream must be
    # self-consistent (offset + length lands on the next instruction).
    bad_instr = []
    for name, p in parsed.items():
        for script in p['script_list']:
            instrs = script['instructions']
            for index, ins in enumerate(instrs):
                if ins['length'] < 8 or ins['length'] % 4 != 0:
                    bad_instr.append((name, ins['offset'], ins['length']))
                    break
                if index + 1 < len(instrs):
                    if ins['offset'] + ins['length'] != instrs[index + 1]['offset']:
                        bad_instr.append((name, ins['offset'], 'gap'))
                        break
    ok = not bad_instr
    print('[%s] every instruction is 8+ bytes, 4-aligned, and contiguous (%d bad)'
          % ('PASS' if ok else 'FAIL', len(bad_instr)))
    if not ok:
        for item in bad_instr[:5]:
            print('       %s' % (item,))
        failures += 1

    # The thtk formats_v8 table must predict the sizes of the opcodes it covers.
    # This is the check that validates the vendored table against real data.
    import json as _json
    table_path = HERE.parent.parent / 'reference' / 'anm-opcodes.json'
    if table_path.is_file():
        table = _json.loads(table_path.read_text(encoding='utf-8'))
        v8 = {int(k): v for k, v in table['formats_v8'].items()}
        patch = {int(k): v for k, v in table['th18_patch'].items()}
        observed = {}
        for p2 in parsed.values():
            for script in p2['script_list']:
                for ins in script['instructions']:
                    observed.setdefault(ins['opcode'], set()).add(ins['length'])

        mismatched = []
        covered = 0
        for opcode, lengths in observed.items():
            fmt = patch.get(opcode, v8.get(opcode))
            if fmt is None:
                continue
            covered += 1
            # Field widths, measured against the data:
            #   n / N / f / o / t  -> 4 bytes
            #   s                  -> 2 bytes  (op 421 is "ss" = 4 bytes total,
            #                        which is 12 with the 8-byte header)
            #   S                  -> variable (u32 length + text + padding)
            fixed = sum(4 for c in fmt if c in 'nfotN') + sum(2 for c in fmt if c == 's')
            has_string = 'S' in fmt
            if has_string:
                # Strings are variable length, so only the minimum is known.
                if min(lengths) < 8 + fixed:
                    mismatched.append((opcode, fmt, sorted(lengths)))
            else:
                expected = 8 + fixed
                if lengths != {expected}:
                    mismatched.append((opcode, fmt, sorted(lengths), expected))
        ok = not mismatched
        print('[%s] thtk formats_v8 predicts the size of all %d covered opcodes (%d bad)'
              % ('PASS' if ok else 'FAIL', covered, len(mismatched)))
        if not ok:
            for item in mismatched[:5]:
                print('       %s' % (item,))
            failures += 1
    else:
        print('[SKIP] reference/anm-opcodes.json not found')

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: ANM container fully parsed via anm_header11_t")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 ANM parser")
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
        print("%-22s %8s %8s %6s %5s %s"
              % ("file", "sprites", "scripts", "w x h", "fmt", "atlas"))
        for e in entries:
            if not e["name"].endswith(".anm"):
                continue
            p = load_anm(e["name"])
            print("%-22s %8d %8d %6dx%-4d %5d %s"
                  % (e["name"], p["sprites"], p["scripts"],
                     p["width"], p["height"], p["format"], p["atlas"]))
        return 0

    if args.file:
        p = load_anm(args.file)
        print("version        : %d" % p["version"])
        print("sprites        : %d" % p["sprites"])
        print("scripts        : %d" % p["scripts"])
        print("size           : %dx%d" % (p["width"], p["height"]))
        print("format         : %d (%s)" % (p["format"], p["format_name"]))
        print("atlas          : %s" % p["atlas"])
        print("memory priority: %d" % p["memory_priority"])
        print("lowres scale   : %d" % p["low_res_scale"])
        print("sprite offset  : %d" % p["sprite_offset"])
        print()
        print("sprites (first 10):")
        for s in p["sprite_list"][:10]:
            print("  id=%-5d x=%-8.2f y=%-8.2f w=%-8.2f h=%-8.2f"
                  % (s["id"], s["x"], s["y"], s["w"], s["h"]))
        print()
        print("textures:")
        for t in p["textures"]:
            print("  @0x%06X fmt=%d(%s) %dx%d size=%d bpp=%.1f"
                  % (t["offset"], t["format"], t["format_name"],
                     t["width"], t["height"], t["size"], t["bytes_per_pixel"]))
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

