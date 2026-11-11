# TH185 卡牌类映射（RTTI → vtable → 虚函数）

> 状态：**87 个卡牌类全部定位到 vtable，每个 22 个虚函数；效果实现语义未解。**
> 日期：2026-11-12

---

## 1. 为什么做这个

`docs/RECONSTRUCTION.md` §7 把「自机 + 卡牌效果（87 类）」评为
**最大工作量项**（0.8 / 3.4 单位），且是全新工作（TH11 无对应）。

本文档是它的入口：把 `docs/CARDS.md` 里 87 个不透明卡牌 id
变成**有名字的 C++ 类 + 已定位的虚函数**，这是写效果实现的前提。

---

## 2. 二进制给我们的东西

TH185 是 MSVC 编译且带完整 RTTI，所以每个卡牌类留下一条链：

```
TypeDescriptor   ".?AVAbilityCardDangoInf@@"
    ↓ 被引用
Complete Object Locator   (signature = 0)
    ↓ 被引用
vtable[-1]
    ↓
vtable[0..21]  →  .text 函数
```

### 2.1 实测数据

| 项 | 值 |
|---|---|
| `AbilityCard*` TypeDescriptor 数 | **87** |
| 成功解析到 vtable 的 | **87 / 87** |
| 每个 vtable 的虚函数数 | **22**（全部一致） |
| vtable 位置 | `.rdata` 的 `0x4B6AB0`..`0x4B8998` |
| 相邻 vtable 间距 | `0x5C` = 92 字节 = **23 指针** |

**间距算术锁定了虚函数数**：23 指针 = 22 个虚函数 + 1 个 COL 回指针。
这比「数到下一个非 `.text` 词为止」可靠得多。

---

## 3. 虚函数槽位语义

22 个槽位里，只有少数有实际逻辑：

| 槽位 | 不同实现数 | 判读 |
|---|---|---|
| 0..2 | 18..24 | 部分类覆盖，多为 `xor eax,eax; ret` |
| 3..5, 8, 10, 12, 14..19 | 1..4 | **默认实现**（`xor eax,eax; ret`） |
| 6, 7, 9, 11 | 9..40 | 部分类覆盖 |
| 13 | 66 | 部分类覆盖 |
| 20 | **79** | 大部分类覆盖 |
| **21** | **87** | **标量删除析构函数** |

### 3.1 槽位 21 = 标量删除析构函数（不是效果！）

```asm
40A230: push ebp
40A231: mov ebp, esp
40A233: push esi
40A234: mov esi, ecx
40A236: mov dword ptr [esi], 0x4b8998   ; ★ 写基类 vtable 指针
40A23C: mov edx, dword ptr [esi + 0x10] ; 链表摘除
...
40A256: test byte ptr [ebp + 8], 1      ; ★ 测试 delete 标志
40A26A: push 0x58                        ; 对象大小
40A26D: call 0x48f711                   ; operator delete
40A279: ret 4
```

**这是 vtable 的常规槽位**，每个类都有。
我一开始把「87 个类全不同」误判成「效果实现」，实际是析构函数的常规分布。

### 3.2 槽位 20 = 状态标志设置（不是效果主体）

`Dango` 的槽位 20：

```asm
40AA80: or dword ptr [ecx + 0x54], 2   ; 设置一个标志位
40AA84: ret
```

只有 **5 字节**。`Komachi` 的同槽位 61 字节，是清除多个字段的状态重置。

长度分布：

| 长度 | 类数 |
|---|---|
| 3 字节 | 9 |
| **5 字节** | **56** |
| 12 字节 | 3 |
| 61 字节 | 4 |
| 129 字节 | 9 |
| 148 字节 | 最多（多数是析构槽 21） |

**卡牌效果是薄封装——设置标志位/字段，真正生效逻辑在消费这些字段的地方。**

---

## 4. ★ 命名陷阱：不能按字符串关联

卡表名与 RTTI 名**不总是相同**：

| 卡表名 | RTTI 名 |
|---|---|
| `FLAN` | `Fran` |
| `STAR` | `StarOp` |
| `LUNA` | `LunaOp` |
| `SUNNY` | `SunnyOp` |
| `YUREIMU` | `YuReimuOp` |
| `YUMARISA` | `YuMarisaOp` |

**16 个名字无法精确匹配。** 必须按 `id` 关联，不能按字符串。

---

## 5. 87 个类名

```
AliceOp    Aun        AutoBomb   BassDrum   Blank      CirnoOp
Cylinder   Dango      DeadSpell  Dremy      EikaOp     Extend
Fran       Futo       ItemCatch  ItemLine   Joon       Junko
Kaname     Kaname2    Keiki      Kiseru     Koishi     Komachi
Kozuchi    Kutaka     LarvaOp    Lily       LunaOp     MagatamaOp
MagicScroll MainShotPU MainShotSP Maneki    MarisaOp   MarisaOp2
MikoFlash  Minoriko   Mokou      Money      Moon       Mukade
Narumi     NemunoOp   Nitori     NueOp      Null       OkinaOp
OptionBR   Pache      Peace      Pendulum   Pendulum2  PowerMax
Psyco      ReimuOp    ReimuOp2   Riceball   Rokumon    Saki
SakuyaOp   SakuyaOp2  SanaeOp    SanaeOp2   Seija      SeiranOp
SekibankiOp Shion     SpeedQueen StarOp     Suika      Sun
SunnyOp    Takane     Tewi       ToramaruOp Urumi      Vampire
Wakasagi   Warp       Yamawaro   YoumuOp    YoumuOp2   YuMarisaOp
YuReimuOp  Yuyuko
```

---

## 6. 工具

```powershell
python th185_web/scripts/native/card_classes.py --selftest   # 8 项
python th185_web/scripts/native/card_classes.py --list       # 87 类 + vtable
python th185_web/scripts/native/card_classes.py --class Dango
```

---

## 7. 验证（8 项）

```
[PASS] found 87 AbilityCard RTTI classes (expect 87)
[PASS] every class resolves to a vtable (87 / 87)
[PASS] every vtable has exactly 22 virtual functions
[PASS] every class has a DISTINCT vtable
[PASS] all vtables lie in the expected .rdata window
[PASS] the abstract base (.?AVAbilityCardInf@@) is present
[PASS] slot 21 is the scalar deleting destructor in every class
[PASS] card table also has 87 entries (they correspond 1:1)
```

---

## 8. 未解项

| 项 | 状态 |
|---|---|
| 87 个类的 RTTI 与 vtable | ✅ 全部定位 |
| 虚函数数量与槽位分布 | ✅ 22 个，已统计 |
| 槽位 21 语义 | ✅ 标量删除析构 |
| **卡牌效果的主体逻辑** | ❌ **未解** |
| **效果对象的内存布局** | ❌ 未解（`ecx+0x10`/`0x14`/`0x1c`/`0x34`..`0x58` 已见片段） |
| **标志位被谁消费** | ❌ 未解 |
| 卡牌 id → 类 的精确映射表 | ⚠️ 16 个需按 id 而非名字关联 |

**`SYS-GAME-CARD` 保持 `in-progress`。**

> 卡牌效果是「薄封装 + 标志位」结构，
> 真正玩法逻辑在消费标志的地方（自机/弹幕/道具系统），
> 那些系统尚未实现——所以这里无法单独完成。
> 这是一个**结构性依赖**，不是分析不足。
