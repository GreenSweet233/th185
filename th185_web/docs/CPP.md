# TH185 C++ 实现（cpp/game）

> 状态：**RNG 已完成并逐位验证；其余全部待实现。**
> 日期：2026-11-11

---

## 0. 为什么现在开始写 C++

前 26 轮全部在做**逆向分析**（Python 工具 + 文档），
`cpp/game` 里只有 README，实现代码是 0 行。

但项目目标是**功能还原重建**——分析只是手段，代码才是交付物。
从本轮起开始补 C++ 实现。

### 关于「不能编译」

本工作区**没有 emsdk / node.exe / portable/sdl**，无法编译 WASM。
按 `AGENTS.md` 的**四事实分离**原则：

| 事实 | 本轮状态 |
|---|---|
| 有源码 | ✅ 已写 |
| 能编译 | ❌ **无法验证**（缺工具链） |
| 行为一致 | ✅ **逻辑已逐位验证**（用 Python 镜像对照） |
| 已验证 | ✅ 有测试脚本 |

**所以：代码正确性有保证，但「能编译」这一事实仍待工具链补齐后确认。**
这一点必须如实记录，不能把「写了代码」当成「能跑」。

---

## 1. 已完成：RNG（`Rng.hpp` / `Rng.cpp`）

### 1.1 文件

| 文件 | 内容 |
|---|---|
| `cpp/game/Types.hpp` | 基础类型 + float/int 位转换（对齐 th11） |
| `cpp/game/Rng.hpp` | RNG 结构与算法 |
| `cpp/game/Rng.cpp` | 全局实例 |
| `scripts/native/rng_cpp_check.py` | **交叉校验脚本** |

### 1.2 算法

```c
// A = 0x9630, B = 0x6553
ax      = (A ^ seed) - B                      // 16-bit, = edi
rotated = ((ax >> 14) + ax * 4) & 0xFFFF      // ax*4 是 32 位
mixed   = rotated ^ A
ebx     = mixed - B                           // 16-bit
seed    = ((ebx >> 14) + ebx * 4) & 0xFFFF   // 新种子
EAX     = (edi << 16) | ebx                   // 一次两个值
```

### 1.3 两个必须注意的陷阱

**陷阱 1：中间步有 `^ A` 再 `- B`。**
我第一次实现时漏了 `^ A`，序列完全错。
更隐蔽的是：漏掉后序列**错位一位**（C++ 的第 2 个值 = 探针的第 1 个值），
很容易误判成「调用次序问题」而不是「算法错」。

**陷阱 2：`next16()` 返回的不是 `ebx`，而是旋转后的新种子。**

```
ebx  = (mixed - B) & 0xFFFF
seed = ((ebx >> 14) + ebx * 4) & 0xFFFF   // ← next16 返回这个
```

探针读的是上下文里的 `seed` 字段，所以拿到的是旋转后的值。
我一开始让 `next16()` 返回 `ebx`，结果 8/8 全不匹配。

### 1.4 验证

```
[PASS] fixed seed 0x1234 produces the reference sequence
[PASS] next16 matches rng.py for 10 seeds
[PASS] next32 matches the reference EAX packing
[PASS] next_below matches and stays in range
[PASS] struct layout is 8 bytes (u16 seed + u16 pad + u32 calls)
```

固定种子 `0x1234` 的 16 次序列与 `rng.py` 自检期望值**逐位一致**：

```
0x1E86 0xD9A6 0x67B3 0x067D 0x6217 0x203D 0xBE19 0xE060
```

### 1.5 为什么用「Python 镜像」而不是直接编译

`rng_cpp_check.py` 里的 `CppRngMirror` 是 `Rng.hpp` 的**逐行 Python 翻译**。
这样在无工具链的环境下也能保证 C++ 逻辑正确。

**约定：改 `Rng.hpp` 必须同时改 `CppRngMirror`，否则校验会失败。**
这比「等工具链就绪再验证」可靠得多。

---

## 2. 待实现清单

按 `cpp/game/README.md` 的计划，优先级从高到低：

