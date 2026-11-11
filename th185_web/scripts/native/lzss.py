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

WINDOW_SIZE = 4096
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

    对应原版 RVA 0x71E70。窗口是一个 4096 字节的环形缓冲，
    写指针 `dst_pos` 同时用作环形索引（`and 0x1FFF`）。
    """
    reader = BitReader(src)
    window = bytearray(WINDOW_SIZE)
    out = bytearray()
    dst_pos = 0

    while len(out) < dst_size:
        if reader.exhausted():
            break
        if reader.bit() == 0:
            # 字面量：8 位
            literal = reader.bits(8)
            out.append(literal)
            window[dst_pos & WINDOW_MASK] = literal
            dst_pos += 1
        else:
            # 匹配：8 位长度 + 12 位偏移
            length = reader.bits(8)
            if length == 0:
                # 原版：长度 0 时跳出（`test edi,edi; je 0x7204a`）
                break
            length += 2
            offset = reader.bits(12)
            for i in range(length):
                if len(out) >= dst_size:
                    break
                src_index = (offset + i) & WINDOW_MASK
                byte = window[src_index]
                out.append(byte)
                window[dst_pos & WINDOW_MASK] = byte
                dst_pos += 1
    return bytes(out)


def selftest() -> int:
    """用构造数据验证解压器逻辑。不需要原版 exe。"""
    print("=== LZSS 解压器自检 ===")
    print()
    failures = 0

    # 1. 空输入应产出空输出
    out = decompress(b"", 100)
    ok = out == b""
    print("[%s] 空输入 -> 空输出（实得 %d 字节）" % ("PASS" if ok else "FAIL", len(out)))
    if not ok:
        failures += 1

    # 2. 纯字面量：每个字面量 = flag(0) + 8 位数据
    # 构造 3 个字面量 A B C
    bits = []
    for ch in b"ABC":
        bits.append(0)              # flag = 字面量
        for i in range(7, -1, -1):  # 8 位，高位先
            bits.append((ch >> i) & 1)
    # 打包成字节
    packed = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for j in range(8):
            byte = (byte << 1) | (bits[i + j] if i + j < len(bits) else 0)
        packed.append(byte)
    out = decompress(bytes(packed), 3)
    ok = out == b"ABC"
    print("[%s] 3 个字面量 -> %r（期望 b'ABC'）" % ("PASS" if ok else "FAIL", out))
    if not ok:
        failures += 1

    # 3. 字面量 + 匹配
    # 先输出 "ABC"，再用匹配复制（offset=0, length=3）得到 "ABCABC"
    # 注意：长度字段 0 = 终止符；字段 1 -> 解码长度 3（最小匹配）
    bits = []
    for ch in b"ABC":
        bits.append(0)
        for i in range(7, -1, -1):
            bits.append((ch >> i) & 1)
    bits.append(1)                      # flag = 匹配
    for i in range(7, -1, -1):
        bits.append((1 >> i) & 1)       # 长度字段 = 1 -> 解码 1+2 = 3
    for i in range(11, -1, -1):
        bits.append((0 >> i) & 1)       # offset = 0
    packed = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for j in range(8):
            byte = (byte << 1) | (bits[i + j] if i + j < len(bits) else 0)
        packed.append(byte)
    out = decompress(bytes(packed), 6)
    ok = out == b"ABCABC"
    print("[%s] 字面量+匹配 -> %r（期望 b'ABCABC'）" % ("PASS" if ok else "FAIL", out))
    if not ok:
        failures += 1

    # 4. 长度字段 0 = 终止符
    bits = []
    bits.append(0)                      # 字面量 'X'
    for i in range(7, -1, -1):
        bits.append((ord("X") >> i) & 1)
    bits.append(1)                      # flag = 匹配
    for i in range(7, -1, -1):
        bits.append(0)                  # 长度字段 = 0 -> 终止
    packed = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for j in range(8):
            byte = (byte << 1) | (bits[i + j] if i + j < len(bits) else 0)
        packed.append(byte)
    out = decompress(bytes(packed), 100)
    ok = out == b"X"
    print("[%s] 长度字段 0 -> 终止，输出 %r（期望 b'X'）" % ("PASS" if ok else "FAIL", out))
    if not ok:
        failures += 1

    print()
    if failures:
        print("自检失败 %d 项" % failures)
        return 1
    print("自检通过：解压器逻辑正确（注意：尚未用真实 PBG4 数据验证）")
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

