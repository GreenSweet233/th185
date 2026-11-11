#!/usr/bin/env python3
"""TH185 难度系统：难度档位、分数上限表、全局访问器。

## 已确证

### 1. 难度档位（8 档）

难度索引在 **0x4D1038**（uint32，值 0..7）。
名称来自 exe 字符串实测 + thprac 的 difficulties[8] 数组。

| 索引 | 名称 |
|---|---|
| 0 | Very Easy |
| 1 | Easy |
| 2 | Normal |
| 3 | Little Hard |
| 4 | Hard |
| 5 | Very Hard |
| 6 | Lunatic |
| 7 | Over Drive |

注意：Little Hard（索引 3）是 TH185 独有的档位，TH18 没有。

### 2. 分数上限表（5 项）

在 RVA 0x3107C 处初始化，来源是常量 VA 0x4BCB00（16 字节）+ 立即数：

    03107C: movaps xmm0, xmmword ptr [0x4bcb00]      ; 前 4 项
    031083: movups xmmword ptr [ebp - 0x18], xmm0
    031087: mov dword ptr [ebp - 8], 0xf4240          ; 第 5 项
    03109A: mov eax, dword ptr [0x4d1038]             ; 难度索引
    0310A1: mov eax, dword ptr [ebp + eax*4 - 0x18]   ; 查表
    0310A5: imul eax, dword ptr [0x4d1004]            ; x 关卡号
    0310AC: mov dword ptr [edi + 0x7c], eax           ; 存入对象

| 索引 | 值 |
|---|---|
| 0 | 500000 |
| 1 | 1000000 |
| 2 | 1500000 |
| 3 | 2000000 |
| 4 | 1000000 |

这张表只有 5 项但难度有 8 档，说明它不是「每档难度的分数上限」。
用途待确认，从 imul [0x4d1004] 看可能是「本关分数上限」的基数。

### 3. 全局访问器函数族（RVA 0x3E735 - 0x3E7C5）

这些是极短的 getter，每个 3-4 条指令。见 ACCESSORS 表。

关键：只有 is_difficulty_0..3 四个判断函数，没有 4..7。
说明游戏逻辑只对前 4 档做了特殊分支，5..7 走通用路径。
这解释了为什么分数上限表只有 5 项。

## 用法

    python th185_web/scripts/native/difficulty.py --list
    python th185_web/scripts/native/difficulty.py --selftest

退出码：0 成功；1 自检失败；2 环境缺失
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

IMAGE_BASE = 0x400000

DIFFICULTIES = [
    "Very Easy", "Easy", "Normal", "Little Hard",
    "Hard", "Very Hard", "Lunatic", "Over Drive",
]

SCORE_CAP_CONST_VA = 0x4BCB00
SCORE_CAP_TAIL = 0xF4240

ACCESSORS = {
    0x3E735: ("get_4d1040", "returns [0x4D1040]"),
    0x3E740: ("get_difficulty", "returns [0x4D1038], difficulty index 0..7"),
    0x3E74B: ("get_ecx_6224", "returns [ecx+0x6224]"),
    0x3E757: ("is_difficulty_0", "difficulty == 0 (Very Easy)"),
    0x3E768: ("is_difficulty_1", "difficulty == 1 (Easy)"),
    0x3E77A: ("is_difficulty_2", "difficulty == 2 (Normal)"),
    0x3E78C: ("is_difficulty_3", "difficulty == 3 (Little Hard)"),
    0x3E79E: ("get_em_3c", "returns [EM+0x3C]"),
    0x3E7AC: ("get_em_40", "returns [EM+0x40]"),
    0x3E7BA: ("get_em_44", "returns [EM+0x44]"),
    0x3E7C8: ("get_em_4f44", "returns [EM+0x4F44]"),
}


def load_score_caps():
    """Read the score-cap table from the exe constant area."""
    from probe import load_image, parse_pe

    data, _target = load_image()
    meta = parse_pe(data)

    def rva_to_off(rva):
        for s in meta["sections"]:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rsize"]):
                return s["roff"] + (rva - s["vaddr"])
        return -1

    off = rva_to_off(SCORE_CAP_CONST_VA - IMAGE_BASE)
    if off < 0:
        raise SystemExit("constant VA 0x%X not in any section" % SCORE_CAP_CONST_VA)

    values = [struct.unpack_from("<I", data, off + i * 4)[0] for i in range(4)]
    values.append(SCORE_CAP_TAIL)
    return values


def selftest() -> int:
    print("=== difficulty system selftest ===")
    print()
    failures = 0

    ok = len(DIFFICULTIES) == 8
    print("[%s] difficulty count = %d (expect 8)" % ("PASS" if ok else "FAIL", len(DIFFICULTIES)))
    if not ok:
        failures += 1

    ok = DIFFICULTIES[3] == "Little Hard"
    print("[%s] index 3 = %s (expect Little Hard)" % ("PASS" if ok else "FAIL", DIFFICULTIES[3]))
    if not ok:
        failures += 1

    ok = DIFFICULTIES[-1] == "Over Drive"
    print("[%s] index 7 = %s (expect Over Drive)" % ("PASS" if ok else "FAIL", DIFFICULTIES[-1]))
    if not ok:
        failures += 1

    checks = [k for k, v in ACCESSORS.items() if v[0].startswith("is_difficulty_")]
    ok = len(checks) == 4
    print("[%s] difficulty predicates = %d (expect 4, only 0..3)"
          % ("PASS" if ok else "FAIL", len(checks)))
    if not ok:
        failures += 1

    try:
        caps = load_score_caps()
        expected = [500000, 1000000, 1500000, 2000000, 1000000]
        ok = caps == expected
        print("[%s] score cap table = %s" % ("PASS" if ok else "FAIL", caps))
        if not ok:
            print("       expect %s" % expected)
            failures += 1
    except ImportError as exc:
        print("[SKIP] score cap table (missing dep: %s)" % exc)

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: difficulty system matches expectations")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 difficulty system")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if args.list:
        print("=== difficulty tiers (index at [0x4D1038]) ===")
        for i, name in enumerate(DIFFICULTIES):
            print("  [%d] %s" % (i, name))
        print()
        print("=== score cap table (const VA 0x%X + immediate) ===" % SCORE_CAP_CONST_VA)
        try:
            caps = load_score_caps()
            for i, v in enumerate(caps):
                print("  [%d] %d" % (i, v))
        except (ImportError, SystemExit) as exc:
            print("  (unavailable: %s)" % exc)
        print()
        print("=== global accessors ===")
        for rva in sorted(ACCESSORS):
            name, desc = ACCESSORS[rva]
            print("  0x%X  %-18s %s" % (rva, name, desc))
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