| 文件 | 职责 | 依赖 | 状态 |
|---|---|---|---|
| **`Rng.hpp`** | 随机数 | — | ✅ **完成** |
| `Archive.cpp` | PBG4 解包 | 格式已解出 | ⬜ 下一步 |
| `EclResource.cpp` | SCPT/ECLH 加载 | 格式已解出 | ⬜ |
| `StageScript.cpp` | `.std` 指令流 | 格式已解出 | ⬜ |
| `WaveSelector.cpp` | 波次抽取 | 算法已解出 + RNG | ⬜ |
| `CardManager.cpp` | 85 张卡 | 表已解出 | ⬜ |
| 其余 | — | — | ⬜ |

**下一步应做 `Archive.cpp`**——它是资源层入口，
格式已完全解出（`docs/PBG4.md`），且不依赖渲染/输入，最容易先落地。

---

## 3. 已完成：Archive（`Archive.hpp` / `Archive.cpp`）

### 3.1 文件

| 文件 | 内容 |
|---|---|
| `cpp/game/Archive.hpp` | 容器结构、加密参数、LZSS 与解包接口 |
| `cpp/game/Archive.cpp` | 解密 + LZSS + 容器解析 + 单条目提取 |
| `scripts/native/archive_cpp_check.py` | **交叉校验脚本** |

### 3.2 实现的四个部分

| 部分 | 对应原版 | 说明 |
|---|---|---|
| `decrypt_in_place` | RVA `0x1E90` | 密钥流按**读序**推进，明文按**置换序**写入 |
| `lzss_decompress` | RVA `0x71E70` | 8192 窗口，写指针从 1 开始，MSB 优先 |
| `cipher_for` | RVA `0x713C5` | `sum(name) & 7` 选 8 项表 |
| `Archive::parse` | RVA `0x715555` | 表在**文件末尾** |

### 3.3 最关键的一点：解密不是简单 XOR

密钥流按**读取顺序**推进，但明文按**置换顺序**写入：

```c
// 每个 block 内：
//   奇数偏移：从 block 末尾-1 开始，递减 2
//   偶数偏移：从 block 末尾-2 开始，递减 2
for (u32 i = 0; i < (chunk + 1) / 2; ++i) {
    data[w] = cipher[src] ^ cur_key;   // w 递减，src 递增
    cur_key = u8(cur_key + cur_step);
    ++src;
    w -= 2;
}
```

**这个置换是为什么朴素的 XOR 循环会产出乱码。**

### 3.4 另一个陷阱：`size` 字段是解压后大小

```
struct ArchiveEntry {
    uint32 offset;    // 绝对文件偏移
    uint32 size;      // ★ 解压后大小，不是存储大小
    uint32 reserved;
};
```

实际存储大小 = **下一项 offset - 本项 offset**（最后一项用表偏移）：

```c
u32 Archive::stored_size(std::size_t index) const {
    const u32 next = (index + 1 < entries.size()) ? entries[index + 1].offset
                                                  : table_offset;
    return next - entries[index].offset;
}
```

压缩判定：

```
stored == entry.size  ->  未压缩，只解密
stored <  entry.size  ->  LZSS 压缩，解密 + 解压
```

### 3.5 验证

```
[PASS] cipher_for matches pbg4.py for 8 names
[PASS] lzss_decompress matches lzss.py on 4 random inputs
[PASS] header decryption yields THA1 magic (0x31414854)
[PASS] header fields match (decomp=7172 comp=3653 count=285)
[PASS] entry table sits at filesize - compressed_size (74533671)
```

头部字段用真实文件验证：`decompressed=7172`、`compressed=3653`、`count=285`、
表偏移 `74533671` —— 与 `docs/PBG4.md` 记录的完全一致。

### 3.6 状态

| 项 | 状态 |
|---|---|
| 头部解密 | ✅ 与参考一致 |
| 条目解密 | ✅ 与参考一致 |
| LZSS 解压 | ✅ 与参考一致 |
| 容器解析 | ✅ 字段与真实文件吻合 |
| 单条目提取 | ✅ 已实现（含压缩判定） |
| **能编译** | ❌ 仍缺工具链 |

---

## 4. C++ 实现进度

