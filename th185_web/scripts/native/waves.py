#!/usr/bin/env python3
"""TH185 波次模板表提取器。

从 th185.exe 的静态数据里读出波次模板名表（111 项），这是 TH185
「波次随机」机制的索引表。

## 已确证

名表在 **VA `0x4B5D50`**（RVA `0xB5D50`），111 个 DWORD 指针：

| 索引 | 名称 | 数量 | 含义 |
|---|---|---|---|
| 0 | （空） | 1 | 占位，表示「不选」 |
| 1 | Boss01t | 1 | 教程 BOSS |
| 2..28 | Boss01..Boss27 | 27 | 常规 BOSS |
| 29..31 | Wave01t..Wave03t | 3 | 教程道中波次 |
| 32..110 | Wave01..Wave79 | 79 | 常规道中波次 |

**合计 1 + 1 + 27 + 3 + 79 = 111。**

> 注意与调研阶段的差异：调研说「110 条模板池」，实测是 **111 项**
> （索引 0..110），其中索引 0 是空占位。实际可选模板 = 110 条。
> 调研说「28 个 BOSS 编号」，实测 BOSS 名 = 1 教程 + 27 常规 = 28 个。
> **两个数字都对，只是计数口径不同。**

## 权重数组

权重不在静态数据里，而是运行时构建：

| 地址 | 用途 |
|---|---|
| `ENEMY_MANAGER(0x4D7AF4) + 0x4E0` | 权重数组 A（Boss Rush 模式，`0x3D02F`） |
| `ENEMY_MANAGER(0x4D7AF4) + 0x324` | 权重数组 B（常规模式，`0x3D0B8`） |
| `ENEMY_MANAGER(0x4D7AF4) + 0x4F4C` | 要跳过的索引 |

## 抽取算法（RVA 0x3CFFF - 0x3D0E6）

```c
// 第一遍：累加权重，同时跳过 skipIndex
total_a = 0; total_b = 0;
for (i = 0; i < 0x6F; i++) {          // 0x6F = 111
    if (i != skip_index) {
        total_a += weights_a[i];      // [ecx - 0x1BC]，即 +0x4E0 - 0x1BC = +0x324
        total_b += weights_b[i];      // [ecx]
    }
}

// RNG 取模
roll = rng_next() % total;

// 第二遍：减去权重直到为负
for (i = 0; i < 0x6F; i++) {
    if (i != skip_index) {
        roll -= weights[i];
        if (roll < 0) break;          // 命中 i
    }
}
```

**注意**：两遍循环都跳过了 `skip_index`，所以同一个模板不会连续出现两次。

## 用法

```powershell
python th185_web/scripts/native/waves.py --list
python th185_web/scripts/native/waves.py --json
python th185_web/scripts/native/waves.py --selftest
```

退出码：0 成功；1 自检失败；2 环境缺失
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# 从反汇编确证的地址
NAME_TABLE_VA = 0x4B5D50
IMAGE_BASE = 0x400000
TABLE_COUNT = 111          # 0x6F

# 期望的分布（用于自检，锁死表结构不被误读）
EXPECTED = {
    "empty": [0],
    "tutorial_boss": [1],
    "boss": list(range(2, 29)),       # Boss01..Boss27
    "tutorial_wave": [29, 30, 31],    # Wave01t..Wave03t
    "wave": list(range(32, 111)),     # Wave01..Wave79
}


def load_table():
    """从 exe 的静态数据读名表。"""
    from probe import load_image, parse_pe

    data, _target = load_image()
    meta = parse_pe(data)

    def rva_to_off(rva):
        for s in meta["sections"]:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rsize"]):
                return s["roff"] + (rva - s["vaddr"])
        return -1

    base = rva_to_off(NAME_TABLE_VA - IMAGE_BASE)
    if base < 0:
        raise SystemExit("name table VA 0x%X is not inside any section" % NAME_TABLE_VA)

    names = []
    for i in range(TABLE_COUNT):
        ptr = struct.unpack_from("<I", data, base + i * 4)[0]
        if ptr == 0:
            names.append("")
            continue
        off = rva_to_off(ptr - IMAGE_BASE)
        if off < 0:
            names.append("")
            continue
        end = off
        while data[end] != 0 and end - off < 128:
            end += 1
        names.append(data[off:end].decode("ascii", "replace"))
    return names


def classify(names):
    """按名称模式分类，返回各类索引。"""
    groups = {"empty": [], "tutorial_boss": [], "boss": [],
              "tutorial_wave": [], "wave": [], "other": []}
    for i, name in enumerate(names):
        if not name:
            groups["empty"].append(i)
        elif name.endswith("t"):
            if name.startswith("Boss"):
                groups["tutorial_boss"].append(i)
            elif name.startswith("Wave"):
                groups["tutorial_wave"].append(i)
            else:
                groups["other"].append(i)
        elif name.startswith("Boss"):
            groups["boss"].append(i)
        elif name.startswith("Wave"):
            groups["wave"].append(i)
        else:
            groups["other"].append(i)
    return groups


# ---------- 权重抽取算法（已反汇编确证，见 docs/WAVES.md §4） ----------


def pick_index(weights, total, roll, skip_index=-1):
    """按原版两遍循环逻辑从权重数组里选一个索引。

    对应 RVA 0x3D080 - 0x3D0A4 的第二遍循环：
        for (i = 0; i < 111; i++) {
            if (i != skip_index) {
                roll -= weights[i];
                if ((int32_t)roll < 0) return i;
            }
        }
        return -1;

    参数 total 与 roll 由调用方算好（total 是权重总和，roll 是 RNG 取模结果）。
    """
    if total <= 0:
        return -1
    remaining = roll
    for i in range(len(weights)):
        if i == skip_index:
            continue
        remaining -= weights[i]
        if remaining < 0:
            return i
    return -1


def sum_weights(weights, skip_index=-1):
    """对应 RVA 0x3D017 - 0x3D044 的第一遍循环。"""
    total = 0
    for i, w in enumerate(weights):
        if i == skip_index:
            continue
        total += w
    return total


def pick_wave(weights, rng, skip_index=-1):
    """完整抽取：累加 -> RNG 取模 -> 递减。返回 (index, roll, total)。"""
    total = sum_weights(weights, skip_index)
    if total == 0:
        return -1, 0, 0
    roll = rng.next_below(total)
    index = pick_index(weights, total, roll, skip_index)
    return index, roll, total


def selftest_algorithm() -> int:
    """用构造数据验证抽取算法的边界与分布。不需要原版 exe。"""
    print("=== 权重抽取算法自检 ===")
    print()
    failures = 0

    # 1. 均匀权重：每个索引都应可达
    w = [1] * 111
    total = sum_weights(w)
    ok = total == 111
    print("[%s] 均匀权重总和 = %d" % ("PASS" if ok else "FAIL", total))
    if not ok:
        failures += 1

    # 2. roll=0 应命中第一个非跳过项
    idx = pick_index(w, total, 0)
    ok = idx == 0
    print("[%s] roll=0 -> index=%d（期望 0）" % ("PASS" if ok else "FAIL", idx))
    if not ok:
        failures += 1

    # 3. roll=total-1 应命中最后一项
    idx = pick_index(w, total, total - 1)
    ok = idx == 110
    print("[%s] roll=total-1 -> index=%d（期望 110）" % ("PASS" if ok else "FAIL", idx))
    if not ok:
        failures += 1

    # 4. skip_index 生效：roll=0 且 skip 0 -> 应命中 1
    idx = pick_index(w, total, 0, skip_index=0)
    ok = idx == 1
    print("[%s] skip=0 时 roll=0 -> index=%d（期望 1）" % ("PASS" if ok else "FAIL", idx))
    if not ok:
        failures += 1

    # 5. 跳过项的权重不计入总和
    w2 = [5] * 111
    total_skip = sum_weights(w2, skip_index=3)
    ok = total_skip == 5 * 110
    print("[%s] skip=3 时总和 = %d（期望 %d）" % ("PASS" if ok else "FAIL", total_skip, 5 * 110))
    if not ok:
        failures += 1

    # 6. 零权重项永远不会被选中
    w3 = [0] * 111
    w3[42] = 10
    total3 = sum_weights(w3)
    hits = set()
    for roll in range(total3):
        hits.add(pick_index(w3, total3, roll))
    ok = hits == {42}
    print("[%s] 只有 42 号有权重时，所有 roll 都命中 42（实际 %s）"
          % ("PASS" if ok else "FAIL", sorted(hits)))
    if not ok:
        failures += 1

    # 7. 分布正确性：权重 1:3 的两项，命中比应接近 1:3
    w4 = [0] * 111
    w4[10] = 1
    w4[20] = 3
    total4 = sum_weights(w4)
    counts = {10: 0, 20: 0}
    for roll in range(total4):
        counts[pick_index(w4, total4, roll)] += 1
    ok = counts == {10: 1, 20: 3}
    print("[%s] 权重 1:3 的分布 = %s（期望 {10: 1, 20: 3}）"
          % ("PASS" if ok else "FAIL", counts))
    if not ok:
        failures += 1

    # 8. 与 RNG 联动的确定性
    from rng import Rng
    r1 = Rng(0x1234)
    r2 = Rng(0x1234)
    seq1 = [pick_wave(w, r1)[0] for _ in range(20)]
    seq2 = [pick_wave(w, r2)[0] for _ in range(20)]
    ok = seq1 == seq2
    print("[%s] 相同种子产生相同序列" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1
    print("       前 10 次抽取: %s" % seq1[:10])

    print()
    if failures:
        print("自检失败 %d 项" % failures)
        return 1
    print("自检通过：抽取算法逻辑正确")
    return 0



def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 波次模板表提取器")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--selftest", action="store_true", help="验证表结构")
    parser.add_argument("--selftest-algo", action="store_true", help="验证抽取算法（不需要 exe）")
    args = parser.parse_args()

    if args.selftest_algo:
        return selftest_algorithm()

    try:
        names = load_table()
    except ImportError as exc:
        print("error: %s (需要 probe.py 的同目录依赖)" % exc, file=sys.stderr)
        return 2

    groups = classify(names)

    if args.selftest:
        print("=== 波次表自检 ===")
        print("名表 VA 0x%X, 项数 %d" % (NAME_TABLE_VA, len(names)))
        print()
        failures = 0
        for key, expected in EXPECTED.items():
            actual = groups[key]
            ok = actual == expected
            if not ok:
                failures += 1
            print("[%s] %-14s 期望 %d 项 %s"
                  % ("PASS" if ok else "FAIL", key, len(expected),
                     "" if ok else "实际 %s" % actual[:6]))
        if groups["other"]:
            print("[FAIL] other 非空: %s" % [names[i] for i in groups["other"]])
            failures += 1
        print()
        total = sum(len(v) for v in groups.values())
        print("合计 %d 项 = 空 %d + 教程BOSS %d + BOSS %d + 教程波 %d + 常规波 %d"
              % (total, len(groups["empty"]), len(groups["tutorial_boss"]),
                 len(groups["boss"]), len(groups["tutorial_wave"]), len(groups["wave"])))
        print()
        if failures:
            print("自检失败 %d 项" % failures)
            return 1
        print("自检通过：表结构与预期一致")
        return 0

    if args.json:
        payload = {"table_va": "0x%X" % NAME_TABLE_VA, "count": len(names),
                   "names": names, "groups": {k: v for k, v in groups.items() if v}}
        print(json.dumps(payload, indent=2, ensure_ascii=False))
        return 0

    print("波次模板表 @ VA 0x%X（%d 项）" % (NAME_TABLE_VA, len(names)))
    print()
    print("  %-6s %-12s %s" % ("索引", "名称", "分类"))
    kind_of = {}
    for key, idxs in groups.items():
        for i in idxs:
            kind_of[i] = key
    for i, name in enumerate(names):
        print("  [%3d] %-12s %s" % (i, name if name else "(空)", kind_of.get(i, "?")))
    print()
    print("合计: 空 %d + 教程BOSS %d + BOSS %d + 教程波 %d + 常规波 %d = %d"
          % (len(groups["empty"]), len(groups["tutorial_boss"]),
             len(groups["boss"]), len(groups["tutorial_wave"]),
             len(groups["wave"]), len(names)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

