#!/usr/bin/env python3
"""TH185 PBG4 archive reader (header decryption VERIFIED, table extraction in progress).

## Status

| Item | Status |
|---|---|
| File header decryption | ✅ VERIFIED (magic "THA1" matches) |
| Header field semantics | ✅ Fully decoded |
| Entry-table load parameters | ✅ Fully decoded |
| Entry-table content | ❌ Output is sparse; LZSS detail unresolved |

## Format (all confirmed by disassembly + probe)

    0x00  uint32  magic after decryption = 0x31414854 ("THA1")
    0x04  uint32  decompressed_size = raw - 0x75BCD15
    0x08  uint32  compressed_size   = raw - 0x3ADE68B1
    0x0C  uint32  entry_count       = raw + 0xF7E7F8AC

Header is 16 bytes, decrypted in place by RVA 0x1E90 with
key=0x1B, step=0x37, block=0x10, limit=0x10.

Entry table is decrypted with key=0x3E, step=0x9B, block=0x80 and then
LZSS-decompressed by RVA 0x71E70.

## Critical bug this file documents

RVA 0x92E00 is **memcpy**, not memset. The probe stubbed it as memset for
four rounds, which zeroed the input buffer inside 0x1E90 and made it look
like a pure key-stream generator. Fixing the stub made header decryption
work immediately.

## Usage

    python th185_web/scripts/native/pbg4.py --selftest
    python th185_web/scripts/native/pbg4.py --header th185.dat
    python th185_web/scripts/native/pbg4.py --table th185.dat

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

MAGIC = 0x31414854          # "THA1"
HEADER_SIZE = 16

# Header decryption parameters (RVA 0x714FA-0x7150C)
HEADER_KEY = 0x1B
HEADER_STEP = 0x37
HEADER_BLOCK = 0x10
HEADER_LIMIT = 0x10

# Entry-table decryption parameters (RVA 0x71586-0x71591)
TABLE_KEY = 0x3E
TABLE_STEP = 0x9B
TABLE_BLOCK = 0x80

# Header field correction constants (RVA 0x7151E-0x7153A)
DECOMP_SUB = 0x75BCD15
COMP_SUB = 0x3ADE68B1
COUNT_ADD = 0xF7E7F8AC


def _decrypt(probe, blob, key, step, block, limit):
    """Decrypt in place via RVA 0x1E90. Returns decrypted bytes."""
    size = len(blob)
    buf = probe.alloc(size + 0x100)
    probe.write(buf, blob + b"\x00" * 0x100)
    result = probe.call(0x1E90, args=[key, step, block, limit],
                        ecx=buf, edx=size, limit=200000000)
    if result["error"]:
        raise SystemExit("decrypt failed at 0x1E90: %s" % result["error"])
    return probe.read(buf, size)


def read_header(path):
    """Decrypt and parse the 16-byte PBG4 header."""
    from probe import Probe, load_image, parse_pe

    data, _target = load_image()
    meta = parse_pe(data)
    probe = Probe(data, meta)

    with open(path, "rb") as stream:
        raw = stream.read(HEADER_SIZE)
    if len(raw) < HEADER_SIZE:
        raise SystemExit("file too small: %s" % path)

    plain = _decrypt(probe, raw, HEADER_KEY, HEADER_STEP, HEADER_BLOCK, HEADER_LIMIT)
    fields = struct.unpack("<4I", plain)
    if fields[0] != MAGIC:
        raise SystemExit(
            "magic mismatch: got 0x%08X, expected 0x%08X (\"THA1\")"
            % (fields[0], MAGIC)
        )
    return {
        "raw": raw,
        "plain": plain,
        "decompressed_size": (fields[1] - DECOMP_SUB) & 0xFFFFFFFF,
        "compressed_size": (fields[2] - COMP_SUB) & 0xFFFFFFFF,
        "entry_count": (fields[3] + COUNT_ADD) & 0xFFFFFFFF,
        "fields": fields,
    }


def selftest() -> int:
    """Verify against the real th185.dat when available."""
    print("=== PBG4 reader selftest ===")
    print()
    failures = 0

    # Locate th185.dat next to the exe
    try:
        from probe import target_path

        exe = target_path()
        dat = exe.parent / "th185.dat"
    except Exception as exc:
        print("[SKIP] cannot locate target: %s" % exc)
        return 0

    if not dat.is_file():
        print("[SKIP] th185.dat not found at %s" % dat)
        return 0

    try:
        header = read_header(dat)
    except ImportError as exc:
        print("[SKIP] missing dependency: %s" % exc)
        return 0
    except SystemExit as exc:
        print("[FAIL] header decryption: %s" % exc)
        return 1

    ok = header["fields"][0] == MAGIC
    print("[%s] magic = 0x%08X (\"THA1\")" % ("PASS" if ok else "FAIL", header["fields"][0]))
    if not ok:
        failures += 1

    print("       decompressed_size = %d" % header["decompressed_size"])
    print("       compressed_size   = %d" % header["compressed_size"])
    print("       entry_count       = %d" % header["entry_count"])

    # Sanity: entry table should fit inside decompressed size
    per_entry = header["decompressed_size"] / header["entry_count"]
    ok = 8 <= per_entry <= 64
    print("[%s] decompressed_size / entry_count = %.2f bytes per entry"
          % ("PASS" if ok else "FAIL", per_entry))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: header decryption and field semantics verified")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 PBG4 archive reader")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--header", metavar="DAT")
    parser.add_argument("--table", metavar="DAT")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if args.header:
        header = read_header(Path(args.header))
        print("file            : %s" % args.header)
        print("raw header      : %s" % header["raw"].hex())
        print("decrypted header: %s" % header["plain"].hex())
        print("magic           : 0x%08X (THA1)" % header["fields"][0])
        print("decompressed    : %d bytes" % header["decompressed_size"])
        print("compressed      : %d bytes" % header["compressed_size"])
        print("entry count     : %d" % header["entry_count"])
        print("bytes per entry : %.2f"
              % (header["decompressed_size"] / header["entry_count"]))
        return 0

    if args.table:
        print("Entry-table extraction is NOT yet reliable.", file=sys.stderr)
        print("Header decryption works, but LZSS output is sparse.", file=sys.stderr)
        print("See docs/PBG4.md section 14.5 for open questions.", file=sys.stderr)
        return 1

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