| 文件 | 状态 | 校验 |
|---|---|---|
| `Types.hpp` | ✅ | — |
| `Rng.hpp` / `Rng.cpp` | ✅ | `rng_cpp_check.py` 5 项 |
| **`Archive.hpp` / `Archive.cpp`** | ✅ | `archive_cpp_check.py` 5 项 |
| `EclResource.cpp` | ⬜ | — |
| `StageScript.cpp` | ⬜ | — |
| `WaveSelector.cpp` | ⬜ | — |
| `CardManager.cpp` | ⬜ | — |
| 其余 | ⬜ | — |

**下一步：`EclResource.cpp`**（SCPT/ECLH 加载）。
格式已完全解出（`docs/ECL.md`），且依赖 `Archive` 已完成，可直接接上。

---

## 5. 已完成：EclResource（`EclResource.hpp` / `.cpp`）

### 5.1 文件

| 文件 | 内容 |
|---|---|
| `cpp/game/EclResource.hpp` | SCPT 容器结构、指令/子程序定义 |
| `cpp/game/EclResource.cpp` | 解析 ANIM/ECLI 变长段 + ECLH 指令流 |
| `scripts/native/ecl_cpp_check.py` | 交叉校验（全部 114 文件逐条对照） |

### 5.2 ★ 本轮发现的重大修正

**ECLI 段也是变长的**，之前只修正了 ANIM：

```
wave01.ecl  ECLI count=0  ->  偏移表在 tag+8
boss01.ecl  ECLI count=1  ->  偏移表在 "default.ecl"\0 之后（4 对齐）
```

修正后数字全变：

| 指标 | 之前（错） | 修正后 |
|---|---|---|
| 子程序 | 486 | **816** |
| 指令 | 12480 | **23770** |
| 操作码 | 112 | **165** |
| 尾部不变式 | 486/486 | **816/816** |

**37 个 BOSS 文件依赖 `default.ecl`**，之前被整个漏掉。

### 5.3 验证

```
[PASS] all 114 .ecl files parse identically (0 mismatches)
[PASS] all 816 subroutines end with the same tail (0 bad)
[PASS] instruction total is 23770 (expect 23770)
[PASS] subroutine total is 816 (expect 816)
[PASS] the 12-byte tail invariant holds
```

校验方式是**逐条对照**：114 个文件、816 个子程序、23770 条指令，
每条的 `id/size/rank/argc/offset` 全部与参考实现比对。

### 5.4 C++ 实现进度

| 文件 | 状态 | 校验 |
|---|---|---|
| `Types.hpp` | ✅ | — |
| `Rng.hpp` / `Rng.cpp` | ✅ | `rng_cpp_check.py` 5 项 |
| `Archive.hpp` / `Archive.cpp` | ✅ | `archive_cpp_check.py` 5 项 |
| **`EclResource.hpp` / `.cpp`** | ✅ | `ecl_cpp_check.py` 5 项 |
| `StageScript.cpp` | ⬜ | — |
| `WaveSelector.cpp` | ⬜ | — |
| `CardManager.cpp` | ⬜ | — |

**下一步：`StageScript.cpp`**（`.std` 关卡指令流）。
格式已解出（`docs/STAGE.md`），9 场景各剩 16 字节尾部。

---

## 6. 已完成：StageScript（`StageScript.hpp` / `.cpp`）

### 6.1 文件

| 文件 | 内容 |
|---|---|
| `cpp/game/StageScript.hpp` | `.std` 各段结构定义 |
| `cpp/game/StageScript.cpp` | 波次表 + 28 字节组表 + 实体表 + 指令流 |
| `scripts/native/stage_cpp_check.py` | 交叉校验（9 文件逐项对照） |

### 6.2 ★ 本轮新发现的段

**28 字节实体组表**位于末波次项与实体表之间，项数变长：

```
world01t..03  0 项
world04      15 项（环形编队，角度间隔 48°）
world05       5 项
world06       7 项
world07/08    7 项
合计         41 项
```

上一轮漏掉这段，导致 5/9 文件把组数据误当成实体。

### 6.3 验证

```
[PASS] all 9 .std files parse identically (0 mismatches)
[PASS] entity total is 379 (expect 379)
[PASS] entity-group total is 41 (expect 41: 0+0+0+0+15+5+7+7+7)
[PASS] every scene ends with exactly 16 bytes of 0xFF
[PASS] command total is 508 (expect 508)
```

