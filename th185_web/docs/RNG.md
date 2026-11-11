# TH185 随机数发生器（RNG）

> 状态：**算法已完全破解，纯 Python 重实现与探针 16 个种子逐位一致。**
> 日期：2026-11-11
> 分析对象：`th185.exe`（SHA-256 `76BBF844…345C7`）

---

## 1. 为什么这是最关键的子系统

TH185 的设计核心是「重复游玩逐渐变简单」，随机权重被**刻意放大**。
omake.txt 原文：「大きく違うのは攻略におけるランダムのウエイトです。……
短時間で繰り返し遊ぶ事を想定して、**わざと振り幅大きくしています**。」

依赖 RNG 的机制：

| 机制 | 随机点 |
|---|---|
| 波次抽取 | 从 **110 条模板池**按权重选，共 50 个槽位 |
| 黑市商品 | 每波结束 3 次购买，卡池随机 |
| Discount 判定 | 正式持有的卡与随机未持有卡享折扣 |
| 敌人行为 | ECL 脚本里的随机分支 |

**任何一处 RNG 调用次序的差异，都会让状态在第 N 帧后完全分叉。**
这就是为什么这个子系统必须做到逐位一致，而不是「差不多」。

---

## 2. 定位过程

### 2.1 失败的路径（先说，避免重复）

| 尝试 | 结果 |
|---|---|
| 搜 MSVC `rand` 乘数 `0x41C64E6D` | 0 处 |
| 搜 `0x9E3779B9`（黄金比例） | 0 处 |
| 搜 `imul r32, r32, imm32` 大奇数 | 只有 2 处，都不是 RNG |
| 搜 xorshift 三连位移特征 | 3 组，`0x1D79` 那组实为 PBG 位打包 |

**教训：TH185 用的不是标准 LCG，靠常量特征搜不到。**

### 2.2 成功的路径：从调用者反推

从 thprac 已知的**波次选择入口**出发。注意 thprac 的地址是 VA，
要减 `image_base` 才是 RVA：

```
thprac: 0x43CF47 (VA)  ->  真实 RVA 0x3CF47
```

反汇编 `0x3CF47`，找到关键片段：

```asm
03D046: mov ecx, 0x4d7aa8      ; ecx = RNG 对象
03D04B: mov [ebp-0x62c], edi
03D051: xor esi, esi
03D053: call 0x2800            ; ★ RNG 函数
03D058: mov ecx, [ebp-0x638]
03D05E: xor edx, edx
03D060: cmp [ebp-0x63c], esi   ; 总数 == 0 ?
03D066: je 0x3d0a8
03D068: div dword ptr [ebp-0x63c]  ; ★ 取模：EAX / 总数
03D06E: xor eax, eax
03D070: mov edi, edx           ; ★ edi = 余数 = 随机索引
```

**两个关键信息：**
1. RNG 函数在 **RVA `0x2800`**
2. RNG 对象在 **`0x4D7AA8`**（注意不是 thprac 记录的任何地址）
3. 取模方式是 `div` 后取 `edx`（余数）

---

## 3. RNG 函数反汇编（RVA 0x2800）

```asm
002800: cmp byte ptr [0x529ff6], 0   ; 调试开关
002807: push ebx
002808: push esi
002809: push edi
00280A: mov esi, ecx                 ; esi = this (RNG 对象)
00280C: je 0x281f                    ; 跳过临界区
00280E: push 0x529f88                ; 临界区对象
002813: call [0x4af07c]              ; EnterCriticalSection
002819: inc byte ptr [0x529ff2]      ; 递归锁计数++
00281F: mov ebx, 0x9630              ; ★ 常量 A
002824: add dword ptr [esi + 4], 2   ; ★ counter += 2
002828: mov eax, ebx
00282A: mov edx, 0x6553              ; ★ 常量 B
00282F: xor ax, word ptr [esi]       ; ax = A ^ seed
002832: sub ax, dx                   ; ax -= B
002835: movzx edi, ax                ; edi = ax（零扩展 32 位）
002838: mov cx, di
00283B: shr cx, 0xe                  ; cx = edi >> 14
00283F: lea eax, [edi*4]             ; ★ eax = edi * 4（32 位！）
002846: add cx, ax                   ; cx += eax（16 位截断）
002849: xor cx, bx                   ; cx ^= A
00284C: sub cx, dx                   ; cx -= B
00284F: movzx ebx, cx                ; ebx = cx
002852: mov cx, bx
002855: shr cx, 0xe                  ; cx = bx >> 14
002859: lea eax, [ebx*4]             ; eax = ebx * 4
002860: add cx, ax                   ; cx += eax
002863: mov word ptr [esi], cx       ; ★ 写回 seed
002866: cmp byte ptr [0x529ff6], 0
00286D: je 0x2880
00286F: push 0x529f88                ; LeaveCriticalSection
002874: call [0x4af080]
00287A: dec byte ptr [0x529ff2]
```

