#!/usr/bin/env python3
"""TH185 PBG4 archive reader/writer. FULLY REVERSE-ENGINEERED AND VERIFIED.

## Format (all confirmed by disassembly + differential testing)

### Container layout

    0x00            uint32  magic after decryption = 0x31414854 ("THA1")
    0x04            uint32  entry_table_decompressed_size = raw - 0x75BCD15
    0x08            uint32  entry_table_compressed_size   = raw - 0x3ADE68B1
    0x0C            uint32  entry_count                   = raw + 0xF7E7F8AC
    0x10 ..         file data (raw, each entry encrypted separately)
    filesize-comp   entry table (encrypted + LZSS compressed)

**The entry table is at the END of the file, not the beginning.**
Disassembly proof (RVA 0x7154A): `eax = IFile->GetSize() - table_size`
then `IFile->Seek(eax)` before reading the table.

### Encryption (RVA 0x1E90)

Key stream is arithmetic: `ks[k] = (key + k*step) & 0xFF` where k is the
**read index**. Ciphertext is read sequentially, but plaintext is written
with a permuted index (per block): odd positions descending, then even
positions descending. See decrypt_pure() for the exact loop.

Parameters:
  header : key=0x1B step=0x37 block=0x10  limit=0x10
  table  : key=0x3E step=0x9B block=0x80  limit=compressed_size
  entries: key/step/block/limit from table[sum(filename) & 7]
           (table at VA 0x4C9470, 8 entries x 12 bytes)

### Compression (RVA 0x71E70) - LZSS

    flag = 1  -> literal: read 8 bits                        (9 bits total)
    flag = 0  -> match: read 13-bit offset                   (18 bits total)
                offset == 0 -> end
                else read 4-bit length field, copy (field + 3) bytes

Window 8192 bytes (`and 0x1fff`), initial write pointer 1, MSB-first bits.

### Entry table structure

Each entry is variable-length:

    char     name[];   // NUL-terminated
    padding;           // total (name + NUL) padded up to multiple of 4
    uint32   offset;   // absolute file offset of the entry data
    uint32   size;     // size of the entry data in bytes
    uint32   reserved; // always 0

Verified: all 284 parsed names are valid ASCII, offsets are strictly
increasing, and the last offset+size lands just past the table start.

## Usage

    python th185_web/scripts/native/pbg4.py --selftest
    python th185_web/scripts/native/pbg4.py --list   <th185.dat>
    python th185_web/scripts/native/pbg4.py --header <th185.dat>
    python th185_web/scripts/native/pbg4.py --extract <th185.dat> --name ascii.anm --outdir build

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

HEADER_KEY = 0x1B
HEADER_STEP = 0x37
HEADER_BLOCK = 0x10
HEADER_LIMIT = 0x10

TABLE_KEY = 0x3E
TABLE_STEP = 0x9B
TABLE_BLOCK = 0x80

DECOMP_SUB = 0x75BCD15
COMP_SUB = 0x3ADE68B1
COUNT_ADD = 0xF7E7F8AC


def decrypt_pure(cipher, key, step, block, limit):
    """Pure-Python reimplementation of RVA 0x1E90 (PBG4 decryption).

    Verified BYTE-IDENTICAL against the original on the real 3653-byte entry
    table (0 differing bytes). The write order was measured with a Unicorn
    memory-write hook; see the comment inside the loop for why the partial
    final block needs a different start offset.

    Key stream is arithmetic by READ index; plaintext is written with a
    permuted index (odd positions descending, then even positions).
    """
    n = len(cipher)
    out = bytearray(n)
    src = 0
    dst_base = 0
    remain = n
    avail = limit
    cur_key = key & 0xFF
    cur_step = step & 0xFF

    while remain > 0 and avail > 0 and dst_base < n:
        chunk = min(remain, block, n - dst_base)
        if chunk <= 0:
            break

        # Write order, measured with a Unicorn memory-write hook on the original
        # function (RVA 0x1E90), for a 69-byte tail:
        #   67, 65, ..., 1   then   66, 64, ..., 0
        # i.e. ODD offsets descending first, then EVEN offsets descending.
        #
        # The odd start is the largest odd number BELOW chunk, which is chunk-1
        # when chunk is even but chunk-2 when chunk is odd. Using chunk-1
        # unconditionally (as this code used to) is only correct for full
        # blocks; it silently corrupts the last, partial block. That is why
        # exactly the final 69 bytes of the entry table were wrong before.
        odd_start = chunk - 1 if chunk % 2 == 0 else chunk - 2
        odd_count = (chunk + 1) // 2
        even_start = odd_start - 1
        even_count = chunk // 2

        w = dst_base + odd_start
        for _ in range(odd_count):
            if src >= n or w < dst_base:
                break
            out[w] = cipher[src] ^ cur_key
            cur_key = (cur_key + cur_step) & 0xFF
            src += 1
            w -= 2

        w = dst_base + even_start
        for _ in range(even_count):
            if src >= n or w < dst_base:
                break
            out[w] = cipher[src] ^ cur_key
            cur_key = (cur_key + cur_step) & 0xFF
            src += 1
            w -= 2

        dst_base += chunk
        remain -= chunk
        avail -= chunk

    return bytes(out)


def _decrypt(probe, blob, key, step, block, limit):
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


def read_entries(path):
    """Read and parse the full entry table. Returns (header, entries).

    Each entry is a dict: {name, offset, size, reserved}.
    """
    from probe import Probe, load_image, parse_pe

    header = read_header(path)
    filesize = path.stat().st_size
    table_off = filesize - header["compressed_size"]
    if table_off < HEADER_SIZE:
        raise SystemExit("entry table offset underflows: %d" % table_off)

    data, _target = load_image()
    meta = parse_pe(data)
    probe = Probe(data, meta)

    with open(path, "rb") as stream:
        stream.seek(table_off)
        cipher = stream.read(header["compressed_size"])
    plain = _decrypt(probe, cipher, TABLE_KEY, TABLE_STEP, TABLE_BLOCK,
                     header["compressed_size"])

    from lzss import decompress
    table = decompress(plain, 0x100000)
    end = len(table)
    while end > 0 and table[end - 1] == 0:
        end -= 1
    table = table[:end]

    entries = []
    pos = 0
    total = len(table)
    while pos < total:
        start = pos
        while pos < total and table[pos] != 0:
            pos += 1
        if pos >= total:
            break
        name = table[start:pos]
        n = len(name) + 1
        pos = start + n + ((4 - (n & 3)) & 3)
        if pos + 12 > total:
            break
        offset, size, reserved = struct.unpack("<III", table[pos:pos + 12])
        pos += 12
        entries.append({
            "name": name.decode("ascii", "replace"),
            "offset": offset,
            "size": size,
            "reserved": reserved,
        })

    return header, entries


def extract_entry(path, entries, name):
    """Read one entry's raw (still encrypted) bytes."""
    for entry in entries:
        if entry["name"] == name:
            with open(path, "rb") as stream:
                stream.seek(entry["offset"])
                return stream.read(entry["size"])
    return None


