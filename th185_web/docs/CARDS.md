# TH185 卡牌表

> 状态：**87 张卡的名称与 ID 已完全解出并验证；属性字段部分待确认。**
> 日期：2026-11-11

---

## 1. 表结构与位置

| 项 | 值 |
|---|---|
| VA | **`0x4CA370`**（thprac 的 `CARD_DESC_LIST`） |
| RVA | `0xCA370` |
| 项数 | **87** |
| 每项大小 | **68 字节（17 个 DWORD）** |

```c
struct CardEntry {           // 68 字节
    const char* name;        // +0x00  内部名（ASCII，如 "PENDULUM"）
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
    uint32_t    cost;        // +0x30  ★ 疑似 Cost（待确认）
    uint32_t    f13;         // +0x34
    uint32_t    f14;         // +0x38
    uint32_t    f15;         // +0x3C
    uint32_t    f16;         // +0x40
};
```

---

## 2. 最重要的发现：ID 0 和 66

上一轮（`docs/WAVES.md` §9）解出的强制 BOSS 机制依赖两个卡牌 ID。
**现在知道它们是什么了：**

| ID | 内部名 | 触发条件 | 强制 BOSS |
|---|---|---|---|
| **0** | `BLANK` | 关卡 5 持有 | Boss26 |
| **66** | `NITORI` | 关卡 7 持有 | Boss27 |

### 这完全印证了 THBWiki 的剧情描述

THBWiki 原文：

> 「在月虹市场，魔理沙遇到了千亦。……千亦告诉魔理沙组织并操纵黑市的是山城高岭，
> 并指引魔理沙装备**「空白卡牌」**后到守矢神社的祭典上找自己的合作者。
> 找到**荷取**后，魔理沙从荷取手里获得了荷取制造的毁灭性的能力卡牌
> 「资本主义的困境」。」

**对照解出的机制：**

| 剧情 | 代码 |
|---|---|
| 装备「空白卡牌」 | 持有 `BLANK`（ID 0） |
| 到守矢神社找荷取 | 关卡 5 + 持有 `BLANK` → 强制 Boss26 |
| 从荷取手里获得卡 | 拿到 `NITORI`（ID 66） |
| 之后推进到虹龙洞找高岭 | 关卡 7 + 持有 `NITORI` → 强制 Boss27 |

**这是本轮最有价值的交叉验证**：静态逆向的结果与剧情文档完全吻合，
说明两边的解读都是对的。

---

## 3. 卡牌全表（87 项）

按 ID 排序。★ 标记的是强制 BOSS 触发卡。

| ID | 名称 | 说明 |
|---|---|---|
| 0 | `BLANK` | ★ 空白卡牌，关卡 5 触发 Boss26 |
| 1 | `PENDULUM` | 摆子 |
| 2 | `PENDULUM2` | 摆子（强化） |
| 3 | `EXTEND` | 残机 |
| 4 | `DANGO` | 团子 |
| 5 | `MOKOU` | 妹红 |
| 6 | `REIMU_OP` | 灵梦 Option |
| 7 | `REIMU_OP2` | 灵梦 Option（强化） |
| 8 | `MARISA_OP` | 魔理沙 Option |
| 9 | `MARISA_OP2` | 魔理沙 Option（强化） |
| 10 | `SAKUYA_OP` | 咲夜 Option |
| 11 | `SAKUYA_OP2` | 咲夜 Option（强化） |
| 12 | `SANAE_OP` | 早苗 Option |
| 13 | `SANAE_OP2` | 早苗 Option（强化） |
| 14 | `YOUMU_OP` | 妖梦 Option |
| 15 | `ALICE_OP` | 爱丽丝 Option |
| 16 | `CIRNO_OP` | 琪露诺 Option |
| 17 | `OKINA_OP` | 隐岐奈 Option |
| 18 | `NUE_OP` | 鵺 Option |
| 19 | `ITEM_CATCH` | 道具吸引 |
| 20 | `ITEM_LINE` | 道具线 |
| 21 | `AUTOBOMB` | 自动 Bomb |
| 22 | `TEWI` | 帝 |
| 23 | `MAINSHOT_PU` | 主弹 Power Up |
| 24 | `MAGICSCROLL` | 魔法书 |
| 25 | `KOISHI` | 恋恋 |
| 26 | `MAINSHOT_SP` | 主弹 Speed |
| 27 | `SPEEDQUEEN` | 速度女王 |
| 28 | `OPTION_BR` | Option（广范围） |
| 29 | `DEAD_SPELL` | 死符 |
| 30 | `POWERMAX` | Power 全满 |
| 31 | `YUYUKO` | 幽幽子 |
| 32 | `MONEY` | 金钱 |
| 33 | `ROKUMON` | 六文钱 |
| 34 | `NARUMI` | 成美 |
| 35 | `PACHE` | 帕秋莉 |
| 36 | `MANEKI` | 招财猫 |
| 37 | `YAMAWARO` | 山童 |
| 38 | `KISERU` | 烟管 |
| 39 | `WARP` | 传送 |
| 40 | `KOZUCHI` | 小槌 |
| 41 | `KANAME` | 要 |
| 42 | `MOON` | 月 |
| 43 | `MIKOFLASH` | 巫女闪 |
| 44 | `VAMPIRE` | 吸血鬼 |
| 45 | `SUN` | 太阳 |
| 46 | `LILY` | 莉莉 |
| 47 | `BASSDRUM` | 大鼓 |
| 48 | `PSYCO` | 精神 |
| 49 | `MAGATAMA` | 勾玉 |
| 50 | `CYLINDER` | 圆筒 |
| 51 | `RICEBALL` | 饭团 |
| 52 | `MUKADE` | 蜈蚣 |
| 53 | `TORAMARU` | 虎丸 |
| 54 | `STAR` | 星星 |
| 55 | `LUNA` | 露娜 |
| 56 | `SUNNY` | 桑尼 |
| 57 | `FLAN` | 芙兰 |
| 58 | `FUTO` | 布都 |
| 59 | `AUN` | 阿吽 |
| 60 | `JOON` | 猯藏 |
| 61 | `SHION` | 紫苑 |
| 62 | `KEIKI` | 桂木 |
| 63 | `SEIRAN` | 青兰 |
| 64 | `DREMII` | 多莉米 |
| 65 | `JYUNKA` | 纯狐 |
| 66 | `NITORI` | ★ 荷取，关卡 7 触发 Boss27 |
| 67 | `TAKANE` | 高岭 |
| 68 | `MINORIKO` | 穰子 |
| 69 | `LARVA` | 幼虫 |
| 70 | `NEMUNO` | 祢梦 |
| 71 | `WAKASAGI` | 若鹭姬 |
| 72 | `SEKIBANKI` | 赤蛮奇 |
| 73 | `URUMI` | 润美 |
| 74 | `KUTAKA` | 久侘歌 |
| 75 | `KOMACHI` | 小町 |
| 76 | `EIKA` | 璎花 |
| 77 | `YOUMU_OP2` | 妖梦 Option（强化） |
| 78 | `SEIJA` | 成美（另一形态） |
| 79 | `KANAME2` | 要（另一形态） |
| 80 | `PEACE` | 和平 |
| 81 | `SAKI` | 早鬼 |
| 82 | `SUIKA` | 萃香 |
| 83 | `YUREIMU` | 灵梦（他人） |
| 84 | `YUMARISA` | 魔理沙（他人） |
| 85 | `NULL` | 空占位 |
| 86 | `BACK` | 卡牌背面 |

