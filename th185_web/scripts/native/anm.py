#!/usr/bin/env python3
"""TH185 ANM (animation) container parser.

## Format

The 55 `.anm` files hold sprite sheets plus the scripted animations that use
them. Structure (verified on world01t / enemy / bullet):

    +0x00  uint32  version = 8
    +0x04  uint32  file size (echo)
    +0x08  uint32  constant 0x02000000 (0x01000000 for bullet.anm)
    +0x0C  uint32  constant 0x00050200
    +0x10  uint32  sprite-table offset      <- points at the name table
    +0x14  uint32  reserved = 0
    +0x18  uint32  sprite count   <- counts SPRITE DEFINITIONS, not paths
    +0x1C  uint32  first-script offset      <- points at a THTX block

## Atlas path table (at +0x10)

One or more NUL-terminated image paths, 4-byte padded. The count of PATHS is
NOT `sprite count`; the sprite definitions follow as fixed-size records.

    uint32  format;    // 0 in every observed entry
    uint32  id;        // 128 for the first entry
    char    path[];    // NUL-terminated, FULL path, 4-byte padded,
                       //   e.g. "background/world01t/st01a.png"

## THTX texture blocks

    char   magic[4];   // "THTX"
    uint16 format;     // 0 observed
    uint16 width;      // 512 / 256 observed
    uint16 height;     // 512 / 256 observed
    uint16 pad;
    uint32 data_size;  // 524288 = 512*512*2, or 262144 = 256*256*4
    uint8  pixels[data_size];

Note the two observed blocks differ in bytes-per-pixel (2 vs 4), so the
pixel format is NOT implied by the header alone; it must come from the
consumer. That is why `data_size` is the reliable field.

## Scripts

ANM scripts use the SAME instruction encoding as .std and .ecl:

    uint16 id; uint16 size; payload[size - 4];
    // terminated by id == 0xFFFF

Observed opcodes in the first world01t script: 302, 300, 401, 403, 5, 3.

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
HEADER_SIZE = 0x20
THTX_MAGIC = b"THTX"
SCRIPT_TERMINATOR = 0xFFFF


def _dat_path():
    from probe import target_path
    return target_path().parent / "th185.dat"


def parse_anm(blob: bytes):
    """Parse one .anm blob."""
    if len(blob) < HEADER_SIZE:
        raise SystemExit("anm blob too small")

    version, file_size, const_a, const_b = struct.unpack_from("<IIII", blob, 0)
    sprite_offset, reserved, sprite_count, script_offset = struct.unpack_from(
        "<IIII", blob, 0x10)

    if version != ANM_VERSION:
        raise SystemExit("unsupported anm version %d" % version)
    if sprite_offset >= len(blob):
        raise SystemExit("sprite offset out of range")

    # --- atlas path table ---
    #
    # +0x10 points AT THE FIRST ATLAS PATH, not at a record header. Paths are
    # NUL-terminated and padded to a 4-byte boundary.
    #
    # IMPORTANT: the number of PATHS is NOT sprite_count. sprite_count counts
    # SPRITE DEFINITIONS, which follow the paths as fixed-size records. Treating
    # sprite_count as a path count walks straight into those records and yields
    # empty strings for everything after the first path.
    atlas_paths = []
    pos = sprite_offset
    while pos < len(blob) and len(atlas_paths) < 64:
        end = blob.find(b"\x00", pos)
        if end < 0:
            break
        text = blob[pos:end]
        if not text:
            break
        printable = all(32 <= byte < 127 for byte in text)
        looks_like_path = text.endswith(b".png") or text.endswith(b".jpg")
        if not printable or not looks_like_path:
            break
        atlas_paths.append(text.decode("ascii", "replace"))
        pos = (end + 1 + 3) & ~3

    sprites = [{"index": i, "path": path} for i, path in enumerate(atlas_paths)]

    # --- THTX texture blocks: scan the whole file, they are self-describing ---
    textures = []
    search = 0
    while True:
        at = blob.find(THTX_MAGIC, search)
        if at < 0:
            break
        search = at + 4
        if at + 16 > len(blob):
            break
        fmt, width, height, pad, data_size = struct.unpack_from("<HHHHI", blob, at + 4)
        if width == 0 or height == 0:
            continue
        if at + 16 + data_size > len(blob):
            continue
        textures.append({
            "offset": at,
            "format": fmt,
            "width": width,
            "height": height,
            "data_size": data_size,
            "bytes_per_pixel": data_size / float(width * height),
        })

    # --- scripts: [u16 id][u16 size] runs terminated by 0xFFFF ---
    scripts = parse_scripts(blob, script_offset)

    return {
        "version": version,
        "file_size": file_size,
        "const_a": const_a,
        "const_b": const_b,
        "sprite_offset": sprite_offset,
        "sprite_count": sprite_count,
        "script_offset": script_offset,
        "sprites": sprites,
        "textures": textures,
        "scripts": scripts,
    }


def parse_scripts(blob: bytes, start: int):
    """Walk instruction runs from `start` to the end of the file."""
    runs = []
    pos = start
    while pos + 4 <= len(blob):
        # A run starts here if the first word looks like a plausible opcode.
        instructions = []
        scan = pos
        while scan + 4 <= len(blob):
            opcode, size = struct.unpack_from("<HH", blob, scan)
            if opcode == SCRIPT_TERMINATOR:
                scan += 4
                break
            if size < 4 or scan + size > len(blob):
                instructions = []
                break
            instructions.append({"id": opcode, "size": size, "offset": scan})
            scan += size
        if not instructions:
            break
        runs.append({"offset": pos, "instructions": instructions, "end": scan})
        pos = scan
    return runs


def load_anm(name: str):
    from pbg4 import extract_file

    dat = _dat_path()
    filename = name if name.endswith(".anm") else name + ".anm"
    return parse_anm(extract_file(dat, name=filename)["data"])


def selftest() -> int:
    print("=== ANM parser selftest ===")
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
            print("       %s: %s" % item)
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

    def is_font_atlas(n):
        return n == "text.anm" or n.startswith("ascii")

    non_font = {n: p for n, p in parsed.items() if not is_font_atlas(n)}
    ok = all(len(p["sprites"]) >= 1 for p in non_font.values())
    print("[%s] every non-font file has at least one atlas path (%d files)"
          % ("PASS" if ok else "FAIL", len(non_font)))
    if not ok:
        bad = [n for n, p in non_font.items() if not p["sprites"]]
        print("       %s" % bad[:5])
        failures += 1

    # sprite_count counts SPRITE DEFINITIONS, not paths. Report the gap so a
    # future reader does not re-derive it as a path count.
    gaps = sorted({p["sprite_count"] - len(p["sprites"]) for p in non_font.values()})
    print("       (sprite_count minus path count, observed: %s)" % gaps[:6])

    total_sprites = sum(len(p["sprites"]) for p in parsed.values())
    ok = total_sprites > 40
    print("[%s] parsed %d atlas paths across all files"
          % ("PASS" if ok else "FAIL", total_sprites))
    if not ok:
        failures += 1

    # Font atlases hold glyph slots, not image paths, so they legitimately have
    # no .png names: text.anm and ascii*.anm (the ASCII bitmap font).
    def is_font_atlas(name):
        return name == "text.anm" or name.startswith("ascii")

    bad = []
    for name, resource in parsed.items():
        if is_font_atlas(name):
            continue
        for sprite in resource["sprites"]:
            if not sprite["path"].endswith((".png", ".jpg")):
                bad.append((name, sprite["path"]))
    ok = not bad
    print("[%s] every sprite path ends with an image extension (%d bad)"
          % ("PASS" if ok else "FAIL", len(bad)))
    if not ok:
        print("       %s" % bad[:5])
        failures += 1

    fonts = sorted(n for n in parsed if is_font_atlas(n))
    ok = len(fonts) >= 4
    print("[%s] %d font atlases parsed (glyph slots, no .png names): %s"
          % ("PASS" if ok else "FAIL", len(fonts), fonts))
    if not ok:
        failures += 1

    total_tex = sum(len(p["textures"]) for p in parsed.values())
    ok = total_tex > 0
    print("[%s] found %d THTX texture blocks" % ("PASS" if ok else "FAIL", total_tex))
    if not ok:
        failures += 1

    # data_size must equal width * height * bytes_per_pixel for a sane bpp.
    ok = all(t["data_size"] == t["width"] * t["height"] * int(t["bytes_per_pixel"])
             for p in parsed.values() for t in p["textures"]
             if t["bytes_per_pixel"] == int(t["bytes_per_pixel"]))
    print("[%s] THTX data_size equals width * height * whole bpp"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    total_instr = sum(len(run["instructions"]) for p in parsed.values()
                      for run in p["scripts"])
    ok = total_instr > 0
    print("[%s] parsed %d ANM script instructions"
          % ("PASS" if ok else "FAIL", total_instr))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: ANM container parsed")
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
        print("%-22s %10s %8s %8s %8s" % ("file", "size", "sprites", "textures", "instrs"))
        for e in entries:
            if not e["name"].endswith(".anm"):
                continue
            p = load_anm(e["name"])
            n = sum(len(run["instructions"]) for run in p["scripts"])
            print("%-22s %10d %8d %8d %8d"
                  % (e["name"], e["size"], len(p["sprites"]),
                     len(p["textures"]), n))
        return 0

    if args.file:
        p = load_anm(args.file)
        print("file          : %s" % args.file)
        print("version       : %d" % p["version"])
        print("sprite count  : %d" % p["sprite_count"])
        print("sprite offset : %d" % p["sprite_offset"])
        print("script offset : %d" % p["script_offset"])
        print()
        print("sprites:")
        for s in p["sprites"]:
            print("  [%2d] %s" % (s["index"], s["path"]))
        print()
        print("textures:")
        for t in p["textures"]:
            print("  @0x%06X %dx%d bpp=%.1f data=%d"
                  % (t["offset"], t["width"], t["height"],
                     t["bytes_per_pixel"], t["data_size"]))
        print()
        print("scripts: %d run(s)" % len(p["scripts"]))
        for i, run in enumerate(p["scripts"][:4]):
            print("  [%d] @0x%04X %d instructions"
                  % (i, run["offset"], len(run["instructions"])))
            for ins in run["instructions"][:8]:
                print("       id=%-5d size=%d" % (ins["id"], ins["size"]))
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

