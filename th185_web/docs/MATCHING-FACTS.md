# 字节级匹配侧的实测事实（本仓库的独立副本）

> **来源**：`th185-matching` @ `f1650e2`（2026-11-24），读一次后转写。
> **本文件是本仓库自己的副本**，不构成对 `th185-matching` 的依赖：
> 只引用公开的提交号，任何一个仓库单独 clone 都能自洽。
> 上游数字会过时，本文件只记录**我实际复核过**的部分，复核方式写在每节末尾。

本文件为什么存在：功能还原（本仓库）不做字节级匹配，但**重实现浮点接口、
对象布局、成员函数形态**时，会踩到与匹配侧相同的坑。
把这些已知结论抄过来，能少走弯路。

---

## 1. 工具链事实（已复核）

### 编译器

| 项 | 值 |
|---|---|
| MSVC | v142 **19.29.30159** (x86) |
| 实测路径 | `D:\MSVC142\14.29.30133` |
| 来源 | 从 `C:\ProgramData\Microsoft\VisualStudio\Packages` 的 `.vsix` 解压 |
| 编译选项 | `/MT /O2 /GS- /Oy-` |

**与功能还原的关系：无直接关系。** 本仓库用 Emscripten/clang 编译，不锁 MSVC。
记下来只是为了理解原版机器码的形态（比如为什么会有 x87 与 SSE 混用）。

### Emscripten（★ 本仓库实际在用，本节为本次实测新增）

| 项 | 值 |
|---|---|
| emsdk 位置 | `D:\Windows\GreenSweet\Game\Tasks\thvip\tools\emsdk` |
| 布局 | **releases-\* 风格**：工具链在 `<sdk>/upstream/{bin,emscripten}` |
| `install/` 内容 | 只有一个 `emscripten` **junction** → `upstream/emscripten` |
| clang 实际位置 | `<sdk>/upstream/bin/clang.exe` |
| ⚠️ 不存在 | `<sdk>/install/bin/` —— **这个目录根本不存在** |

**踩过的坑**：`build-cpp.mjs` 原本假设旧布局（`install/bin`），
于是 emcc 报 `clang executable not found at .../install/bin/clang`，
CI 的 `cpp-build` 项直接失败。修法是改为**探测 clang.exe 在哪**再推导
LLVM_ROOT / BINARYEN_ROOT，而不是按命名约定猜。见 `scripts/build-cpp.mjs` 的 `layoutOf()`。

**复核方式**：`Test-Path <sdk>\install\bin\clang.exe` → False；
`Test-Path <sdk>\upstream\bin\clang.exe` → True；修后 `node th185_web/scripts/build-cpp.mjs` exit 0。

---

## 2. MSVC 代码生成规则（5 条，转录，未在本仓库复核）

| # | 规则 | 对功能还原有用吗 |
|---|---|---|
| 1 | `/Oy-` 保留栈帧 | ❌ 不需要（我们不用 MSVC） |
| 2 | 12 字节聚合类型必须 **struct 整体赋值**才会触发 SSE2 `movq` | ⚠️ 间接：说明原版的 Vec3 是**值语义**，不是三个独立 float 参数 |
| 3 | 返回 `int` 与 `bool` 走不同代码路径 | ❌ 不需要 |
| 4 | 只有 `#define` 能让偏移折叠进 disp8；`inline` 会生成 `add` | ⚠️ 间接：说明原版大量字段访问是**宏**而非成员函数 |
| 5 | 优化级可能是 `/Os` 而非 `/O2` | ❌ 不需要 |

---

## 3. ★ 原版用 `__m128` 而非 `float`（值得本仓库注意）

原版读一个 float 字段：

```asm
f30f1001   movss xmm0,[ecx]     ; 原版：SSE 返回
```

而我们若写 `float get()`，MSVC x86 会生成 `d901 fld dword [ecx]`（x87 栈返回）。
**只有返回类型是 `__m128` 时才走 XMM0。**

### 对功能还原的意义：**这条不影响我们**

Emscripten/clang 的浮点 ABI 与 MSVC x86 无关，本仓库不需要为了字节一致去写 `__m128`。
**但它说明一件事**：原版的「返回一个 float」在语义上可能是「返回一个 128 位向量」，
即**原版接口是按向量设计的**。如果某个 ECL 操作码或卡牌效果在本仓库里表现为
「返回值对不上」，值得怀疑是**打包/解包的语义**问题，而不是数值问题。

**复核方式**：本仓库未复核。转录自 matching 侧证据文件，未独立验证。

---

## 4. ★ 原版对象是 C++ 类，`ecx` 就是 `this`（本仓库采纳）

原版访问器是**成员函数**，不是「自由函数 + 偏移常量」：

