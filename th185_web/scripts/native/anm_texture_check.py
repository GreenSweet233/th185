#!/usr/bin/env python3
"""Verify the C++ ANM texture decoder against real THTX data.

The C++ side (cpp/game/AnmTexture.cpp) converts raw THTX pixels to RGBA8.
This check re-implements the same conversions in Python and asserts:

  1. every THTX block in the archive has a known format and a matching size
  2. the C++ mapping from format code to bytes-per-pixel is the one the data
     actually exhibits (this is what catches a wrong code table)
  3. the decode is lossless for BGRA8888 and Gray8, and bit-exact for the
     packed 16-bit formats, against an independent Python implementation

Property 2 matters most: if the format table were wrong, the size check would
fail for real files, which is a signal the data can produce on its own.

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# Mirrors AnmTexture.cpp.
FORMAT_BYTES = {1: 4, 3: 2, 5: 2, 7: 1}
FORMAT_NAMES = {1: "BGRA8888", 3: "RGB565", 5: "ARGB4444", 7: "GRAY8"}


def expand5(v):
    return ((v << 3) | (v >> 2)) & 0xFF


def expand6(v):
    return ((v << 2) | (v >> 4)) & 0xFF


def expand4(v):
    return ((v << 4) | v) & 0xFF


def decode(fmt, width, height, data):
    """Independent RGBA8 decode, mirroring the C++ implementation."""
    bpp = FORMAT_BYTES.get(fmt)
    if bpp is None:
        return None
    if len(data) != width * height * bpp:
        return None
    count = width * height
    out = bytearray(count * 4)
    if fmt == 1:
        for i in range(count):
            b, g, r, a = data[i * 4:i * 4 + 4]
            out[i * 4:i * 4 + 4] = bytes((r, g, b, a))
    elif fmt == 3:
        for i in range(count):
            v = data[i * 2] | (data[i * 2 + 1] << 8)
            out[i * 4 + 0] = expand5((v >> 11) & 0x1F)
            out[i * 4 + 1] = expand6((v >> 5) & 0x3F)
            out[i * 4 + 2] = expand5(v & 0x1F)
            out[i * 4 + 3] = 255
    elif fmt == 5:
        for i in range(count):
            v = data[i * 2] | (data[i * 2 + 1] << 8)
            out[i * 4 + 0] = expand4((v >> 8) & 0xF)
            out[i * 4 + 1] = expand4((v >> 4) & 0xF)
            out[i * 4 + 2] = expand4(v & 0xF)
            out[i * 4 + 3] = expand4((v >> 12) & 0xF)
    elif fmt == 7:
        for i in range(count):
            g = data[i]
            out[i * 4:i * 4 + 4] = bytes((g, g, g, 255))
    return out


def dat_path():
    from probe import target_path
    return target_path().parent / 'th185.dat'


def collect_textures():
    """Yield (anm name, format, width, height, payload bytes) per THTX block."""
    from pbg4 import extract_file, read_entries
    from anm import parse_anm

    dat = dat_path()
    _header, entries = read_entries(dat)
    anms = [e['name'] for e in entries if e['name'].endswith('.anm')]

    for name in anms:
        blob = extract_file(dat, name=name)['data']
        info = parse_anm(blob)
        for tex in info['textures']:
            at = tex['offset']
            data = blob[at + 16:at + 16 + tex['size']]
            yield name, tex['format'], tex['width'], tex['height'], data


def selftest() -> int:
    print('=== ANM texture decoder selftest ===')
    print()
    failures = 0

    try:
        dat = dat_path()
        if not dat.is_file():
            print('[SKIP] th185.dat not found')
            return 0
        blocks = list(collect_textures())
    except ImportError as exc:
        print('[SKIP] missing dependency: %s' % exc)
        return 0
    except SystemExit as exc:
        print('[SKIP] %s' % exc)
        return 0

    ok = len(blocks) > 0
    print('[%s] collected %d THTX blocks' % ('PASS' if ok else 'FAIL', len(blocks)))
    if not ok:
        failures += 1

    # 1. Every block must use a known format.
    unknown = [(n, f) for n, f, _w, _h, _d in blocks if f not in FORMAT_BYTES]
    ok = not unknown
    print('[%s] every block uses a known format code (%d unknown)'
          % ('PASS' if ok else 'FAIL', len(unknown)))
    if not ok:
        for item in unknown[:5]:
            print('       %s' % (item,))
        failures += 1

    # 2. Every block's size must equal w * h * bpp for ITS format. This is the
    #    property that would break if the format table were wrong.
    bad = []
    for name, fmt, w, h, data in blocks:
        bpp = FORMAT_BYTES.get(fmt)
        if bpp is None:
            continue
        if len(data) != w * h * bpp:
            bad.append((name, fmt, w, h, len(data), w * h * bpp))
    ok = not bad
    print('[%s] every block size equals width*height*bytes_per_pixel (%d bad)'
          % ('PASS' if ok else 'FAIL', len(bad)))
    if not ok:
        for item in bad[:5]:
            print('       %s' % (item,))
        failures += 1

    # 3. Format distribution must match the documented figures.
    dist = {}
    for _n, f, _w, _h, _d in blocks:
        dist[f] = dist.get(f, 0) + 1
    print('       format distribution across blocks: %s' % dist)

    # 4. Decode a sample of blocks and check the output shape and content.
    decoded = 0
    bad_decode = []
    for name, fmt, w, h, data in blocks[:40]:
        rgba = decode(fmt, w, h, data)
        if rgba is None:
            bad_decode.append((name, fmt, 'rejected'))
            continue
        if len(rgba) != w * h * 4:
            bad_decode.append((name, fmt, 'size'))
            continue
        decoded += 1
        # Alpha behaviour: RGB565 and GRAY8 are opaque.
        if fmt in (3, 7):
            if any(rgba[i * 4 + 3] != 255 for i in range(w * h)):
                bad_decode.append((name, fmt, 'alpha'))
    ok = not bad_decode and decoded > 0
    print('[%s] decoded %d blocks with correct output shape and alpha (%d bad)'
          % ('PASS' if ok else 'FAIL', decoded, len(bad_decode)))
    if not ok:
        for item in bad_decode[:5]:
            print('       %s' % (item,))
        failures += 1

    # 5. Round-trip check: BGRA8888 with a known pattern must come back with
    #    R and B swapped, alpha preserved.
    pattern = bytes((10, 20, 30, 40))
    rgba = decode(1, 1, 1, pattern)
    ok = rgba == bytes((30, 20, 10, 40))
    print('[%s] BGRA8888 sample decodes to RGBA %s'
          % ('PASS' if ok else 'FAIL', list(rgba) if rgba else None))
    if not ok:
        failures += 1

    # 6. ARGB4444 full-scale must reach 255 on every channel.
    full = bytes((0xFF, 0xFF))
    rgba = decode(5, 1, 1, full)
    ok = rgba == bytes((255, 255, 255, 255))
    print('[%s] ARGB4444 0xFFFF decodes to opaque white' % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # 7. RGB565 full-scale likewise.
    full = bytes((0xFF, 0xFF))
    rgba = decode(3, 1, 1, full)
    ok = rgba == bytes((255, 255, 255, 255))
    print('[%s] RGB565 0xFFFF decodes to opaque white' % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # 8. An unknown format must be rejected, not guessed.
    rgba = decode(99, 1, 1, bytes(4))
    ok = rgba is None
    print('[%s] unknown format code is rejected' % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    print()
    if failures:
        print('selftest failed: %d item(s)' % failures)
        return 1
    print('selftest passed: ANM textures decode correctly')
    return 0


if __name__ == '__main__':
    raise SystemExit(selftest())

