# TH185 波次系统

> 状态：**波次模板表已完全解出（111 项，结构验证通过）；抽取算法已反汇编但未做运行时验证。**
> 日期：2026-11-11

---

## 1. 这是 TH185 最独特的机制

TH18 是「6 面 + Extra」的固定结构。TH185 完全不同：

```
TH18:  面 1 -> 面 2 -> ... -> 面 6 -> Extra（固定）
TH185: 场景（9 个）-> 波次 1..N（每波从模板池随机抽）-> BOSS（候选池随机）
```

**同一个场景每次玩到的道中编成都不一样。** 这是本作设计的核心，
也是它「重复游玩」定位的技术基础。

---

## 2. 波次模板表（已完全解出）

### 2.1 位置

| 项 | 值 |
|---|---|
| VA | **`0x4B5D50`** |
| RVA | `0xB5D50` |
| 项数 | **111**（`0x6F`） |
| 结构 | 111 个 DWORD，每个是字符串指针 |

### 2.2 完整结构

| 索引范围 | 名称模式 | 数量 | 含义 |
|---|---|---|---|
| `[0]` | （空） | 1 | 占位，表示「不选」 |
| `[1]` | `Boss01t` | 1 | 教程 BOSS |
| `[2..28]` | `Boss01`..`Boss27` | 27 | 常规 BOSS |
| `[29..31]` | `Wave01t`..`Wave03t` | 3 | 教程道中波次 |
| `[32..110]` | `Wave01`..`Wave79` | 79 | 常规道中波次 |

**合计 1 + 1 + 27 + 3 + 79 = 111。**

### 2.3 与调研阶段的数字对照

| 调研说法 | 实测 | 差异原因 |
|---|---|---|
| 「110 条模板池」 | **111 项**（索引 0..110） | 索引 0 是空占位，**实际可选 110 条** |
| 「28 个 BOSS 编号」 | **28 个**（1 教程 + 27 常规） | 口径一致 ✅ |

**两个数字都对，只是计数口径不同。** 调研是从 thprac 的
`for (i = 0; i < 110; i++)` 循环推断的，那个 110 是「可选模板数」；
而表本身有 111 项，第 0 项是占位。

### 2.4 验证

```
[PASS] empty          期望 1 项
[PASS] tutorial_boss  期望 1 项
[PASS] boss           期望 27 项
[PASS] tutorial_wave  期望 3 项
[PASS] wave           期望 79 项
合计 111 项
```

`waves.py --selftest` 把分布硬编码，锁死表结构不被误读。

---

## 3. 权重数组（运行时构建）

权重**不在静态数据里**，是运行时填的：

| 地址 | 用途 | 证据 |
|---|---|---|
| `ENEMY_MANAGER + 0x4E0` | 权重数组 A | `0x3D02F add edi, [ecx-0x1BC]`（`0x4E0-0x1BC = 0x324`） |
| `ENEMY_MANAGER + 0x324` | 权重数组 B | `0x3D0B8 add edx, 0x324` |
| `ENEMY_MANAGER + 0x4F4C` | 要跳过的索引 | `0x3D021 cmp eax, [esi+0x4f4c]` |

`ENEMY_MANAGER` 的全局指针在 **`0x4D7AF4`**（thprac 已记录）。

> 两个权重数组对应两种抽取模式。从代码看 `0x3D02F` 分支（数组 A）
> 和 `0x3D0B8` 分支（数组 B）是**互斥的两条路径**，
> 由 `[ebp-0x638]` 这个标志决定走哪条。具体语义待确认。

---

## 4. 抽取算法（RVA 0x3CFFF - 0x3D0E6）

### 4.1 第一遍：累加权重