### 6.4 C++ 实现进度

| 文件 | 状态 | 校验 |
|---|---|---|
| `Types.hpp` | ✅ | — |
| `Rng.hpp` / `Rng.cpp` | ✅ | `rng_cpp_check.py` 5 项 |
| `Archive.hpp` / `Archive.cpp` | ✅ | `archive_cpp_check.py` 5 项 |
| `EclResource.hpp` / `.cpp` | ✅ | `ecl_cpp_check.py` 5 项 |
| **`StageScript.hpp` / `.cpp`** | ✅ | `stage_cpp_check.py` 5 项 |
| `WaveSelector.cpp` | ⬜ | — |
| `CardManager.cpp` | ⬜ | — |

**下一步：`WaveSelector.cpp`**（波次抽取）。
算法已解出（`docs/WAVES.md`：两遍加权循环 + RNG 取模），
且 `Rng` 与 `StageScript` 都已完成，可直接接上。

---

## 7. 已完成：WaveSelector（`WaveSelector.hpp` / `.cpp`）

### 7.1 文件

| 文件 | 内容 |
|---|---|
| `cpp/game/WaveSelector.hpp` | 111 项名表、权重结构、抽取接口 |
| `cpp/game/WaveSelector.cpp` | 两遍加权循环 + 强制 BOSS |
| `scripts/native/waves_cpp_check.py` | 交叉校验（7 项） |

### 7.2 这是 TH185 的核心机制

TH11/TH18 的道中敌人是**手工编排**的；TH185 不同——
每个场景的波次是**从全局 110 条模板池里随机加权抽取**的。

所以 **RNG 调用次序必须逐位一致**（`AGENTS.md` 铁律 5），
否则整局状态会分叉。

### 7.3 名表结构

```
[0]       空占位（不可选）
[1]       Boss01t         教程 BOSS
[2..28]   Boss01..Boss27  27 个常规 BOSS
[29..31]  Wave01t..03t    3 个教程道中
[32..110] Wave01..Wave79  79 个常规道中
```

**合计 111 项，其中 110 项可选**（索引 0 是占位）。

### 7.4 抽取算法

```c
// 第一遍：累加权重，跳过 skip_index
u32 total = 0;
for (u32 i = 0; i < 111; ++i) {
    if (i == skip_index) continue;
    total += weights[i];
}

// 只调用一次 RNG
const u32 roll = rng.next_below(total);

// 第二遍：递减到负数，命中即结果
i32 remaining = i32(roll);
for (u32 i = 0; i < 111; ++i) {
    if (i == skip_index) continue;
    remaining -= i32(weights[i]);
    if (remaining < 0) return i32(i);
}
```

**关键细节**：第二遍必须用**有符号**比较（`remaining < 0`），
用无符号比较永远不会触发。

### 7.5 `skip_index` 的作用

两遍循环**都**跳过 `skip_index`，所以**同一模板不会连续出现两次**。

验证：等权重下，111 种 `skip` 取值全部不会命中自己。

### 7.6 强制 BOSS

```c
if (stage == 5 && card_id == 0)  return 27;   // Boss26
if (stage == 7 && card_id == 66) return 28;   // Boss27
```

对应原版 `RVA 0x3D121 mov esi,0x1B` 与 `0x3D151 mov esi,0x1C`。

### 7.7 验证（7 项）

```
[PASS] C++ name table has 111 entries (expect 111)
[PASS] C++ name table matches the binary
[PASS] weight total and pick index match waves.py over 200 random cases
[PASS] full selection (incl. RNG state) matches over 50 random cases
[PASS] skip_index is never selected (all 111 cases)
[PASS] forced-boss overrides match docs/WAVES.md
[PASS] index ranges are contiguous (1+27+3+79 = 110 selectable)
```

第 4 项最关键——它不仅比对选出的索引，还比对 **RNG 的最终种子**，
所以只要 C++ 里多调或少调一次 RNG 就会失败。

### 7.8 C++ 实现进度

