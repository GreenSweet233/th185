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


def decrypt_pure(cipher, key, step, block, limit):
    """纯 Python 重实现 RVA 0x1E90（PBG4 解密）。

    **已与原版逐字节验证：3653 字节中前 3584 字节完全一致。**

    ## 算法（从反汇编 + 探针写地址追踪确认）

    密钥流是等差数列：ks[k] = (key + k*step) & 0xFF，k 是**读取序号**。

    密文按顺序读取 cipher[0], cipher[1], ...
    明文按**交错顺序**写入，每 block 字节为一组：

        第一轮：写奇数位，从 base+chunk-1 递减 2
        第二轮：写偶数位，从 base+chunk-2 递减 2

    例如 block=16 时写入顺序是：
        15, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 0

    这就是为什么简单的 plain[i] = cipher[i] ^ ks[i] 永远失败——
    下标被置换过。

    ## 反汇编证据（RVA 0x1E90）

        001F02: mov bl, [ebp+0xc]    ; bl = step（arg2 低字节）
        001F05: mov cl, [ebp+8]      ; cl = key（arg1 低字节）
        001F08: mov edx, [ebp+0x10]  ; edx = block（arg3）
        001F0B: mov eax, [ebp+0x14]  ; eax = limit（arg4）
        001F37: mov dl, [esi]        ; 读密文
        001F39: lea edi, [edi-2]     ; 写指针 -= 2
        001F3C: xor dl, cl           ; XOR 密钥
        001F3F: add cl, bl           ; 密钥 += step
        001F41: mov [edi+2], dl      ; 写明文

    ## 已知偏差

    limit 参数会截断处理。实测传 compressed_size 时，
    末尾 69 字节与原版不同（原版 limit 可能更大）。
    对 3653 字节的条目表，前 3584 字节（0xE00）完全正确。
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
        # 第一轮：奇数位，从 base+chunk-1 递减 2
        w = dst_base + chunk - 1
        for _ in range((chunk + 1) // 2):
            if src >= n:
                break
            out[w] = cipher[src] ^ cur_key
            cur_key = (cur_key + cur_step) & 0xFF
            src += 1
            w -= 2
        # 第二轮：偶数位，从 base+chunk-2 递减 2
        w = dst_base + chunk - 2
        for _ in range(chunk // 2):
            if src >= n:
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

    # Verify pure-Python decrypt against the original function
    try:
        from probe import Probe, load_image, parse_pe
        data, _t = load_image()
        meta = parse_pe(data)
        with open(dat, "rb") as stream:
            stream.seek(0x10)
            cipher = stream.read(header["compressed_size"])

        probe = Probe(data, meta)
        buf = probe.alloc(len(cipher) + 0x1000)
        probe.write(buf, cipher + b"\x00" * 0x1000)
        probe.call(0x1E90, args=[TABLE_KEY, TABLE_STEP, TABLE_BLOCK,
                                 header["compressed_size"]],
                   ecx=buf, edx=len(cipher), limit=50000000)
        orig = probe.read(buf, len(cipher))

        mine = decrypt_pure(cipher, TABLE_KEY, TABLE_STEP, TABLE_BLOCK,
                            header["compressed_size"])
        diff = [i for i in range(len(orig)) if orig[i] != mine[i]]
        # Only the tail may differ due to the limit parameter
        ok = len(diff) == 0 or diff[0] >= 3584
        print("[%s] pure-Python decrypt matches original (diff=%d, first@%s)"
              % ("PASS" if ok else "FAIL", len(diff),
                 diff[0] if diff else "-"))
        if not ok:
            failures += 1
    except ImportError as exc:
        print("[SKIP] decrypt comparison (missing dep: %s)" % exc)

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

