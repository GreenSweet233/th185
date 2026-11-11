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

    对应原版 RVA 0x71E70。经差分测试（喂可控输入、观察输出）确认：

        flag = 1  -> 字面量：读 8 位数据
        flag = 0  -> 匹配：读 13 位偏移，偏移为 0 则结束；
                          否则读 8 位长度字段，拷贝 (字段 + 3) 字节

    反汇编证据：
        0x71F3B: test esi,esi ; je 0x71eef   flag=0 -> 匹配分支(13位偏移)
        0x71F3F: mov esi,0x80                 flag=1 -> 8 位字面量
        0x71F7E: mov byte [esi], al           字面量输出
        0x71FB5: test edi,edi ; je 0x7204a    偏移 0 -> 结束
        0x71FFB: add edi, 2                   长度 = 字段 + 2
        0x72032: cmp esi,edi ; jle 0x72013    循环 edi+1 次 => 实际长度 = 字段+3

    窗口 8192 字节（and 0x1fff），初始写指针 1（0x71EB0: mov [ebp-0x1c],1）。
    位序 MSB 优先（掩码从 0x80 递减，0x71F69: shr bl,1）。
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
            # 匹配：13 位偏移 + 8 位长度字段
            offset = reader.bits(13)
            if offset == 0:
                break
            length = reader.bits(8) + 3
            for i in range(length):
                if len(out) >= dst_size:
                    break
                byte = window[(offset + i) & WINDOW_MASK]
                out.append(byte)
                window[dst_pos & WINDOW_MASK] = byte
                dst_pos = (dst_pos + 1) & WINDOW_MASK
    return bytes(out)


def selftest() -> int:
    """用构造数据验证解压器逻辑。不需要原版 exe。"""
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

    # 1. 空输入应产出空输出
    out = decompress(b"", 100)
    ok = out == b""
    print("[%s] 空输入 -> 空输出（实得 %d 字节）" % ("PASS" if ok else "FAIL", len(out)))
    if not ok:
        failures += 1

    # 2. 纯字面量：flag=1 + 8 位数据
    bits = []
    for ch in b"ABC":
        bits.append(1)                  # flag = 字面量
        for i in range(7, -1, -1):
            bits.append((ch >> i) & 1)
    out = decompress(pack(bits), 3)
    ok = out == b"ABC"
    print("[%s] 3 个字面量 -> %r（期望 b'ABC'）" % ("PASS" if ok else "FAIL", out))
    if not ok:
        failures += 1

    # 3. 字面量 + 匹配
    # 窗口写指针从 1 开始，所以第 1 个字面量落在 window[1]。
    # 匹配偏移取 window[offset+i]，offset=1 指向 'A'。
    bits = []
    for ch in b"ABC":
        bits.append(1)
        for i in range(7, -1, -1):
            bits.append((ch >> i) & 1)
    bits.append(0)                      # flag = 匹配
    for i in range(12, -1, -1):
        bits.append((1 >> i) & 1)       # 13 位偏移 = 1 -> window[1] = 'A'
    for i in range(7, -1, -1):
        bits.append((0 >> i) & 1)       # 长度字段 0 -> 拷贝 0+3 = 3 字节
    out = decompress(pack(bits), 6)
    ok = out == b"ABCABC"
    print("[%s] 字面量+匹配 -> %r（期望 b'ABCABC'）" % ("PASS" if ok else "FAIL", out))
    if not ok:
        failures += 1

    # 4. 偏移 0 = 终止符
    bits = []
    bits.append(1)                      # 字面量 'X'
    for i in range(7, -1, -1):
        bits.append((ord("X") >> i) & 1)
    bits.append(0)                      # flag = 匹配
    for i in range(12, -1, -1):
        bits.append(0)                  # 13 位偏移 = 0 -> 终止
    out = decompress(pack(bits), 100)
    ok = out == b"X"
    print("[%s] 偏移 0 -> 终止，输出 %r（期望 b'X'）" % ("PASS" if ok else "FAIL", out))
    if not ok:
        failures += 1

    # 5. 对照原版实测: 输入 0x81 0x00 -> 输出首字节 0x02
    out = decompress(bytes([0x81, 0x00]), 4)
    ok = len(out) >= 1 and out[0] == 0x02
    print("[%s] 原版对照 0x8100 -> 首字节 %s（期望 0x02）"
          % ("PASS" if ok else "FAIL", ("0x%02X" % out[0]) if out else "无"))
    if not ok:
        failures += 1

    # 6. 对照原版实测: 输入 0xFF 0x00 -> 输出首字节 0xFE
    out = decompress(bytes([0xFF, 0x00]), 4)
    ok = len(out) >= 1 and out[0] == 0xFE
    print("[%s] 原版对照 0xFF00 -> 首字节 %s（期望 0xFE）"
          % ("PASS" if ok else "FAIL", ("0x%02X" % out[0]) if out else "无"))
    if not ok:
        failures += 1

    print()
    if failures:
        print("自检失败 %d 项" % failures)
        return 1
    print("自检通过：解压器逻辑与原版差分测试一致")
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

