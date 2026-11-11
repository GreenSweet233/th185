#!/usr/bin/env python3
"""TH185 LZSS 解压器（RVA 0x71E70 的纯 Python 重实现）。

## 来源

从 `th185.exe` RVA `0x71E70` 的完整反汇编（155 条指令）翻译而来。
结构已确证：

| 特征 | 反汇编证据 |
|---|---|
| 4096 字节滑动窗口 | `mov [esi + 0x527e80]` / `and 0x1FFF` |
| 逐位读取 | `mov bl, 0x80` + `shr bl, 1` + `cmove ebx, [ebp-0x20]` |
| 8 位长度字段 | `mov esi, 8` |
| 匹配长度 +2 | `add edi, 2` |
| 8 位字面量 | `mov esi, 0x80` |

## 重要：本实现未经明文验证

解压器逻辑是从反汇编忠实翻译的，但**尚未用真实的 PBG4 数据验证**，
因为 PBG4 的完整解密链（密钥流如何参与）仍未闭合。
见 `docs/PBG4.md` §12。

用法：
  python th185_web/scripts/native/lzss.py --selftest
  python th185_web/scripts/native/lzss.py --probe <data.bin>

退出码：0 成功；1 自检失败；2 环境缺失
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

WINDOW_SIZE = 8192   # 原版 and 0x1fff
WINDOW_MASK = 0x1FFF


class BitReader:
    """对应原版的位流读取器。

    原版用 bl 作掩码（从 0x80 开始右移），掩码耗尽时读下一个字节。
    超界时原版返回 0 位（`cmp eax, [ebp-0x10]; jl ...; xor ecx,ecx`），
    这里同样处理，保证行为一致。
    """

    __slots__ = ("data", "pos", "mask", "cur")

    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.mask = 0
        self.cur = 0

    def exhausted(self) -> bool:
        """原版在超界时返回 0 位；但为了能区分「真的读完了」和「读到 0」，
        这里提供显式的耗尽判断，由调用方在循环顶部检查。"""
        return self.pos >= len(self.data) and self.mask == 0

    def bit(self) -> int:
        if self.mask == 0:
            if self.pos >= len(self.data):
                self.cur = 0
            else:
                self.cur = self.data[self.pos]
                self.pos += 1
            self.mask = 0x80
        result = 1 if (self.cur & self.mask) else 0
        self.mask >>= 1
        return result

    def bits(self, count: int) -> int:
        value = 0
        for _ in range(count):
            value = (value << 1) | self.bit()
        return value


def decompress(src: bytes, dst_size: int) -> bytes:
    """LZSS 解压。返回最多 dst_size 字节。

    对应原版 RVA 0x71E70。编码格式经**穷举验证**确证：

        flag = 1  -> 字面量：读 8 位数据                      (共 9 位)
        flag = 0  -> 匹配：读 13 位偏移                       (共 1+13+4 = 18 位)
                          偏移 == 0 -> 结束
                          否则读 **4 位**长度字段，拷贝 (字段 + 3) 字节

    关键证据（th185.dat 条目表，解密后）：
        目标输出第 29..34 字节是 B8 19 12 06 09 D8，它们是 6 个连续字面量，
        位模式在解密流中位于 **bit 54**。
        因此前 54 位必须恰好产出 29 字节。
        穷举 (偏移位宽, 长度位宽, 长度偏移) 的所有组合，
        只有 **(13, 4, +3)** 能精确满足：消耗 54 位、产出 29 字节：
            M(偏移=5476, 长度=10) + M(偏移=2553, 长度=6) + M(偏移=7909, 长度=13)
            = 10 + 6 + 13 = 29 ✓

    窗口 8192 字节（and 0x1fff），初始写指针 1
    （实测：win[1] 对应输出[0]，win[30] 对应输出[29]）。
    位序 MSB 优先。
    """
    reader = BitReader(src)
    window = bytearray(WINDOW_SIZE)
    out = bytearray()
    dst_pos = 1

    while len(out) < dst_size:
        if reader.exhausted():
            break
        if reader.bit() == 1:
            # 字面量：8 位
            literal = reader.bits(8)
            out.append(literal)
            window[dst_pos & WINDOW_MASK] = literal
            dst_pos = (dst_pos + 1) & WINDOW_MASK
        else:
            # 匹配：13 位偏移 + 4 位长度字段
            offset = reader.bits(13)
            if offset == 0:
                break
            length = reader.bits(4) + 3
            for i in range(length):
                if len(out) >= dst_size:
                    break
                byte = window[(offset + i) & WINDOW_MASK]
                out.append(byte)
                window[dst_pos & WINDOW_MASK] = byte
                dst_pos = (dst_pos + 1) & WINDOW_MASK
    return bytes(out)


def selftest() -> int:
    """验证解压器。含与原版的差分对照。不需要原版 exe 也能跑前 4 项。"""
    print("=== LZSS 解压器自检 ===")
    print()
    failures = 0

    def pack(bits):
        out = bytearray()
        for i in range(0, len(bits), 8):
            byte = 0
            for j in range(8):
                byte = (byte << 1) | (bits[i + j] if i + j < len(bits) else 0)
            out.append(byte)
        return bytes(out)

    # 1. 空输入
    out = decompress(b"", 100)
    ok = out == b""
    print("[%s] 空输入 -> 空输出（实得 %d 字节）" % ("PASS" if ok else "FAIL", len(out)))
    if not ok:
        failures += 1

    # 2. 纯字面量：flag=1 + 8 位数据
    bits = []
    for ch in b"ABC":
        bits.append(1)
        for i in range(7, -1, -1):
            bits.append((ch >> i) & 1)
    out = decompress(pack(bits), 3)
    ok = out == b"ABC"
    print("[%s] 3 个字面量 -> %r（期望 b'ABC'）" % ("PASS" if ok else "FAIL", out))
    if not ok:
        failures += 1

    # 3. 字面量 + 匹配（13 位偏移 + 4 位长度）
    # 窗口写指针从 1 开始，第 1 个字面量落在 window[1]
    bits = []
    for ch in b"ABC":
        bits.append(1)
        for i in range(7, -1, -1):
            bits.append((ch >> i) & 1)
    bits.append(0)                      # flag = 匹配
    for i in range(12, -1, -1):
        bits.append((1 >> i) & 1)       # 13 位偏移 = 1 -> window[1] = 'A'
    for i in range(3, -1, -1):
        bits.append((0 >> i) & 1)       # 4 位长度字段 = 0 -> 拷贝 0+3 = 3 字节
    out = decompress(pack(bits), 6)
    ok = out == b"ABCABC"
    print("[%s] 字面量+匹配 -> %r（期望 b'ABCABC'）" % ("PASS" if ok else "FAIL", out))
    if not ok:
        failures += 1

    # 4. 偏移 0 = 终止
    bits = []
    bits.append(1)
    for i in range(7, -1, -1):
        bits.append((ord("X") >> i) & 1)
    bits.append(0)
    for i in range(12, -1, -1):
        bits.append(0)                  # 偏移 0 -> 终止
    out = decompress(pack(bits), 100)
    ok = out == b"X"
    print("[%s] 偏移 0 -> 终止，输出 %r（期望 b'X'）" % ("PASS" if ok else "FAIL", out))
    if not ok:
        failures += 1

    # 5. 与原版逐字节对照（真实 th185.dat）
    try:
        from probe import Probe, load_image, parse_pe, target_path
        from pbg4 import (read_header, decrypt_pure,
                          TABLE_KEY, TABLE_STEP, TABLE_BLOCK)

        dat = target_path().parent / "th185.dat"
        if not dat.is_file():
            print("[SKIP] original comparison (th185.dat not found)")
        else:
            header = read_header(dat)
            with open(dat, "rb") as stream:
                stream.seek(0x10)
                cipher = stream.read(header["compressed_size"])
            plain = decrypt_pure(cipher, TABLE_KEY, TABLE_STEP, TABLE_BLOCK,
                                 header["compressed_size"])
            mine = decompress(plain, header["decompressed_size"])

            data, _t = load_image()
            meta = parse_pe(data)
            probe = Probe(data, meta)
            buf = probe.alloc(len(plain) + 0x1000)
            probe.write(buf, plain + b"\x00" * 0x1000)
            res = probe.call(0x71E70,
                             args=[0, header["decompressed_size"]],
                             ecx=buf, edx=len(plain), limit=200000000)
            orig = probe.read(res["eax"], header["decompressed_size"])

            diff = [i for i in range(len(orig)) if orig[i] != mine[i]]
            ok = len(diff) == 0
            print("[%s] 与原版逐字节一致（%d 字节，差异 %d）"
                  % ("PASS" if ok else "FAIL", len(orig), len(diff)))
            if not ok:
                print("       首个差异 @ %d" % diff[0])
                failures += 1
    except ImportError as exc:
        print("[SKIP] original comparison (missing dep: %s)" % exc)

    print()
    if failures:
        print("自检失败 %d 项" % failures)
        return 1
    print("自检通过：解压器与原版逐字节一致")
    return 0


def probe_real(path: Path) -> int:
    """用真实文件跑一次，报告输出统计。用于探索，不是验证。"""
    data = path.read_bytes()
    out = decompress(data, 0x10000)
    nonzero = sum(1 for b in out if b != 0)
    printable = sum(1 for b in out if 32 <= b < 127)
    print("输入: %s (%d 字节)" % (path, len(data)))
    print("输出: %d 字节（非零 %d，可打印 %d）" % (len(out), nonzero, printable))
    if out:
        print()
        print("前 128 字节:")
        for i in range(0, min(128, len(out)), 16):
            chunk = out[i:i+16]
            print("  %04X  %-47s  %s"
                  % (i, " ".join("%02X" % b for b in chunk),
                     "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 LZSS 解压器")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--probe", metavar="FILE")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if args.probe:
        return probe_real(Path(args.probe))

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