```asm
03CFFF: mov ecx, [0x4d7af4]    ; ecx = ENEMY_MANAGER
03D005: xor esi, esi
03D007: xor eax, eax           ; eax = i = 0
03D009: mov [ebp-0x63c], esi   ; total = 0
03D00F: add ecx, 0x4e0         ; ecx = &weights_a[0]
03D015: xor edi, edi           ; edi = 0（另一个累加器）
03D017: test edx, edx
03D019: je 0x3d02f
03D01B: mov esi, [0x4d7af4]
03D021: cmp eax, [esi+0x4f4c]  ; i == skip_index ?
03D02D: je 0x3d03d             ; 跳过
03D02F: add edi, [ecx-0x1bc]   ; edi += weights_a[i]
03D035: add esi, [ecx]         ; total += weights_b[i]
03D03D: inc eax
03D03E: add ecx, 4
03D041: cmp eax, 0x6f          ; ★ 111
03D044: jl 0x3d017
```

### 4.2 RNG 取模

```asm
03D046: mov ecx, 0x4d7aa8      ; ★ RNG 实例（与 RNG.md 一致）
03D053: call 0x2800            ; ★ RNG
03D05E: xor edx, edx
03D060: cmp [ebp-0x63c], esi
03D066: je 0x3d0a8             ; total_b == 0 -> 走另一分支
03D068: div dword ptr [ebp-0x63c]  ; ★ EAX / total_b
03D070: mov edi, edx           ; edi = roll = 余数
```

### 4.3 第二遍：减去权重直到为负

```asm
03D072: mov edx, [0x4d7af4]
03D078: add edx, 0x4e0         ; &weights_a[0]
03D080: test ecx, ecx
03D082: je 0x3d098
03D084: mov ecx, [0x4d7af4]
03D08A: cmp eax, [ecx+0x4f4c]  ; i == skip_index ?
03D096: je 0x3d09c             ; 跳过（不加 esi）
03D098: sub edi, [edx]         ; roll -= weights[i]
03D09A: js 0x3d0e6             ; ★ roll < 0 -> 命中，跳出
03D09C: inc eax
03D09D: inc esi
03D09E: add edx, 4
03D0A1: cmp eax, 0x6f
03D0A4: jl 0x3d080
```

### 4.4 等价伪代码

```c
int pick_wave(EnemyManager* em, uint32_t skip_index) {
    // 第一遍：累加（跳过 skip_index）
    uint32_t total = 0;
    for (int i = 0; i < 111; i++) {
        if (i != skip_index) {
            total += em->weights_b[i];
        }
    }
    if (total == 0) return -1;

    // RNG 取模
    uint32_t roll = rng_next() % total;

    // 第二遍：减到负数
    for (int i = 0; i < 111; i++) {
        if (i != skip_index) {
            roll -= em->weights_a[i];
            if ((int32_t)roll < 0) return i;
        }
    }
    return -1;
}
```

**关键细节：两遍都跳过 `skip_index`，所以同一模板不会连续出现两次。**
这是防止「连续两波一样」的设计。

### 4.5 两条互斥路径（本轮澄清的关键点）

第一次分析时我怀疑「总数与逐项和不匹配」，**这个怀疑是错的**。

关键在于：`0x4E0 - 0x324 = 0x1BC = 444 = 111 × 4`。
两个数组**紧邻且各占 111 个 dword**，是两个独立的权重表。

第一遍循环同时累加两个总和：

```asm
03D00F: add ecx, 0x4e0         ; ecx = EM + 0x4E0
03D02F: add edi, [ecx-0x1bc]   ; edi += A[i]   (EM + 0x324)
03D035: add esi, [ecx]         ; esi += B[i]   (EM + 0x4E0)
```

然后按 `total_B` 是否为零分两条**互斥**路径：

| 路径 | 条件 | 取模用 | 递减用 | 代码 |
|---|---|---|---|---|
| **1** | `total_B != 0` | `total_B` | **B 数组**（`+0x4E0`） | `0x3D068` / `0x3D078` |
| **2** | `total_B == 0` | `total_A` | **A 数组**（`+0x324`） | `0x3D0A8` / `0x3D0B2` |

```asm
; 路径 1
03D066: je 0x3d0a8             ; total_B == 0 -> 跳路径 2
03D068: div dword ptr [ebp-0x63c]  ; roll = RNG % total_B
03D078: add edx, 0x4e0         ; 用 B 数组递减

; 路径 2
03D0A8: div dword ptr [ebp-0x62c]  ; roll = RNG % total_A
03D0B2: add edx, 0x324         ; 用 A 数组递减
```