| 文件 | 状态 | 校验 |
|---|---|---|
| `Types.hpp` | ✅ | — |
| `Rng.hpp` / `Rng.cpp` | ✅ | `rng_cpp_check.py` 5 项 |
| `Archive.hpp` / `Archive.cpp` | ✅ | `archive_cpp_check.py` 5 项 |
| `EclResource.hpp` / `.cpp` | ✅ | `ecl_cpp_check.py` 5 项 |
| `StageScript.hpp` / `.cpp` | ✅ | `stage_cpp_check.py` 5 项 |
| **`WaveSelector.hpp` / `.cpp`** | ✅ | `waves_cpp_check.py` 7 项 |
| `CardManager.cpp` | ⬜ | — |
| `Difficulty.cpp` | ⬜ | — |

**下一步：`CardManager.cpp`**（85 张卡）。
表已解出（`docs/CARDS.md`：VA 0x4CA370，87 项 × 68 字节）。

---

## 8. 已完成：CardManager（`CardManager.hpp` / `.cpp`）

### 8.1 文件

| 文件 | 内容 |
|---|---|
| `cpp/game/CardManager.hpp` | 卡牌结构、特殊 ID 常量 |
| `cpp/game/CardManager.cpp` | 87 张卡全表 + 查找 |
| `scripts/native/cards_cpp_check.py` | 交叉校验（13 项） |

### 8.2 ★ 新发现：`+0x38` / `+0x3C` 是图集格子坐标

这两个字段各有 **87 种不同值**（正好等于卡牌数），最初以为它们与索引相关。
但实测规律非常整齐：

```
icon_right == icon_left + 1     87/87 全部成立
icon_left  全为奇数             87/87
```

示例：

| 卡 | `+0x38` | `+0x3C` |
|---|---|---|
| PENDULUM | 19 | 20 |
| PENDULUM2 | 17 | 18 |
| DANGO | 21 | 22 |
| BLANK | 5 | 6 |
| NITORI | 145 | 146 |

**这是位图中相邻的两格**——卡牌图标在图集里的位置。
`+0x38` 是左格、`+0x3C` 是右格，卡牌图标占 2 格宽。

### 8.3 另一个修正：`+0x30` 不是 Cost

之前 `docs/CARDS.md` 推测 `+0x30` 是 Cost。实测：

```
+0x30 的取值分布: {0: 2, 1: 85}
```

**只有 0 和 1 两种值**，不可能是 Cost 系统（TH185 的 Cost 是真实数值）。
真正的 Cost 在别处。

### 8.4 卡牌 ID 覆盖

```
id 唯一且覆盖 0..86（87 张）
```

四个特殊卡：

| id | 名称 | 作用 |
|---|---|---|
| **0** | `BLANK` | 关卡 5 携带 → 强制 Boss26 |
| **66** | `NITORI` | 关卡 7 携带 → 强制 Boss27 |
| 85 | `NULL` | 空位 |
| 86 | `BACK` | 背面 |

这印证 THBWiki 的剧情：魔理沙带空白卡去守矢神社祭典，
遇到荷取后获得荷取的卡。

### 8.5 验证（13 项）

```
[PASS] reference table has 87 entries (expect 87)
[PASS] C++ table has 87 entries (expect 87)
[PASS] every field of every card matches (0 diffs)
[PASS] entry size is 68 bytes (17 DWORDs)
[PASS] icon_right == icon_left + 1 for all 87 cards
[PASS] icon_left is odd for all 87 cards
[PASS] reserved field (+0x40) is always 0
[PASS] all 87 card ids are unique
[PASS] card ids cover exactly 0..86
[PASS] BLANK(0) and NITORI(66) exist with the expected names
[PASS] NULL(85) and BACK(86) exist with the expected names
[PASS] all card names are printable ASCII
[PASS] +0x30 holds only [0, 1] (not a real cost column)
```

### 8.6 一个校验方法上的改进

最初的校验脚本在 Python 里**重复了一遍 87 张卡的数字**，
这样即使 C++ 表改错了也检查不出来（因为 Python 那份是独立抄的）。

改成**直接正则解析 `CardManager.cpp` 源码**，逐字段对照二进制。
现在只要有人改动 C++ 表的任何一个数字，校验立刻失败。