```cpp
// 错：token 12
int __fastcall get(void* self, void* unused_edx) { return *(int*)self; }

// 对：token 6，字节完全相同
int Obj00::get() { return f_00; }
```

### 本仓库采纳了什么

1. **写 `struct` + 成员函数**，不用「一堆偏移常量 + 自由函数」。
2. **不需要 `__fastcall` 伪装、不需要哑参数**——那些是字节匹配的 ABI 成本，
   AGENTS.md 铁律 3 明确禁止带回本仓库。
3. 字段偏移**只写有二进制见证的**，中间用 `gap_XX` 占位并注明「未解析」。

> 本仓库现有的 `cpp/game/` 已经是这个形态（例如 `EnemyManager` 的
> `EM_COUNTER_OFFSET` 常量配成员函数），**无需改动**。
> 这条的价值是：以后再开新 struct 时按这个形态写。

---

## 5. 结构布局见证（16 个 struct，转录）

| struct | 字段偏移 | witness 地址 |
|---|---|---|
| `Obj00` | +0x00 | 0x00433B40 |
| `Obj04` | +0x00 / +0x04 / +0x08 | 0x00433BA0 / 0x00433BB0 |
| `Obj1C` | +0x1C | 0x0041FA30 / 0x00429A50 |
| `Obj20` | +0x20 | 0x004176D0 |
| `Obj24` | +0x24 | 0x00406730 |
| `Obj28` | +0x28 | 0x00424480 |
| `Obj34` | +0x34 | 0x00417770 |
| `Obj3C` | +0x3C | 0x00441210 / 0x00440840 |
| `Obj44` | +0x44 | 0x00433DC0 |
| `Obj50` | +0x50 | 0x00431300 |
| `Obj620` | +0x620 | 0x00407E30 |
| `Obj1230` | +0x1230 | 0x00433DF0 |
| `Obj1274` | +0x1274 | 0x0040A090 |
| `Obj4FF4` | +0x4FF4 | 0x00433DD0 |
| `Obj19260` | +0x19260 | 0x00431260 |
| `Obj47FAC` | +0x47FAC | 0x0040A0E0 |

**偏移是硬的（有机器码见证），但字段的业务含义一个都不知道。**

### ★ 本仓库能反向提供什么

本仓库从 ECL / 存档 / 卡牌数据推出了若干**有语义的偏移**，这些对 matching 侧有用：

| 对象 | 偏移 | 语义 | 本仓库证据 |
|---|---|---|---|
| EnemyManager | +0x168 | `u32 counter[111]` 每个模板被抽中的次数 | `docs/WAVES.md` §10 + RVA 0x3D243 |
| EnemyManager | +0x324 | `u32 weights_a[111]` | RVA 0x3CEF2-0x3CF31 |
| EnemyManager | +0x4E0 | `u32 weights_b[111]` | 同上，三数组间距恒为 0x1BC |
| EnemyManager | +0x4F4C | `u32 skip_index` 上次抽中的索引 | 反汇编 |
| EnemyManager | +0x4F34 | 268 字节的关卡数据块 | RVA 0x3521D |
| Enemy | **偏移 unknown** | 状态标志位数组（op502 写 `[0]`，op504/505 写 `[1]`） | TH20 的源码把字段叫作 `fields_2c8`，但 **0x2c8 是 TH20 的偏移，TH185 未经实测**，不要当成 TH185 的见证 |
| Enemy | **偏移 unknown** | 血量四字 `[v,v,v,v*7]`，v 实测 7000 | op511 的**行为**实测（见 `docs/ECL_OPCODE_GAPS.md` §4.1），但字段偏移未测 |

> **这是本次最有价值的反向贡献**：matching 侧一直在找「+0x324 是什么」，
> 本仓库能给出「它是波次权重数组的起点，111 个 u32，与 +0x168 和 +0x4E0 同构」。
> 详见 `docs/WAVES.md`。

---

## 6. 已实测的「物理不可得」（转录，与本仓库无关）

| 项 | 数量 | 原因 |
|---|---:|---|
| `fsincos` 函数 | 19 | MSVC 前端永不合成该指令 |
| `encode_value` | 1 | 需要 `8bff` hotpatch 序言 |
| `and [mem],0` | 1 | 现代 MSVC 生成 `mov` |
| `xor eax,eax; inc eax` | 1 | `return 1` 被优化成 `mov eax,1` |
| G6 三项 | 3 | 无 RTTI / 无 `.pdata` / 无第二个反汇编器 |

**对功能还原完全无影响**——这些是「字节相等」的障碍，不是「行为等价」的障碍。

---

## 7. 维护约定

- **本文件只写我复核过或明确标注「转录」的内容。**
- 转录条目不追上游更新；如果 matching 侧改了口径，以本仓库的复核结果为准。
- 每次改动请在提交信息里注明「给 matching 侧的结构语义证据」或「转录自 matching」。