**每条路径内部都是自洽的**：取模用哪个数组的总和，递减就用哪个数组。
不存在总数与逐项和不匹配的问题。

> 两个数组的语义待确认。可能是「常规权重」与「Boss Rush 权重」，
> 或者「当前场景可用」与「全局权重」。需要运行时 dump 才能确定。

---

## 5. 命中后的处理

```asm
03D156: push dword ptr [esi*4 + 0x4b5d50]   ; ★ 用索引查名表
```

`esi` 是命中索引，`[esi*4 + 0x4B5D50]` 取出模板名字符串指针。
**这确认了 `0x4B5D50` 就是名表，索引就是这里的 `esi`。**

---

## 6. 特殊索引的硬编码

在 `0x3D0E6` 之后有段特殊处理：

```asm
03D0E6: test dword ptr [ebp-0x644], 0x100
03D0F8: mov eax, [0x4d1004]      ; 某个状态值
03D0FD: cmp eax, 5
03D121: mov esi, 0x1b           ; ★ 索引 27（Boss27）
03D128: cmp eax, 7
03D151: mov esi, 0x1c           ; ★ 索引 28（无对应名？）
```

**`0x1B` = 27**（`Boss27`），**`0x1C` = 28**（超出 BOSS 范围）。
这说明存在硬编码的 BOSS 强制选择，条件是某个状态值 `[0x4D1004]`
等于 5 或 7。具体语义待确认——可能是「剧情 BOSS」或「最终 BOSS」。

---

## 7. 工具

```powershell
# 列出完整表
python th185_web/scripts/native/waves.py --list

# JSON 输出（给其他脚本用）
python th185_web/scripts/native/waves.py --json

# 自检（锁死表结构）
python th185_web/scripts/native/waves.py --selftest
```

---

## 8. 状态

| 项 | 状态 |
|---|---|
| 名表位置与内容 | ✅ **完全解出并验证** |
| 表结构分类 | ✅ 1+1+27+3+79 = 111 |
| 抽取算法（两遍循环） | ✅ 反汇编完整 |
| 权重数组位置 | ✅ `+0x324` / `+0x4E0` / `+0x4F4C` |
| 权重数组内容 | ❌ 运行时才填，未 dump |
| 两条路径互斥且各自自洽 | ✅ **已澄清**（0x1BC = 111×4，两个独立数组） |
| 两个权重数组的语义 | ❌ 待确认 |
| 强制 BOSS 机制（索引 27/28） | ✅ **已解出**（关卡 5/7 + 特定卡牌） |
| 卡牌 ID 0 与 66 的具体卡名 | ❌ 待查卡牌表 |
| 每场景的模板子集 | ❌ 未确认（场景如何筛选模板池） |

**`SYS-GAME-WAVE` 保持 `in-progress`**——表解出来了，
但权重与场景筛选还没验证。

---

## 9. 强制 BOSS 机制（本轮新解出）

### 9.1 之前误读的地方

上一轮我写：

> 「硬编码索引 27/28，条件 `[0x4D1004]` 等于 5 或 7」

**这个描述不完整。** 实际条件还要求**持有特定卡牌**。

### 9.2 完整逻辑

