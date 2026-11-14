#!/usr/bin/env python3
"""TH185 卡牌表提取器。

从 th185.exe 的静态数据读出 87 张能力卡的名称与属性。

## 已确证

卡牌描述表在 **VA `0x4CA370`**（thprac 记录的 `CARD_DESC_LIST`），
每项 **17 个 DWORD（68 字节）**：

```c
struct CardEntry {           // 68 字节
    const char* name;        // +0x00  卡牌内部名（如 "PENDULUM"）
    uint32_t    id;          // +0x04  卡牌 ID（0..86）
    uint32_t    f2;          // +0x08
    uint32_t    f3;          // +0x0C
    uint32_t    f4;          // +0x10
    uint32_t    f5;          // +0x14
    uint32_t    f6;          // +0x18
    uint32_t    f7;          // +0x1C
    uint32_t    f8;          // +0x20
    uint32_t    f9;          // +0x24
    uint32_t    f10;         // +0x28
    uint32_t    f11;         // +0x2C
    uint32_t    cost;        // +0x30  ★ 可能是 Cost
    uint32_t    f13;         // +0x34
    uint32_t    f14;         // +0x38
    uint32_t    f15;         // +0x3C
    uint32_t    f16;         // +0x40
};
```

## 关键发现：ID 0 和 66

这两个 ID 是强制 BOSS 机制的触发条件（见 `docs/WAVES.md` §9）：

| ID | 内部名 | 触发 |
|---|---|---|
| **0** | `BLANK` | 关卡 5 持有 → 强制 Boss26 |
| **66** | `NITORI` | 关卡 7 持有 → 强制 Boss27 |

**这完全印证了 THBWiki 的剧情描述**：

> 「魔理沙装备**空白卡牌**后到守矢神社的祭典上找自己的合作者」
> 「找到**荷取**后，魔理沙从荷取手里获得了荷取制造的毁灭性的能力卡牌」

所以隐藏剧情的机制是：
1. 拿到 `BLANK`（空白卡牌）
2. 在**关卡 5** 带着它 → 强制遇到 Boss26
3. 推进后拿到 `NITORI`（荷取的卡）
4. 在**关卡 7** 带着它 → 强制遇到 Boss27

## 用法

```powershell
python th185_web/scripts/native/cards.py --list
python th185_web/scripts/native/cards.py --json
python th185_web/scripts/native/cards.py --id 0
python th185_web/scripts/native/cards.py --selftest
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

# 从反汇编 + thprac 确证的地址
CARD_DESC_LIST_VA = 0x4CA370
IMAGE_BASE = 0x400000
ENTRY_DWORDS = 17
MAX_CARDS = 90          # 实际 87，留余量

# 强制 BOSS 相关的两个 ID（docs/WAVES.md §9）
FORCE_BOSS_CARDS = {0: "BLANK", 66: "NITORI"}


def load_cards():
    """从 exe 静态数据读卡牌表。"""
    from probe import load_image, parse_pe

    data, _target = load_image()
    meta = parse_pe(data)

    def rva_to_off(rva):
        for s in meta["sections"]:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rsize"]):
                return s["roff"] + (rva - s["vaddr"])
        return -1

    def cstr_at(va):
        off = rva_to_off(va - IMAGE_BASE)
        if off < 0:
            return None
        end = off
        while data[end] != 0 and end - off < 80:
            end += 1
        try:
            text = data[off:end].decode("ascii")
        except UnicodeDecodeError:
            return None
        if not text or not all(32 <= ord(c) < 127 for c in text):
            return None
        return text

    base = rva_to_off(CARD_DESC_LIST_VA - IMAGE_BASE)
    if base < 0:
        raise SystemExit(
            "card table VA 0x%X is not inside any section" % CARD_DESC_LIST_VA
        )

    cards = []
    for index in range(MAX_CARDS):
        entry = base + index * ENTRY_DWORDS * 4
        name_ptr = struct.unpack_from("<I", data, entry)[0]
        name = cstr_at(name_ptr)
        if name is None:
            break
        fields = [
            struct.unpack_from("<I", data, entry + k * 4)[0]
            for k in range(ENTRY_DWORDS)
        ]
        cards.append(
            {
                "index": index,
                "id": fields[1],
                "name": name,
                "cost": fields[13],      # byte +0x34: the equip budget (11 tiers).
                            # fields[k] is at byte +0x04*k, so [13] is +0x34.
                            # The exposed "fields" list is fields[2:], so the
                            # SAME dword is c["fields"][11] -- two indexings of
                            # one table, which is why this was easy to confuse.
                "fields": fields[2:],
            }
        )
    return cards


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 卡牌表提取器")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--id", type=lambda s: int(s, 0), help="查特定 ID")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    try:
        cards = load_cards()
    except ImportError as exc:
        print("error: %s (需要同目录 probe.py)" % exc, file=sys.stderr)
        return 2

    if args.selftest:
        print("=== 卡牌表自检 ===")
        print("卡牌表 VA 0x%X，解析 %d 项" % (CARD_DESC_LIST_VA, len(cards)))
        print()
        failures = 0

        # 1. 数量应为 87（85 可用 + BLANK + BACK）
        ok = len(cards) == 87
        print("[%s] 项数 = %d（期望 87）" % ("PASS" if ok else "FAIL", len(cards)))
        if not ok:
            failures += 1

        # 2. ID 应覆盖 0..86 且无重复
        ids = sorted(c["id"] for c in cards)
        ok = ids == list(range(87))
        print("[%s] ID 覆盖 0..86 无重复（实得 %d 个，范围 %d..%d）"
              % ("PASS" if ok else "FAIL", len(ids), ids[0], ids[-1]))
        if not ok:
            failures += 1

        # 3. 关键 ID 必须是预期名称
        by_id = {c["id"]: c["name"] for c in cards}
        for card_id, expected in FORCE_BOSS_CARDS.items():
            actual = by_id.get(card_id)
            ok = actual == expected
            print("[%s] ID %d = %s（期望 %s）"
                  % ("PASS" if ok else "FAIL", card_id, actual, expected))
            if not ok:
                failures += 1

        # 4. 名称全部唯一
        names = [c["name"] for c in cards]
        ok = len(set(names)) == len(names)
        print("[%s] 名称全部唯一（%d 个）" % ("PASS" if ok else "FAIL", len(names)))
        if not ok:
            failures += 1

        print()
        if failures:
            print("自检失败 %d 项" % failures)
            return 1
        print("自检通过：卡牌表结构与关键 ID 均符合预期")
        return 0

    if args.id is not None:
        match = [c for c in cards if c["id"] == args.id]
        if not match:
            print("找不到 ID %d" % args.id, file=sys.stderr)
            return 1
        card = match[0]
        print("ID    : %d" % card["id"])
        print("名称  : %s" % card["name"])
        print("索引  : %d" % card["index"])
        print("cost? : %d" % card["cost"])
        print("字段  : %s" % card["fields"])
        if card["id"] in FORCE_BOSS_CARDS:
            print()
            print("★ 这张卡是强制 BOSS 的触发条件（docs/WAVES.md §9）")
        return 0

    if args.json:
        print(json.dumps({"table_va": "0x%X" % CARD_DESC_LIST_VA,
                          "count": len(cards), "cards": cards},
                         indent=2, ensure_ascii=False))
        return 0

    print("卡牌表 @ VA 0x%X（%d 项，每项 %d DWORD）"
          % (CARD_DESC_LIST_VA, len(cards), ENTRY_DWORDS))
    print()
    print("  %-4s %-16s %-6s %s" % ("ID", "名称", "cost?", "字段[2..]"))
    for card in sorted(cards, key=lambda c: c["id"]):
        mark = " ★" if card["id"] in FORCE_BOSS_CARDS else ""
        print("  %-4d %-16s %-6d %s%s"
              % (card["id"], card["name"], card["cost"],
                 " ".join(str(f) for f in card["fields"][:10]), mark))
    print()
    print("★ = 强制 BOSS 触发卡（ID 0 BLANK / ID 66 NITORI）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