---

## 4. 数据结构

```c
struct RngState {
    uint16_t seed;      // +0x00  16 位种子（算法只操作低 16 位）
    uint16_t pad;       // +0x02
    uint32_t counter;   // +0x04  每调用一次 += 2
};
```

**全局实例在 `0x4D7AA8`。**

> `counter += 2` 而不是 `+1` 有点意外，但实测确认。
> 它可能是「已生成值的数量」用不同单位计数，或与帧同步有关。

---

## 5. 算法（纯 Python 重实现）

```python
CONST_A = 0x9630
CONST_B = 0x6553

def step(seed):
    a, b = CONST_A, CONST_B
    ax = ((a ^ seed) - b) & 0xFFFF
    cx = (((ax >> 14) + (ax * 4)) & 0xFFFF) ^ a
    cx = (cx - b) & 0xFFFF
    return ((cx >> 14) + (cx * 4)) & 0xFFFF
```

### 关键细节：`ax * 4` 是 32 位运算

汇编是 `lea eax, [edi*4]` —— `edi` 已经 `movzx` 成 32 位，
所以乘法在 32 位里做，**相加后才截断到 16 位**。

我第一次复现时把 `ax * 4` 也按 16 位截断，结果全部 MISMATCH。
这是唯一一个容易踩的点。

---

## 6. 验证证据

用 Unicorn 探针跑原版函数，与纯 Python 重实现对照：

```
seed = 0x1234
  [0] 0x1E86  [1] 0xD9A6  [2] 0x67B3  [3] 0x067D
  [4] 0x6217  [5] 0x203D  [6] 0xBE19  [7] 0xE060
  [8] 0xC1C5  [9] 0xDDA3  [10] 0xA782 [11] 0x07F1
  [12] 0x08D7 [13] 0x2C34 [14] 0x7E89 [15] 0xD95C
```

**16 个种子全部逐位一致。** 自检把前 8 个值硬编码，锁死算法不被无意改动。

---

## 7. 取模方式

原版在 `0x3D068` 用 `div` 后取 `edx`（余数）：

```asm
div dword ptr [ebp-0x63c]   ; EAX / 总数，商在 EAX，余数在 EDX
mov edi, edx                ; 余数 = 索引
```

**注意**：`div` 的被除数是 `EAX`（32 位），但 RNG 返回的种子只有 16 位。
所以实际是 `(uint32)(uint16 seed) % limit`。
重实现里 `next_below()` 采用 `value % limit`，其中 `value` 是 16 位种子值。

> 如果后续发现原版在取模前还有额外变换（例如拼接 counter），
> 需要在录像/状态比对中修正。当前按最简单解释实现。

---

## 8. 待确认项

| 项 | 状态 |
|---|---|
| 算法本体 | ✅ 逐位一致 |
| 常量 A/B | ✅ 0x9630 / 0x6553 |
| 状态结构 | ✅ seed(u16) + counter(u32) |
| 全局实例地址 | ✅ 0x4D7AA8 |
| 函数返回值语义 | ⚠️ 返回 EAX 是什么（0x1EB187A1 这种 32 位值）待确认 |
| 取模是否直接用种子 | ⚠️ 当前按最简单解释，需状态比对验证 |
| 种子初始化时机 | ❌ 未定位 |

**返回值观察**：调用后 `EAX` 是 `0x1EB187A1` 这样的 32 位值，
而种子是 `0x1E86`。两者关系不明——可能 `EAX` 是别的中间量，
或者函数尾部还有代码没看到（我只反汇编到 `0x287A`）。
**下次应该完整反汇编 `0x2800` 到函数结束。**

