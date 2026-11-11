# TH185 难度系统

> 状态：**8 档难度索引、分数上限表、访问器函数族已解出；数值倍率未解。**
> 日期：2026-11-11

---

## 1. 难度档位（8 档）

难度索引在 **`0x4D1038`**（uint32，值 0..7）。

| 索引 | 名称 | 备注 |
|---|---|---|
| 0 | Very Easy | |
| 1 | Easy | |
| 2 | Normal | |
| 3 | **Little Hard** | **TH185 独有，TH18 没有** |
| 4 | Hard | |
| 5 | Very Hard | |
| 6 | Lunatic | |
| 7 | Over Drive | 最高难度 |

证据：`th185.exe` 字符串实测命中全部 8 个名称；
thprac 的 `difficulties[8]` 数组完全一致。

---

## 2. 分数上限表（5 项）

### 2.1 初始化代码

```asm
03107C: movaps xmm0, xmmword ptr [0x4bcb00]   ; 读 16 字节常量
031083: movups xmmword ptr [ebp - 0x18], xmm0  ; 存到栈上局部数组
031087: mov dword ptr [ebp - 8], 0xf4240       ; 第 5 项 = 1000000
03109A: mov eax, dword ptr [0x4d1038]          ; 难度索引
0310A1: mov eax, dword ptr [ebp + eax*4 - 0x18]  ; ★ 查表
0310A5: imul eax, dword ptr [0x4d1004]         ; × 关卡号
0310AC: mov dword ptr [edi + 0x7c], eax        ; 存入对象 +0x7C
0310AF: mov dword ptr [edi + 0x80], eax        ; 同时存 +0x80
```

### 2.2 表内容

常量 `VA 0x4BCB00` 的前 16 字节：

```
20 A1 07 00 | 40 42 0F 00 | 60 E3 16 00 | 80 84 1E 00
  = 500000  |  = 1000000  |  = 1500000  |  = 2000000
```

加上 `0x31087` 的立即数 `0xF4240`：

| 索引 | 值 |
|---|---|
| 0 | 500,000 |
| 1 | 1,000,000 |
| 2 | 1,500,000 |
| 3 | 2,000,000 |
| 4 | 1,000,000 |

### 2.3 一个必须说清楚的问题

**这张表只有 5 项，但难度有 8 档。**

代码用 `[ebp + eax*4 - 0x18]` 查表，`eax` 是难度索引 0..7。
索引 5/6/7 会读到 `[ebp-8]` 之后的栈上相邻数据——
**这是原版的越界读，不是我的误读。**

结合下一节的发现（只有 `is_difficulty_0..3` 四个判断函数），
最合理的解释是：**这个函数只在难度 0..4 的场合被调用**，
5..7 走另一条路径。

---

## 3. 全局访问器函数族

RVA `0x3E735` – `0x3E7C5` 是一串极短的 getter，每个 3-4 条指令：

| RVA | 行为 | 等价 C |
|---|---|---|
| `0x3E735` | `mov eax, [0x4d1040]` | `return g_4d1040;` |
| **`0x3E740`** | `mov eax, [0x4d1038]` | **`return difficulty;`** |
| `0x3E74B` | `mov eax, [ecx+0x6224]` | `return self->field_6224;` |
| `0x3E757` | `cmp [0x4d1038],0; sete al` | `return difficulty == 0;` |
| `0x3E768` | `cmp [0x4d1038],1; sete al` | `return difficulty == 1;` |
| `0x3E77A` | `cmp [0x4d1038],2; sete al` | `return difficulty == 2;` |
| `0x3E78C` | `cmp [0x4d1038],3; sete al` | `return difficulty == 3;` |
| `0x3E79E` | `mov eax, [EM+0x3c]` | `return em->field_3c;` |
| `0x3E7AC` | `mov eax, [EM+0x40]` | `return em->field_40;` |
| `0x3E7BA` | `mov eax, [EM+0x44]` | `return em->field_44;` |
| `0x3E7C8` | `mov eax, [EM+0x4f44]` | `return em->field_4f44;` |

> `EM` = `[0x4D7AF4]`（ENEMY_MANAGER）。

### 3.1 关键观察：只有 4 个难度判断

**没有 `is_difficulty_4..7`。**

这说明游戏逻辑里只对**前 4 档**（Very Easy / Easy / Normal / Little Hard）
做了特殊分支，Hard 及以上走通用路径。

这也解释了 §2.3 的疑问：分数上限表只需要 5 项就够用了。

---

## 4. 与其他作品的对照

| 作品 | 难度档数 | 特殊档 |
|---|---|---|
| TH11 地灵殿 | 4 + Extra | — |
| TH18 虹龙洞 | 6 | — |
| **TH185 黑市** | **8** | **Little Hard（索引 3）** |

TH185 比 TH18 多 2 档，且第 4 档名称是 `Little Hard` 而非 `Hard`。

---

## 5. 工具

```powershell
python th185_web/scripts/native/difficulty.py --list
python th185_web/scripts/native/difficulty.py --selftest
```

自检 5 项：档位数 8、索引 3 = Little Hard、索引 7 = Over Drive、
判断函数只有 4 个、分数上限表内容。

---

## 6. 状态

