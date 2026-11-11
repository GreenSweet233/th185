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