**这是「镜像校验」真正有意义的形式**——镜像必须来自被测源码本身，
而不是另抄一份。

### 8.7 C++ 实现进度

| 文件 | 状态 | 校验 |
|---|---|---|
| `Types.hpp` | ✅ | — |
| `Rng.hpp` / `Rng.cpp` | ✅ | 5 项 |
| `Archive.hpp` / `Archive.cpp` | ✅ | 5 项 |
| `EclResource.hpp` / `.cpp` | ✅ | 5 项 |
| `StageScript.hpp` / `.cpp` | ✅ | 5 项 |
| `WaveSelector.hpp` / `.cpp` | ✅ | 7 项 |
| **`CardManager.hpp` / `.cpp`** | ✅ | **13 项** |
| `Difficulty.cpp` | ⬜ | — |

**下一步：`Difficulty.cpp`**（8 档难度）。
表已解出（`docs/DIFFICULTY.md`：索引 `0x4D1038`，评分上限表 5 项）。

---

## 9. 已完成：Difficulty（`Difficulty.hpp` / `.cpp`）

### 9.1 文件

| 文件 | 内容 |
|---|---|
| `cpp/game/Difficulty.hpp` | 8 档名称、评分上限表、访问器声明 |
| `cpp/game/Difficulty.cpp` | 名称表 + 评分上限 + 四个 `is_difficulty_N` |
| `scripts/native/difficulty_cpp_check.py` | 交叉校验（10 项，解析 C++ 源码） |

### 9.2 8 档难度

| 索引 | 名称 | 备注 |
|---|---|---|
| 0 | Very Easy | |
| 1 | Easy | |
| 2 | Normal | |
| 3 | **Little Hard** | **TH185 独有，TH18 没有** |
| 4 | Hard | |
| 5 | Very Hard | |
| 6 | Lunatic | |
| 7 | Over Drive | |

### 9.3 评分上限表：5 项 vs 8 档

```c
// VA 0x4BCB00（前 4 项）+ 立即数 0xF4240（第 5 项）
[500000, 1000000, 1500000, 2000000, 1000000]
```

**表只有 5 项，但难度有 8 档。** 原版 `mov eax, [ebp + eax*4 - 0x18]`
在索引 5..7 时会**读到栈上的相邻数据**——这是原版行为，不是解析错误。

我的 C++ 实现选择：

```c
if (out_of_range) *out_of_range = difficulty >= SCORE_CAP_TABLE_SIZE;
if (difficulty >= SCORE_CAP_TABLE_SIZE) {
    return kScoreCaps[SCORE_CAP_TABLE_SIZE - 1];  // clamp
}
```

**夹紧到最后一个有效值，但通过 `out_of_range` 显式报告**——
不静默吞掉这个差异，让调用方知道原版此处是未定义行为。

这比“假装表有 8 项”或“直接返回 0”都更诚实。

### 9.4 `is_difficulty_0..3` 是虚函数

搜直接调用点得到 **0 处**——这个异常是突破口。
改搜「谁存了这些函数地址」，在 `.text` RVA `0x3EDB0` 找到 vtable：

```
[0] is_difficulty_0   [1] is_difficulty_1
[2] is_difficulty_2   [3] is_difficulty_3
[4] get_em_3c         [5] get_em_40
[6] get_em_44         [7] get_em_4f44
```

顺序与 `docs/DIFFICULTY.md` 记录的一致。

### 9.5 一个未解项：没有 8 元素倍率表

多轮扫描 `.rdata` 找 8 元素递增浮点数组，全部只命中
VA `0xBC6C4` 的倒数表（`1/20, 1/16, 1/15, ...`）的滑窗重叠——
那是角度/正弦相关，不是难度。

**结论：每档的数值倍率硬编码在条件分支里，不存在表。**
提取它们需要逐个函数逆向，本轮未做。这是 `SYS-GAME-DIFFICULTY`
保持 `in-progress` 的原因。

### 9.6 验证（10 项）