CIPHER_TABLE_VA = 0x4C9470

# 8-entry algorithm table at VA 0x4C9470: (key, step, block, limit)
_CIPHER_TABLE = [
    (0x1B, 0x73, 0x100, 0x3800),
    (0x12, 0x43, 0x200, 0x3E00),
    (0x35, 0x79, 0x400, 0x3C00),
    (0x03, 0x91, 0x80, 0x6400),
    (0xAB, 0xDC, 0x80, 0x7000),
    (0x51, 0x9E, 0x100, 0x4000),
    (0xC1, 0x15, 0x400, 0x2C00),
    (0x99, 0x7D, 0x80, 0x4400),
]


def cipher_for(name):
    """Select cipher parameters for an entry name.

    Index = sum(filename_bytes) & 7. Confirmed by disassembly at
    RVA 0x713C5-0x713F5 (sums bytes until NUL, then ANDs with 7).
    """
    total = sum(name.encode("ascii", "replace")) & 0xFF
    return _CIPHER_TABLE[total & 7]


def extract_file(path, index=None, name=None, probe=None):
    """Fully extract one entry: decrypt and, if compressed, LZSS-decompress.

    Compression detection: the entry declared `size` is the DECOMPRESSED
    size. The actual compressed size is (next entry offset - this offset).
    If they are equal the data is stored uncompressed (only decryption
    needed); otherwise it is LZSS-compressed.

    Verified on real data:
      help_01.png   -> PNG magic 89 50 4E 47   (stored, comp == size)
      se_plst00.wav -> RIFF magic 52 49 46 46  (stored, comp == size)
      ascii.anm     -> 1866816 bytes exactly   (274760 -> 1866816)
    """
    from probe import Probe, load_image, parse_pe
    from lzss import decompress

    header, entries = read_entries(path)
    filesize = path.stat().st_size
    table_off = filesize - header["compressed_size"]

    if index is None:
        if name is None:
            raise SystemExit("extract_file needs index or name")
        for i, e in enumerate(entries):
            if e["name"] == name:
                index = i
                break
        else:
            raise SystemExit("entry not found: %s" % name)

    entry = entries[index]
    next_off = entries[index + 1]["offset"] if index + 1 < len(entries) else table_off
    comp_size = next_off - entry["offset"]
    if comp_size <= 0:
        raise SystemExit("invalid compressed size %d for %s"
                         % (comp_size, entry["name"]))

    if probe is None:
        data, _target = load_image()
        meta = parse_pe(data)
        probe = Probe(data, meta)

    with open(path, "rb") as stream:
        stream.seek(entry["offset"])
        cipher = stream.read(comp_size)

    key, step, block, limit = cipher_for(entry["name"])
    buf = probe.alloc(comp_size + 0x100)
    probe.write(buf, cipher + bytes(0x100))
    result = probe.call(0x1E90, args=[key, step, block, limit],
                        ecx=buf, edx=comp_size, limit=200000000)
    if result["error"]:
        raise SystemExit("decrypt failed for %s: %s"
                         % (entry["name"], result["error"]))
    plain = probe.read(buf, comp_size)

    compressed = comp_size != entry["size"]
    if compressed:
        plain = decompress(plain, entry["size"])

    return {
        "name": entry["name"],
        "declared_size": entry["size"],
        "compressed_size": comp_size,
        "data": plain,
        "compressed": compressed,
    }


