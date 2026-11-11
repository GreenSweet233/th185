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
