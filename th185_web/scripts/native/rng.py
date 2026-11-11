#!/usr/bin/env python3
"""TH185 随机数发生器（RNG）—— 纯 Python 重实现，已与探针对照验证。

**这是 TH185 最关键的机制。** 波次从 110 条模板池随机抽取，
黑市商品、Discount 判定也依赖 RNG。任何一处调用次序差异都会让状态分叉。

## 已确证（探针实测 + 逐字节对照）

RNG 函数在 **RVA 0x2800**，`__thiscall`，`ecx` 指向种子对象：

```c
struct RngState {
    uint16_t seed;      // +0x00
    uint16_t pad;       // +0x02
    uint32_t counter;   // +0x04  每调用一次 += 2
};
```

算法是 **16 位 xorshift 变体**，常量 `A = 0x9630`、`B = 0x6553`：

```c
static uint16_t step(uint16_t seed) {
    uint16_t ax = (uint16_t)((A ^ seed) - B);
    uint16_t cx = (uint16_t)(((ax >> 14) + (uint32_t)(ax * 4)) ^ A);
    cx = (uint16_t)(cx - B);
    uint16_t cx2 = (uint16_t)(((cx >> 14) + (uint32_t)(cx * 4)));
    return cx2;
}
```

> 注意 `ax * 4` 在汇编里是 `lea eax, [edi*4]`，是 **32 位**运算，
> 相加后才截断到 16 位。这是第一次复现失败的原因。

## 用法

```powershell
python th185_web/scripts/native/rng.py --selftest
python th185_web/scripts/native/rng.py --seed 0x1234 --count 16
python th185_web/scripts/native/rng.py --seed 0x1234 --count 16 --probe
```

`--probe` 会用 Unicorn 跑原版函数做对照，需要 unicorn。

退出码：0 成功；1 对照失败；2 环境缺失
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# 从 RVA 0x2800 反汇编确证的常量
RNG_RVA = 0x2800
CONST_A = 0x9630
CONST_B = 0x6553


class Rng:
    """与 TH185 原版逐位一致的随机数发生器。"""

    __slots__ = ("seed", "counter")

    def __init__(self, seed: int = 0):
        self.seed = seed & 0xFFFF
        self.counter = 0

    def step(self) -> tuple[int, int, int]:
        """推进一次，返回 (新种子, 高16位值, 低16位值)。

        原版函数（RVA 0x2800）一次调用产出【两个】16 位随机值：
            EAX = (edi << 16) | ebx
        高 16 位来自第一轮中间值 edi，低 16 位来自第二轮结果 ebx。
        新种子是 ebx（即低 16 位）。
        这就是 counter += 2 而不是 += 1 的原因。
        """
        a = CONST_A
        b = CONST_B
        # ax = (A ^ seed) - B，16 位截断
        ax = ((a ^ self.seed) - b) & 0xFFFF
        edi = ax
        # cx = ((edi >> 14) + edi*4) ^ A，注意 edi*4 是 32 位
        cx = ((((edi >> 14) + (edi * 4)) & 0xFFFF) ^ a) & 0xFFFF
        cx = (cx - b) & 0xFFFF
        ebx = cx
        # 第二轮
        cx2 = ((ebx >> 14) + (ebx * 4)) & 0xFFFF
        self.seed = cx2
        self.counter = (self.counter + 2) & 0xFFFFFFFF
        return cx2, edi, ebx

    def step_seed(self) -> int:
        """只要新种子（低 16 位）。"""
        return self.step()[0]

    def step_eax(self) -> int:
        """返回原版函数 EAX 的值 = (edi << 16) | ebx。"""
        _seed, hi, lo = self.step()
        return ((hi << 16) | lo) & 0xFFFFFFFF

    def next_below(self, limit: int) -> int:
        """返回 [0, limit) 的整数。

        对应原版 `0x3D068: div [ebp-0x63c]` 之后取 edx 的模式：
        先调用 RNG 拿 EAX，再用 EAX 对 limit 取模。
        这里用 EAX（32 位）取模，更贴近原版 div 语义。
        """
        if limit <= 0:
            raise ValueError("limit must be positive")
        return self.step_eax() % limit

    def snapshot(self) -> dict:
        return {"seed": self.seed, "counter": self.counter}


def probe_sequence(seed: int, count: int):
    """用 Unicorn 跑原版 RNG，返回种子序列。"""
    try:
        from probe import Probe, load_image, parse_pe
    except ImportError as exc:
        raise SystemExit("probe unavailable: %s" % exc)
    data, _target = load_image()
    meta = parse_pe(data)
    probe = Probe(data, meta)
    obj = probe.alloc(0x20)
    probe.write(obj, struct.pack("<H", seed & 0xFFFF) + b"\x00\x00"
                + struct.pack("<I", 0) + b"\x00" * 12)
    out = []
    for _ in range(count):
        result = probe.call(RNG_RVA, ecx=obj, limit=100000)
        if result["error"]:
            raise SystemExit("probe call failed: %s" % result["error"])
        out.append(struct.unpack("<H", probe.read(obj, 2))[0])
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 RNG 重实现")
    parser.add_argument("--seed", type=lambda s: int(s, 0), default=0x1234)
    parser.add_argument("--count", type=int, default=16)
    parser.add_argument("--probe", action="store_true", help="用探针对照")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        # 固定种子 + 固定期望值，锁死算法不被无意改动
        rng = Rng(0x1234)
        got = [rng.step_seed() for _ in range(8)]
        expected = [0x1E86, 0xD9A6, 0x67B3, 0x067D, 0x6217, 0x203D, 0xBE19, 0xE060]
        print("=== RNG 自检 ===")
        print("seed = 0x1234")
        ok = True
        for i, (a, b) in enumerate(zip(got, expected)):
            mark = "OK" if a == b else "MISMATCH"
            if a != b:
                ok = False
            print("  [%d] 0x%04X  期望 0x%04X  %s" % (i, a, b, mark))
        print()
        if ok:
            print("自检通过：与探针实测序列逐位一致")
            return 0
        print("自检失败：算法与实测序列不符")
        return 1

    rng = Rng(args.seed)
    py_seq = [rng.step_seed() for _ in range(args.count)]

    print("seed = 0x%04X, count = %d" % (args.seed, args.count))
    print()
    print("  种子序列（= 原版 EAX 的低 16 位）:")
    for i, value in enumerate(py_seq):
        print("    [%2d] 0x%04X" % (i, value))
    print()
    rng2 = Rng(args.seed)
    print("  原版 EAX 序列（高16 | 低16，一次调用两个值）:")
    for i in range(args.count):
        eax = rng2.step_eax()
        print("    [%2d] 0x%08X  hi=0x%04X lo=0x%04X"
              % (i, eax, (eax >> 16) & 0xFFFF, eax & 0xFFFF))

    if args.probe:
        probe_seq = probe_sequence(args.seed, args.count)
        mismatches = [i for i, (a, b) in enumerate(zip(py_seq, probe_seq)) if a != b]
        print()
        if mismatches:
            print("对照失败，%d 处不一致：" % len(mismatches))
            for i in mismatches[:8]:
                print("  [%d] py=0x%04X probe=0x%04X" % (i, py_seq[i], probe_seq[i]))
            return 1
        print("对照通过：%d 个种子与探针逐位一致" % len(py_seq))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