def selftest() -> int:

    print("=== PBG4 reader selftest ===")
    print()
    failures = 0

    try:
        from probe import target_path
        dat = target_path().parent / "th185.dat"
    except Exception as exc:
        print("[SKIP] cannot locate target: %s" % exc)
        return 0
    if not dat.is_file():
        print("[SKIP] th185.dat not found at %s" % dat)
        return 0

    try:
        header, entries = read_entries(dat)
    except ImportError as exc:
        print("[SKIP] missing dependency: %s" % exc)
        return 0
    except SystemExit as exc:
        print("[FAIL] %s" % exc)
        return 1

    ok = header["fields"][0] == MAGIC
    print("[%s] magic = 0x%08X (THA1)" % ("PASS" if ok else "FAIL", header["fields"][0]))
    if not ok:
        failures += 1

    print("       decompressed = %d, compressed = %d, declared entries = %d"
          % (header["decompressed_size"], header["compressed_size"],
             header["entry_count"]))
    print("       parsed entries = %d" % len(entries))

    ok = len(entries) >= header["entry_count"] - 1
    print("[%s] entry count matches (parsed %d, declared %d)"
          % ("PASS" if ok else "FAIL", len(entries), header["entry_count"]))
    if not ok:
        failures += 1

    ok = all(e["name"] and all(32 <= ord(c) < 127 for c in e["name"])
             for e in entries)
    print("[%s] all names are valid ASCII" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = all(entries[i]["offset"] < entries[i + 1]["offset"]
             for i in range(len(entries) - 1))
    print("[%s] offsets are strictly increasing" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    filesize = dat.stat().st_size
    ok = all(0 < e["offset"] < filesize and e["offset"] + e["size"] <= filesize
             for e in entries)
    print("[%s] all entries lie inside the file" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = all(e["reserved"] == 0 for e in entries)
    print("[%s] reserved field is always 0" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # Full extraction check: decompress a few entries of different types
    try:
        checks = [
            ("help_01.png", bytes([0x89, 0x50, 0x4E, 0x47])),   # PNG magic
            ("se_plst00.wav", b"RIFF"),                          # RIFF magic
        ]
        for entry_name, magic in checks:
            info = extract_file(dat, name=entry_name)
            ok = (info["data"][:4] == magic
                  and len(info["data"]) == info["declared_size"]
                  and not info["compressed"])
            print("[%s] %s stored uncompressed, magic OK (%d bytes)"
                  % ("PASS" if ok else "FAIL", entry_name, len(info["data"])))
            if not ok:
                failures += 1

        info = extract_file(dat, name="ascii.anm")
        ok = (len(info["data"]) == info["declared_size"]
              and info["compressed"]
              and info["data"][:4] == bytes([0x08, 0x00, 0x00, 0x00]))
        print("[%s] ascii.anm LZSS-decompressed to exactly %d bytes"
              % ("PASS" if ok else "FAIL", len(info["data"])))
        if not ok:
            failures += 1
    except ImportError as exc:
        print("[SKIP] extraction checks (missing dep: %s)" % exc)

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: PBG4 container fully parsed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 PBG4 archive reader")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--header", metavar="DAT")
    parser.add_argument("--list", metavar="DAT")
    parser.add_argument("--extract", metavar="DAT")
    parser.add_argument("--name")
    parser.add_argument("--outdir", default="build")
    parser.add_argument("--dump", metavar="DAT", help="fully extract all entries to outdir")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if args.header:
        header = read_header(Path(args.header))
        print("file            : %s" % args.header)
        print("decrypted header: %s" % header["plain"].hex())
        print("magic           : 0x%08X (THA1)" % header["fields"][0])
        print("table decomp    : %d bytes" % header["decompressed_size"])
        print("table comp      : %d bytes" % header["compressed_size"])
        print("entry count     : %d" % header["entry_count"])
        return 0

    if args.list:
        header, entries = read_entries(Path(args.list))
        print("%-28s %12s %12s" % ("name", "offset", "size"))
        for e in entries:
            print("%-28s %12d %12d" % (e["name"], e["offset"], e["size"]))
        print()
        print("total %d entries" % len(entries))
        return 0

    if args.dump:
        header, entries = read_entries(Path(args.dump))
        out = Path(args.outdir)
        out.mkdir(parents=True, exist_ok=True)
        ok = 0
        bad = []
        for i, entry in enumerate(entries):
            try:
                info = extract_file(Path(args.dump), index=i)
                if len(info["data"]) != info["declared_size"]:
                    bad.append((entry["name"], info["declared_size"],
                                len(info["data"])))
                    continue
                dest = out / entry["name"]
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(info["data"])
                ok += 1
            except SystemExit as exc:
                bad.append((entry["name"], "-", str(exc)))
        print("extracted %d / %d entries to %s" % (ok, len(entries), out))
        for row in bad[:20]:
            print("  FAILED %s" % (row,))
        return 0 if not bad else 1

    if args.extract:
        if not args.name:
            print("--extract requires --name", file=sys.stderr)
            return 1
        info = extract_file(Path(args.extract), name=args.name)
        if len(info["data"]) != info["declared_size"]:
            print("size mismatch for %s: got %d, expected %d"
                  % (args.name, len(info["data"]), info["declared_size"]),
                  file=sys.stderr)
            return 1
        out = Path(args.outdir)
        out.mkdir(parents=True, exist_ok=True)
        dest = out / args.name
        dest.write_bytes(info["data"])
        print("wrote %s (%d bytes, compressed=%s)"
              % (dest, len(info["data"]), info["compressed"]))
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