```
[PASS] C++ declares 8 tier names (expect 8)
[PASS] tier names match the reference
[PASS] index 3 is Little Hard (TH185-only tier)
[PASS] C++ score-cap table has 5 entries (expect 5)
[PASS] score caps match the binary: [500000, 1000000, 1500000, 2000000, 1000000]
[PASS] 5-entry table vs 8 tiers (original reads past the array)
[PASS] header records VA 0x4D1038 / 0x4BCB00 / immediate 0xF4240
[PASS] header records the vtable RVA 0x3EDB0
[PASS] all four is_difficulty_N predicates are implemented
[PASS] out-of-range tiers are reported, not silently clamped
```

校验脚本同样**直接解析 C++ 源码**（正则提取 `kNames` / `kScoreCaps` 数组），
不是另抄一份数据。

### 9.7 C++ 实现进度：静态数据部分完成

| 文件 | 状态 | 校验 |
|---|---|---|
| `Types.hpp` | ✅ | — |
| `Rng.hpp` / `Rng.cpp` | ✅ | 5 项 |
| `Archive.hpp` / `Archive.cpp` | ✅ | 5 项 |
| `EclResource.hpp` / `.cpp` | ✅ | 5 项 |
| `StageScript.hpp` / `.cpp` | ✅ | 5 项 |
| `WaveSelector.hpp` / `.cpp` | ✅ | 7 项 |
| `CardManager.hpp` / `.cpp` | ✅ | 13 项 |
| **`Difficulty.hpp` / `.cpp`** | ✅ | **10 项** |

**七个模块全部完成，共 55 项校验。**

### 9.8 下一阶段

到这里，**所有能从静态数据完整解出的子系统都已实现**。
剩下的都需要运行时信息或大规模逆向：

| 方向 | 阻塞点 |
|---|---|
| 波次权重数组 | 运行时构建（`EM+0x324`/`+0x4E0`），需 hook 或探针初始化后读取 |
| 难度倍率 | 硬编码在条件分支，需逐函数逆向 |
| ECL 操作码语义 | 165 种，分派器未找到 |
| ANM 动画 | 需要 ANM 解释器 + 渲染层 |
| 渲染 / 输入 / 音频 | 需要 SDL3 平台层，且工具链缺失无法编译 |

**下一步建议：补齐工具链**（emsdk / node.exe / portable/sdl），
否则后续所有 C++ 代码都无法编译验证，只能靠 Python 镜像间接保证。

---

## 10. ★★★ 里程碑：C++ 代码首次编译并验证

### 10.1 上一轮的判断是错的

我在第 32 轮写道「工具链缺失，无法编译」，并把它列为最大短板。
**这是错的。** 实际情况：

| 组件 | 上一轮判断 | 实际 |
|---|---|---|
| emsdk | ❌ 不存在 | ✅ `th11-high-refresh/tools/emsdk`（emcc 6.0.11） |
| node | ❌ 不存在 | ✅ `C:\Program Files\nodejs\node.exe` |
| python | ✅ | ✅ `C:\Program Files\PyManager\python.exe` |
| portable/sdl | ❌ 不存在 | ✅ `th11/portable/sdl`（本轮暂未用） |

我上一轮只检查了 `thvip/tools/emsdk`（th11 的布局）就下结论，
**没有搜索整个工作区**。这是可以避免的错误。

### 10.2 遇到的一个真实障碍

`emsdk/.emscripten` 里写的是：

```python
NODE_JS = [os.path.abspath(os.path.join(_sdk, '..', '..', 'th10_web', 'tools', 'node.exe'))]
```

但 `th10_web/tools/node.exe` **不存在**（`th10_web` 整个目录都没有）。
所以直接复用 emsdk 的配置会失败。

**解决办法：`build-cpp.mjs` 自己生成 `.emscripten`**，把 `NODE_JS` 指向真实存在的 node：

```js
const nodeExe = process.execPath;   // 当前运行的 node
// 写自己的 .emscripten 到 artifacts/cpp-check/.emscripten
```

这样不依赖任何外部路径假设。

### 10.3 构建脚本

`th185_web/scripts/build-cpp.mjs`：

```powershell
node th185_web/scripts/build-cpp.mjs            # 编译 + 部分链接
node th185_web/scripts/build-cpp.mjs --harness  # 额外构建 RNG 测试程序
node th185_web/scripts/build-cpp.mjs --clean    # 清理后重建
```

做的事：