> 中文译名是**推测**，依据是内部名与东方角色名的对应关系。
> 官方中文名需查 THBWiki 卡牌系统页。

---

## 4. 与调研数字的对照

| 调研说法 | 实测 | 说明 |
|---|---|---|
| 「85 张能力卡」 | 87 项 | 85 可用 + `NULL`(85) + `BACK`(86) |
| thprac `card_names_t::count = 86` | 87 | thprac 可能不含 `NULL` |
| 「87 个 `AbilityCard*` RTTI 类」 | 87 | **完全吻合** ✅ |

**三个数字全部对上了**，只是计数口径不同：
表里 87 项 = 85 张可用卡 + 1 个 `NULL` 占位 + 1 个 `BACK`（背面）。

---

## 5. 字段含义（部分待确认）

以 `BLANK`（ID 0）为例：

```
name  = "BLANK"
id    = 0
字段  = 0 2 5 8 0 1 1 1 0 1 1 0 5 6 0
       f2 f3 f4 ...                    f16
```

| 字段 | 观察 | 推测 |
|---|---|---|
| `f2` | 多为 0/1 | 是否主动卡 |
| `f3` | 0..4 | 稀有度或类型 |
| `f4` | 1..13 | 可能是 Cost 或等级 |
| `f12`（`+0x34`） | 0..200 | ✅ **Cost**（已确认，见下方修正） |
| `f13`（`+0x30`） | 0/1 | 标志位（**不是 Cost**） |
| `f14`/`f15` | 递增编号 | 图集格子坐标（左格全奇数，右格 = 左格+1） |

**已确认（2026-11-14 修正）**：Cost 在 `+0x34`，11 个档位：

```
0, 10, 25, 30, 50, 60, 75, 100, 125, 150, 200
```

之前本文档把档位观察挂在了 `+0x30` 上，那是**错的**——
`+0x30` 在全部 87 张卡里只有 0/1 两种值，是标志位。
`docs/CPP.md` §8.3 的「+0x30 不是 Cost」判断正确，
但它没有指出真 Cost 在哪。

两张特殊卡的 Cost：`BLANK` = 0（免费）、`NITORI` = 50。

实现见 `cpp/game/CardManager.hpp` 的 `CardEntry::cost()` 与
`CardInventory::cost_cap()/loadout_cost()`。

---

## 6. 工具

```powershell
# 列出全部卡牌
python th185_web/scripts/native/cards.py --list

# JSON 输出
python th185_web/scripts/native/cards.py --json

# 查特定卡
python th185_web/scripts/native/cards.py --id 0
python th185_web/scripts/native/cards.py --id 66

# 自检
python th185_web/scripts/native/cards.py --selftest
```

自检 5 项：项数 87、ID 覆盖 0..86、ID 0 = BLANK、ID 66 = NITORI、名称唯一。

---

## 7. 状态

| 项 | 状态 |
|---|---|
| 表位置与结构 | ✅ 完全解出（68 字节/项） |
| 87 张卡的名称与 ID | ✅ **完全解出并验证** |
| 强制 BOSS 触发卡 | ✅ ID 0 `BLANK` / ID 66 `NITORI` |
| 与剧情文档的交叉验证 | ✅ **完全吻合** |
| 字段语义（Cost 等） | ⚠️ 推测，未验证 |
| 卡牌效果实现 | ❌ 未开始 |
| 中文官方译名 | ❌ 未核对 |

**`SYS-GAME-CARD` 保持 `in-progress`**——表解出来了，但卡牌效果逻辑没碰。