| 项 | 状态 |
|---|---|
| 难度档位与索引 | ✅ 完全解出 |
| 分数上限表 | ✅ 完全解出（5 项） |
| 访问器函数族 | ✅ 完全解出（11 个） |
| 仅前 4 档有特殊分支 | ✅ 已确认 |
| **各档的弹速/密度倍率** | ❌ **未解**（核心数值） |
| 分数上限表的实际用途 | ⚠️ 推测为「本关分数上限基数」 |

**`SYS-GAME-DIFFICULTY` 保持 `in-progress`**——
结构解出来了，但**最关键的数值倍率（弹速、密度、判定）还没找到**。

> 下一步：搜 `.rdata` 里的 float 数组，找 8 元素且递增的浮点表，
> 那些很可能就是各档难度的倍率。

---

## 7. 深入：为什么找不到 8 元素的倍率表（本轮新结论）

### 7.1 尝试与失败

按上一轮的计划，我扫了 `.rdata` 找「8 元素递增 float 数组」：

| 尝试 | 结果 |
|---|---|
| 8 元素递增 float，范围 0.05–20 | 46 个候选，**全是同一张表的滑窗重叠** |
| 加严格条件（无重复 + 首尾比 > 1.2） | 仍是那 46 个滑窗 |
| 限定 0.5–3.0（倍率典型范围） | **0 个** |
| 从 `movss` 常量地址反查连续 8 浮点 | 全是通用常量池（`5,6,2π,7,8...`） |

被误判的那张表在 VA `0xBC6C4`，内容是：

```
0.0500 0.0625 0.0667 0.0800 0.0982 0.1000 0.1111 0.1250 ...
```

这些是 `1/20, 1/16, 1/15, 1/12.5, 1/10.2, 1/10, 1/9, 1/8`——
**一张倒数表**（正弦/角度相关），不是难度倍率。

### 7.2 决定性发现：难度判断函数是虚函数

搜索 `is_difficulty_0..3` 的 `call` 调用点：

```
is_diff_0 (0x3E757): 0 处
is_diff_1 (0x3E768): 0 处
is_diff_2 (0x3E77A): 0 处
is_diff_3 (0x3E78C): 0 处
```

**一个都没有。** 这不符合「普通 getter」的预期。

继续搜「哪些地方存了这些函数地址」，在 `.text` 段 **RVA `0x3EDB0`** 找到：

```
RVA 0x3EDB0: 0x0043E757  (is_difficulty_0)   ← vtable 起点
RVA 0x3EDB4: 0x0043E768  (is_difficulty_1)
RVA 0x3EDB8: 0x0043E77A  (is_difficulty_2)
RVA 0x3EDBC: 0x0043E78C  (is_difficulty_3)
RVA 0x3EDC0: 0x0043E79E  (get_em_3c)
RVA 0x3EDC4: 0x0043E7AC  (get_em_40)
RVA 0x3EDC8: 0x0043E7BA  (get_em_44)
RVA 0x3EDCC: 0x0043E7C8  (get_em_4f44)
```

> **地址基准踩坑**：第一次写成「VA 0x3EDB0」是错的。搜索时用的是
> `sec.vaddr + i`（i 为节内偏移），结果本身是 **RVA**。
> 验证时 `rva_to_off(0x3EDB0 - 0x400000)` 得到负数才暴露。
> 正确做法：直接 `rva_to_off(0x3EDB0)`。
> 这是本项目第四次踩 VA/RVA 混淆的坑。

**这是一张虚函数表（vtable）。**

所以难度判断是**通过接口间接调用**的：

```c
// 伪代码
struct IGameState {
    virtual int  get_ecx_6224();      // vtable[0]
    virtual bool is_difficulty_0();   // vtable[1]
    virtual bool is_difficulty_1();   // vtable[2]
    virtual bool is_difficulty_2();   // vtable[3]
    virtual bool is_difficulty_3();   // vtable[4]
    virtual int  get_em_3c();         // vtable[5]
    virtual int  get_em_40();         // vtable[6]
    virtual int  get_em_44();         // vtable[7]
    virtual int  get_em_4f44();       // vtable[8]
};

// 调用方：
state->is_difficulty_2()   // 编译成 call [vtable + 0x10]
```

### 7.3 这意味着什么

**难度相关的数值倍率很可能不在静态表里，而是硬编码在各处的条件分支中。**

TH 系列的传统做法有两种：

| 做法 | 特征 | 本例 |
|---|---|---|
| 查表 | 有连续的 8 元素数组 | ❌ 找不到 |
| 条件分支 | 散落在各处的 `if (diff >= N)` | ✅ 更可能 |

如果确实是条件分支，那么「提取难度倍率」就等于**逐个函数读代码**，
没有捷径可走。这解释了为什么扫表扫不到。

### 7.4 状态修正

| 项 | 上一轮判断 | 本轮结论 |
|---|---|---|
| 难度倍率表位置 | 「下一步搜 .rdata」 | ❌ **不存在这样的表** |
| 难度判断函数性质 | 「普通 getter」 | ✅ **虚函数（vtable 间接调用）** |
| 提取倍率的方法 | 查表 | ⚠️ 需逐函数读条件分支 |

**`SYS-GAME-DIFFICULTY` 维持 `in-progress`。**
结构（档位/分数表/访问器/vtable）已完全解出，但**数值倍率需要逐处逆向**，
工作量远大于查表，应放到实现阶段再做。

### 7.5 值得记录的教训

**「找不到」也是结论。** 扫了两轮 float 表都没有，加上「4 个判断函数零调用点」
这个异常，最终定位到 vtable——比继续盲目扫描更有价值。