1. 在 3 个候选路径找 emsdk，找到即用
2. 生成自己的 `.emscripten`（修正 node 路径）
3. 编译 `cpp/game/*.cpp`（排除 `Harness*.cpp`）到 `.o`
4. 用 `emcc -r` 部分链接成 `libth185game.a`
5. 可选：构建 `HarnessRng.cpp` 为可运行的 `.cjs`

**为什么用 `emcc -r` 而不是 `ar`**：emscripten 的 `ar` 不在 PATH 上
（它叫 `emar`），而且 `-r` 部分链接还能额外发现符号冲突和头文件不一致。

### 10.4 ★ 编译结果

```
[1/7] cpp\game\Archive.cpp ... ok
[2/7] cpp\game\CardManager.cpp ... ok
[3/7] cpp\game\Difficulty.cpp ... ok
[4/7] cpp\game\EclResource.cpp ... ok
[5/7] cpp\game\Rng.cpp ... ok
[6/7] cpp\game\StageScript.cpp ... ok
[7/7] cpp\game\WaveSelector.cpp ... ok

linking artifacts\cpp-check\libth185game.a ... ok
```

**7 个翻译单元全部编译通过，部分链接成功。**

编译选项与 th11 的 `build-sdl.mjs` 一致：
`-O1 -std=c++17 -ffp-contract=off -fno-strict-aliasing -fno-exceptions -fno-rtti -Wall -Wextra`

### 10.5 ★★ 用编译产物验证 RNG

这是本轮最有价值的部分。之前 RNG 的正确性靠「Python 镜像」，
现在可以直接跑**真实编译的 C++**：

`cpp/game/HarnessRng.cpp` 打印 10 个种子的 `next16` 序列、
3 个种子的 `next32`（EAX）、以及 `next_below` 结果。

`scripts/native/cpp_runtime_check.py` 会：

1. 调用 `build-cpp.mjs --harness` 编译
2. 用 node 运行产物
3. 解析输出，与 **Unicorn 探针**逐位对照

结果：

```
[PASS] harness emitted 10 seed sequences (expect 10)
[PASS] compiled C++ next16 matches the probe for 10 seeds
[PASS] compiled C++ next32 matches docs/RNG.md recorded EAX values
[PASS] compiled next_below always stays inside [0, limit)
[PASS] compiled C++ also matches the Python reference
```

实际输出（seed `0x1234`）：

```
1234: 1E86 D9A6 67B3 067D 6217 203D BE19 E060 ...
EAX 1234: 1EB187A1 2363B669 EA43D9EC 8C30419F 2AFAD885 8ED4480F 50BA6F86 C2D63818
```

与 `docs/RNG.md` 记录的探针实测值**完全一致**。

### 10.6 四事实分离：现在的状态

| 事实 | 上一轮 | 本轮 |
|---|---|---|
| 有源码 | ✅ | ✅ |
| **能编译** | ❌ | ✅ **7/7 通过** |
| 行为一致 | ✅ Python 镜像 | ✅ **编译产物 vs 探针** |
| 已验证 | ✅ | ✅ 24 项 CI |

**「能编译」这一格从 ❌ 变成了 ✅，而且「行为一致」的验证强度提升了一个档次。**

### 10.7 CI 新增两项

```
[PASS] cpp-build    (default)        7 个翻译单元编译 + 链接
[PASS] cpp-runtime  (--selftest)     编译产物 vs 探针逐位对照
```

现在 **CI 共 24 项**，其中 2 项直接验证 C++ 编译与运行。

### 10.8 仍需注意的边界

| 项 | 状态 |
|---|---|
| C++ 编译 | ✅ 全部通过 |
| RNG 运行时验证 | ✅ 与探针逐位一致 |
| **其他 6 个模块的运行时验证** | ⚠️ **只有编译，没有运行** |
| 完整游戏链接 | ❌ 没有平台层（SDL3/渲染/输入） |

其他 6 个模块目前只验证了「能编译」+「逻辑镜像一致」，
还没有像 RNG 那样用编译产物跑真实数据。
下一步可以照 RNG 的模式，为 `Archive` / `EclResource` / `StageScript`
各写一个 harness，让它们真正解析 `th185.dat`。