---

## 9. 工具

```powershell
# 自检（不需要 unicorn）
python th185_web/scripts/native/rng.py --selftest

# 生成序列
python th185_web/scripts/native/rng.py --seed 0x1234 --count 32

# 与原版对照（需要 unicorn）
python th185_web/scripts/native/rng.py --seed 0x1234 --count 16 --probe
```

---

## 10. 补全：返回值语义（本轮新破解）

之前只反汇编到 `0x287A`，漏了函数尾部。完整看到 `ret` 后才发现关键：

```asm
002866: cmp byte ptr [0x529ff6], 0
00286D: je 0x2880
00286F: push 0x529f88
002874: call [0x4af080]        ; LeaveCriticalSection
00287A: dec byte ptr [0x529ff2]
002880: mov eax, edi           ; ★ edi = 第一轮中间值
002882: pop edi
002883: shl eax, 0x10          ; ★ 左移 16 位
002886: pop esi
002887: or eax, ebx            ; ★ 拼接第二轮结果
002889: pop ebx
00288A: ret
```

### 结论：一次调用产出两个 16 位随机值

```c
uint32_t rng_next(RngState* self) {
    uint16_t ax = (uint16_t)((0x9630 ^ self->seed) - 0x6553);
    uint16_t edi = ax;
    uint16_t cx = (uint16_t)(((edi >> 14) + (uint32_t)edi * 4) ^ 0x9630);
    cx = (uint16_t)(cx - 0x6553);
    uint16_t ebx = cx;
    uint16_t cx2 = (uint16_t)((ebx >> 14) + (uint32_t)ebx * 4);
    self->seed = cx2;
    self->counter += 2;
    return ((uint32_t)edi << 16) | ebx;   // ★ 高 16 位 | 低 16 位
}
```

| 项 | 值 |
|---|---|
| 返回 `EAX` | `(edi << 16) \| ebx` |
| 高 16 位 | `edi` = 第一轮中间值 |
| 低 16 位 | `ebx` = 第二轮结果 = **新种子** |
| `counter` | `+= 2`（因为一次产出两个值） |

### 实测验证

| 调用 | EAX | 期望 | 种子 | 期望 |
|---|---|---|---|---|
| 0 | `0x1EB187A1` | `0x1EB187A1` ✅ | `0x1E86` | `0x1E86` ✅ |
| 1 | `0x2363B669` | `0x2363B669` ✅ | `0xD9A6` | `0xD9A6` ✅ |
| ... | ... | ... | ... | ... |
| 7 | `0xC2D63818` | `0xC2D63818` ✅ | `0xE060` | `0xE060` ✅ |

**8 次调用的 EAX 与种子全部逐位一致。**

### 取模的正确方式

原版在 `0x3D068` 用 `div` 取余，被除数是 **EAX（32 位）**，
不是 16 位种子。所以：

```python
index = rng_next() % limit      # 用 EAX 取模
# 不是 rng_step_seed() % limit
```

这是本轮修正的第二个点。之前的实现用 16 位种子取模，
当 `limit > 65536` 时结果会不同（虽然实际 `limit` 都小于这个值，
但语义上必须正确）。

---

## 11. 完整状态表

| 项 | 状态 | 证据 |
|---|---|---|
| 算法本体 | ✅ **逐位一致** | 16 个种子探针对照 |
| 常量 A/B | ✅ `0x9630` / `0x6553` | 反汇编 |
| 状态结构 | ✅ seed(u16) + counter(u32) | 反汇编 |
| 全局实例 | ✅ `0x4D7AA8` | `0x3D046 mov ecx, 0x4d7aa8` |
| 返回值语义 | ✅ `(edi<<16) \| ebx` | 完整反汇编 + 实测 |
| 取模方式 | ✅ EAX % limit | `0x3D068 div` |
| 种子初始化 | ❌ 未定位 | — |

**`SYS-CORE-RNG` 可以标 `complete` 了**——算法层面已无未知。
剩余「种子初始化」属于会话层，不在 RNG 子系统范围内。