```asm
03D0E6: test dword ptr [ebp-0x644], 0x100   ; 某个标志位
03D0F6: je 0x3d156                          ; 未置位 -> 走正常抽取结果

03D0F8: mov eax, [0x4d1004]                 ; ★ 当前关卡号
03D0FD: cmp eax, 5
03D100: jne 0x3d128

; --- 分支 A：关卡 == 5 ---
03D102: mov eax, [0x4d7ab8]                 ; 卡牌管理器
03D107: mov ecx, [eax + 0x1c]               ; 卡牌链表头
03D10A: test ecx, ecx
03D10C: je 0x3d156                          ; 链表空 -> 正常抽取
03D110: mov eax, [ecx]                      ; 当前节点
03D112: cmp dword ptr [eax + 4], 0          ; ★ 卡牌 ID == 0 ?
03D116: je 0x3d121                          ; 命中
03D118: mov ecx, [ecx + 4]                  ; 下一个节点
03D11B: test ecx, ecx
03D11D: jne 0x3d110                         ; 继续遍历
03D11F: jmp 0x3d156                         ; 遍历完没找到 -> 正常抽取
03D121: mov esi, 0x1b                       ; ★ 强制索引 27

; --- 分支 B：关卡 == 7 ---
03D128: cmp eax, 7
03D12B: jne 0x3d156
03D12D: mov eax, [0x4d7ab8]
03D132: mov ecx, [eax + 0x1c]
03D135: test ecx, ecx
03D137: je 0x3d156
03D140: mov eax, [ecx]
03D142: cmp dword ptr [eax + 4], 0x42       ; ★ 卡牌 ID == 66 ?
03D146: je 0x3d151
03D148: mov ecx, [ecx + 4]
03D14B: test ecx, ecx
03D14D: jne 0x3d140
03D14F: jmp 0x3d156
03D151: mov esi, 0x1c                       ; ★ 强制索引 28

; --- 汇合 ---
03D156: push dword ptr [esi*4 + 0x4b5d50]   ; 用索引查名表
03D15D: mov dword ptr [0x4d1020], esi       ; ★ 记录选中的索引
```

### 9.3 等价伪代码

```c
int pick_wave_or_boss(EnemyManager* em, uint32_t flags) {
    int index = normal_pick(em);        // 上一节的权重抽取

    if (flags & 0x100) {                // 特殊模式标志
        uint32_t stage = g_stage_number;   // [0x4D1004]
        CardNode* node = g_card_manager->list_head;  // [+0x1C]

        if (stage == 5) {
            for (; node; node = node->next) {
                if (node->card->id == 0) {
                    index = 27;         // Boss26
                    break;
                }
            }
        } else if (stage == 7) {
            for (; node; node = node->next) {
                if (node->card->id == 66) {
                    index = 28;         // Boss27
                    break;
                }
            }
        }
    }

    g_selected_index = index;           // [0x4D1020]
    return index;
}
```

### 9.4 索引对应的 BOSS

| 索引 | 名称 | 触发条件 |
|---|---|---|
| `27` (`0x1B`) | **Boss26** | 关卡 == 5 且持有卡牌 ID **0** |
| `28` (`0x1C`) | **Boss27** | 关卡 == 7 且持有卡牌 ID **66** (`0x42`) |

> 索引与名称的换算：`[2] = Boss01`，所以 `[27] = Boss26`、`[28] = Boss27`。

### 9.5 数据结构

| 地址 | 类型 | 含义 |
|---|---|---|
| `0x4D1004` | uint32 | **当前关卡号**（不是对象基址！） |
| `0x4D7AB8` | ptr | 卡牌管理器 |
| `+0x1C`（相对卡牌管理器） | ptr | 卡牌链表头 |
| `+0x00`（相对链表节点） | ptr | 卡牌对象 |
| `+0x04`（相对链表节点） | ptr | 下一个节点 |
| `+0x04`（相对卡牌对象） | uint32 | **卡牌 ID** |
| `0x4D1020` | uint32 | **选中的波次/BOSS 索引**（输出） |

### 9.6 这一节的价值

**这是 TH185 剧情推进的机制。** 之前调研说「存在需要特殊操作才能推进剧情、
解锁场景的情况」——现在知道具体是什么了：

- 在**特定关卡**持有**特定卡牌**时，强制切换到**特定 BOSS**
- 这解释了为什么 THBWiki 会写「隐藏剧情解锁方式」需要单独一页说明

> ⚠️ 但**卡牌 ID 0 和 66 具体是哪张卡**还没确认。
> 需要卡牌表（`CARD_DESC_LIST` @ `0x4CA370`，thprac 已记录）来映射。
> 这是下一步可以做的。
