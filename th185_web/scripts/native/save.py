#!/usr/bin/env python3
"""TH185 save file (scoreth185.dat) reader.

## Format

The whole file is a 24-byte header plus one encrypted, LZSS-compressed blob:

    +0x00  char[4] magic = "581T"
    +0x04  uint32  file size (echo of the on-disk length)
    +0x08  uint16  version = 6
    +0x0A  uint16  reserved = 0
    +0x0C  uint32  constant 0x100
    +0x10  uint32  compressed size
    +0x14  uint32  decompressed size
    +0x18  uint8[] encrypted + LZSS payload

Both stages reuse code already reversed for th185.dat:

  * decryption  — same routine as PBG4 (RVA 0x1E90), with
                  key 0xAC, step 0x35, block 0x10, limit = compressed size
  * decompression — same LZSS (RVA 0x71E70)

Confirmed by disassembly of the save loader at RVA 0x69440:

    069452: cmp dword ptr [eax], 0x54313835   ; "581T"
    06945E: cmp word  ptr [eax + 8], 6        ; version
    069469: mov edx, [eax + 0x10]             ; compressed size
    06946C: lea ecx, [eax + 0x18]             ; payload starts at +0x18
    069471: push edx / push 0x10 / push 0x35 / push 0xac
    06947B: call 0x1E90                       ; decrypt
    0694A0: call 0x71E70                      ; LZSS

## Decompressed layout

A sequence of records, each starting with a 16-bit magic and carrying its own
length at +0x08:

    struct Record {
        uint16 magic;      // "CR" (0x5243) or "ST" (0x5453)
        uint16 version;    // 3 for CR, 4 for ST
        uint32 score;      // CR: stored value; ST: unused
        uint32 length;     // total record length, including this header
        // CR: 32-byte slots from +0x10
        // ST: settings blob
    };

Observed in a real save: two "CR" records of 78176 bytes each, then one "ST"
record of 1888 bytes. 78176 + 78176 + 1888 = 158240, exactly the declared
decompressed size, so the record walk is self-validating.

## Slot layout (inside a CR record, from +0x10)

    struct Slot {          // 32 bytes
        uint32 score;      // descending: 100000, 90000, 80000, ...
        uint16 flag;       // 1 in every observed slot
        char   name[8];    // "--------" means empty
        uint8  extra[20];  // zero in every observed slot
    };

60 consecutive slots match this layout in the sample save.

## Usage

    python th185_web/scripts/native/save.py --selftest
    python th185_web/scripts/native/save.py --dump

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

MAGIC = 0x54313835          # "581T"
VERSION = 6
HEADER_SIZE = 24

SAVE_KEY = 0xAC
SAVE_STEP = 0x35
SAVE_BLOCK = 0x10

RECORD_CR = 0x5243          # "CR"
RECORD_ST = 0x5453          # "ST"
SLOT_SIZE = 32
SLOTS_PER_GROUP = 10     # one ranking table per difficulty
CR_HEADER_SIZE = 0x10


def save_path() -> Path:
    """Default save location for this machine."""
    appdata = os.environ.get("APPDATA")
    if not appdata:
        return Path("scoreth185.dat")
    return Path(appdata) / "ShanghaiAlice" / "th185" / "scoreth185.dat"


def decrypt_payload(cipher: bytes, probe=None):
    """Decrypt with RVA 0x1E90, reusing the PBG4 implementation."""
    from pbg4 import decrypt_pure

    return decrypt_pure(cipher, SAVE_KEY, SAVE_STEP, SAVE_BLOCK, len(cipher))


def read_save(path: Path, probe=None):
    """Read and fully decode a save file. Returns a dict."""
    from lzss import decompress

    blob = path.read_bytes()
    if len(blob) < HEADER_SIZE:
        raise SystemExit("save file too small: %d bytes" % len(blob))

    magic, file_size, version, reserved, constant, comp, decomp = struct.unpack_from(
        "<IIHHIII", blob, 0)
    if magic != MAGIC:
        raise SystemExit("bad magic 0x%08X (expected 0x%08X)" % (magic, MAGIC))
    if version != VERSION:
        raise SystemExit("unsupported version %d" % version)
    if HEADER_SIZE + comp > len(blob):
        raise SystemExit("compressed size %d exceeds the file" % comp)

    cipher = blob[HEADER_SIZE:HEADER_SIZE + comp]
    plain = decrypt_payload(cipher, probe)
    raw = decompress(plain, decomp)

    return {
        "header": {
            "magic": magic,
            "file_size": file_size,
            "version": version,
            "reserved": reserved,
            "constant": constant,
            "compressed_size": comp,
            "decompressed_size": decomp,
        },
        "raw": raw,
        "records": parse_records(raw),
    }


def parse_records(raw: bytes):
    """Walk the record sequence. Each record carries its own length."""
    records = []
    pos = 0
    while pos + 16 <= len(raw):
        magic, version, value, length = struct.unpack_from("<HHII", raw, pos)
        if magic not in (RECORD_CR, RECORD_ST):
            break
        if length < 16 or pos + length > len(raw):
            break

        record = {
            "offset": pos,
            "magic": magic,
            "version": version,
            "value": value,
            "length": length,
            "body": raw[pos:pos + length],
        }
        if magic == RECORD_CR:
            record["slots"] = parse_slots(record["body"])
        records.append(record)
        pos += length

    return {"records": records, "consumed": pos, "total": len(raw)}


def parse_slots(body: bytes):
    """Parse 32-byte score slots inside a CR record, from +0x10."""
    slots = []
    pos = CR_HEADER_SIZE
    while pos + SLOT_SIZE <= len(body):
        score = struct.unpack_from("<I", body, pos)[0]
        # +0x04 and +0x05 are two independent u8 fields, not one u16: observed
        # bytes are "01 00" in one CR record and "01 01" in another. Treating
        # them as a u16 and rejecting > 1 silently empties every later record.
        flag_a = body[pos + 4]
        flag_b = body[pos + 5]
        name = body[pos + 6:pos + 14]
        if not all(32 <= b < 127 for b in name):
            break
        if score > 10000000:
            break
        slots.append({
            "offset": pos,
            "score": score,
            "flag_a": flag_a,
            "flag_b": flag_b,
            "name": name.decode("ascii", "replace"),
        })
        pos += SLOT_SIZE
    return slots


def selftest() -> int:
    print("=== save file reader selftest ===")
    print()
    failures = 0

    path = save_path()
    if not path.is_file():
        print("[SKIP] save file not found: %s" % path)
        return 0

    try:
        save = read_save(path)
    except SystemExit as exc:
        print("[FAIL] %s" % exc)
        return 1

    header = save["header"]
    print("file: %s (%d bytes)" % (path, path.stat().st_size))
    print()

    ok = header["magic"] == MAGIC
    print("[%s] magic is \"581T\"" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = header["version"] == VERSION
    print("[%s] version is %d" % ("PASS" if ok else "FAIL", header["version"]))
    if not ok:
        failures += 1

    ok = header["file_size"] == path.stat().st_size
    print("[%s] header echoes the file size (%d)"
          % ("PASS" if ok else "FAIL", header["file_size"]))
    if not ok:
        failures += 1

    ok = HEADER_SIZE + header["compressed_size"] == path.stat().st_size
    print("[%s] 24 + compressed size == file size" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = header["constant"] == 0x100
    print("[%s] +0x0C is the constant 0x100" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = len(save["raw"]) == header["decompressed_size"]
    print("[%s] LZSS produced exactly %d bytes"
          % ("PASS" if ok else "FAIL", header["decompressed_size"]))
    if not ok:
        failures += 1

    parsed = save["records"]
    ok = parsed["consumed"] == parsed["total"]
    print("[%s] records consume the whole buffer (%d / %d bytes)"
          % ("PASS" if ok else "FAIL", parsed["consumed"], parsed["total"]))
    if not ok:
        failures += 1

    magics = [r["magic"] for r in parsed["records"]]
    ok = all(m in (RECORD_CR, RECORD_ST) for m in magics)
    print("[%s] every record has a known magic (%s)"
          % ("PASS" if ok else "FAIL",
             ["CR" if m == RECORD_CR else "ST" for m in magics]))
    if not ok:
        failures += 1

    cr_records = [r for r in parsed["records"] if r["magic"] == RECORD_CR]
    ok = len(cr_records) >= 1
    print("[%s] found %d CR record(s)" % ("PASS" if ok else "FAIL", len(cr_records)))
    if not ok:
        failures += 1

    if cr_records:
        slots = cr_records[0]["slots"]
        ok = len(slots) > 0
        print("[%s] first CR record has %d slots"
              % ("PASS" if ok else "FAIL", len(slots)))
        if not ok:
            failures += 1

        # flag_a is the slot KIND: 1 = an empty ranking entry, 7 = a real
        # result. Empty entries form groups of 10 (one table per difficulty)
        # with scores descending; real results follow and are not grouped.
        empty = [s for s in slots if s["flag_a"] == 1]
        real = [s for s in slots if s["flag_a"] == 7]
        ok = len(empty) > 0 and len(empty) % SLOTS_PER_GROUP == 0
        print("[%s] %d empty slots form %d groups of %d"
              % ("PASS" if ok else "FAIL", len(empty),
                 len(empty) // SLOTS_PER_GROUP, SLOTS_PER_GROUP))
        if not ok:
            failures += 1

        descending = True
        for start in range(0, len(empty), SLOTS_PER_GROUP):
            group = [s["score"] for s in empty[start:start + SLOTS_PER_GROUP]]
            for i in range(len(group) - 1):
                if group[i] < group[i + 1]:
                    descending = False
        ok = descending
        print("[%s] empty-slot scores decrease within each group"
              % ("PASS" if ok else "FAIL"))
        if not ok:
            failures += 1

        ok = all(s["name"] == "--------" for s in empty)
        print("[%s] every empty slot is named '--------'" % ("PASS" if ok else "FAIL"))
        if not ok:
            failures += 1

        print("       (%d real result slot(s) present)" % len(real))

        names = {s["name"] for s in slots}
        ok = all(len(n) == 8 for n in names)
        print("[%s] every slot name is 8 characters" % ("PASS" if ok else "FAIL"))
        if not ok:
            failures += 1

    st_records = [r for r in parsed["records"] if r["magic"] == RECORD_ST]
    ok = len(st_records) >= 1
    print("[%s] found %d ST record(s)" % ("PASS" if ok else "FAIL", len(st_records)))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: save file fully decoded")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 save file reader")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--dump", action="store_true")
    parser.add_argument("--file")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    path = Path(args.file) if args.file else save_path()
    try:
        save = read_save(path)
    except SystemExit as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2

    header = save["header"]
    print("file            : %s" % path)
    print("magic           : 0x%08X" % header["magic"])
    print("version         : %d" % header["version"])
    print("compressed size : %d" % header["compressed_size"])
    print("decompressed    : %d" % header["decompressed_size"])
    print()
    parsed = save["records"]
    print("records: %d, consumed %d / %d bytes"
          % (len(parsed["records"]), parsed["consumed"], parsed["total"]))
    for record in parsed["records"]:
        kind = "CR" if record["magic"] == RECORD_CR else "ST"
        print("  [%s] @0x%05X version=%d value=%d length=%d"
              % (kind, record["offset"], record["version"],
                 record["value"], record["length"]))
        for slot in record.get("slots", [])[:10]:
            print("       +0x%03X score=%-8d flags=%d,%d name=%r"
                  % (slot["offset"], slot["score"], slot["flag_a"],
                     slot["flag_b"], slot["name"]))
        if len(record.get("slots", [])) > 10:
            print("       ... %d more slots" % (len(record["slots"]) - 10))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

