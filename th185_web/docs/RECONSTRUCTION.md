# TH185 功能还原重建方案

状态：**方案确定，实现未开始。**

本文件说明 TH185 走「功能还原重建（source reconstruction）」而非「字节级 matching decompilation」的理由、路线与验收标准。

---

## 1. 两种路线对比

| | 字节级 matching decomp | **功能还原重建（本项目）** |
|---|---|---|
| 目标 | 编译产物与原版**逐字节相同** | 行为与原版**等价**（同输入 → 同帧状态、同分数、同录像） |
| 编译器 | 必须锁定原版工具链与精确版本 | 任意现代编译器（Emscripten / clang） |
| 验证 | 分节 SHA-256 全等 | 原版录像逐帧比对 + 模块级原版函数对照 |
| 投入产出 | TH06 级别（MSVC 2002）尚可；现代 MSVC 极低 | 与编译器无关，直接服务产品目标 |
| 与 Eagler Touhou 的关系 | 产出的 exe 无法直接跑在浏览器 | 天然适配 C++ → WASM → WebGL2 |

### 为什么 TH185 选后者

**① 工具链版本不利。** TH185 的 Rich Header 显示同时存在 build **27412 / 31328 / 31332** 三档编译单元（对应 VS2019 16.x 的不同更新），且 .text 段达 711,168 字节（TH18 是 702,464）。现代 MSVC 的 LTCG、/O2 指令重排、SSE 自动向量化都会让字节级匹配变得极其脆弱。

**② 产品目标就是行为等价。** Eagler Touhou 的验收标准是「能在浏览器里玩到原版体验」——包括保留原版的一些 Bug。TH11 已经用这套方法跑通了 121 项模块回归 + 100,672 帧六面录像比对，是成熟路径。

**③ 字节级 matching 无法复用现有工作区。** TH08/TH10/TH11 全部是 C++/SDL3 → WASM 路线，共享 portable/sdl 渲染器。走 matching 就等于另起一套工具链，与工作区割裂。

---

## 2. 技术路线

### 2.1 整体架构

```
th185_web/cpp/game/     具名管理器（会话/战斗/资源/ANM/ECL/卡牌/商店…）
        ↓
th185_web/cpp/sdl/      平台层（输入、音频、文件、字体、图形设备）
        ↓
portable/sdl/           工作区共享 GLES 语义渲染器
        ↓
SDL3 → Emscripten → WebAssembly → WebGL2
```

### 2.2 行为依据的四个来源

| 来源 | 提供什么 | 获取方式 |
|---|---|---|
| **原版机器码** | 函数边界、控制流、常量表 | IDA / Ghidra 反汇编 th185.exe |
| **thcrap 断点表** | 已定位的关键函数入口 | base_tsa/th185.v1.00a.js（已逐点验证命中） |
| **thprac 全局变量表** | 数据结构布局 | thprac_th185.cpp 的 enum addrs |
| **原版录像** | 逐帧状态基准 | 用原版 th185.exe 录制 |

### 2.3 验证手段（三级）

**级别 1：模块级原版函数对照（开发期）**

用 Unicorn 执行原版函数，与 C++ 实现对比返回值与内存副作用。工作区已有实现：th11_web/scripts/native/machine.mjs。

```js
// 概念示意
const machine = await NativeMachine.create(exeBytes);
const original = machine.call(0x414F20, { ecx: thisPtr, args: [cardId, 2] });
const reconstructed = cardManager.addCard(cardId);
assert.equal(reconstructed, original);
```

**级别 2：原版录像逐帧比对（验收）**

用原版 exe 录制参考录像，重实现逐帧比较：
- 自机位置、判定点、残机、Bomb、Power、资金
- 弹幕位置、速度、类型
- 敌人位置、血量、状态机
- RNG 状态（TH185 随机权重极大，RNG 必须逐位一致）
- 终局分数、卡牌收集状态

**级别 3：浏览器端实测**

SDL3 / WebGL2 实际运行，验证音频、字体、触控、存档、录像导入导出。

---

## 3. TH185 相比 TH11 的特殊性

| 维度 | TH11 | TH185 | 影响 |
|---|---|---|---|
| 关卡结构 | 6 面 + Extra | **9 个关卡 × 多波次** | 关卡管理器要按「关卡→波次」两层建模 |
| 随机性 | 固定 | **极高**（攻略中随机的权重很大） | RNG 必须逐位复现，否则录像无法回放 |
| 卡牌系统 | 无 | **85 张卡**（cardId < 85） | 独立的卡牌管理器 + 商店系统 |
| 难度 | Easy/Normal/Hard/Lunatic/Extra | **Very Easy / Easy / Normal / Hard / Very Hard / Over Drive** | 难度枚举与数值表 |
| 资源格式 | .dat 未加密（LZSS 压缩） | **.dat 是 PBG4 加密** | 必须先解决解密 |
| 存档 | scoreth11.dat | scoreth185.dat | 需逆向文件格式 |
| 录像 | th11_XX.rpy | th185_XX.rpy | 需逆向格式 |

### 关键难点：随机性

TH185 的设计核心是「重复游玩逐渐变简单」，随机权重被刻意放大（omake.txt 原文：「わざと振り幅大きくしています」）。这意味着：

- **RNG 必须与原版逐位一致**，包括种子生成、每帧消耗次数、取值范围。
- 任何一处 RNG 调用次序的差异都会导致录像在第 N 帧后完全失同步。
- 这是本项目最大的技术风险，建议**优先逆向 RNG**（TH11 的 Rng.hpp 可参考，但 TH185 可能有变化）。

---

## 4. 工作分解

### P0：基础设施（必须先做）

- [ ] **PBG4 解密**。th185.dat 无法用 thtk 12 解包（no module found for version 18）。两条路：
  - 自己实现 PBG4（需先搞清算法，社区无公开实现）
  - 用调试器在 file_load 断点（Rx2174）之后 dump 内存镜像
- [x] **目标校验脚本**（scripts/verify-target.mjs，已 PASS）
- [ ] **RNG 逆向**。定位 RNG 状态地址与算法，验证与原版逐位一致

### P1：资源与脚本层

- [ ] ANM 解释器（TH185 的 ANM 与 TH11 同族，可复用 th11_web/cpp/game/AnmInterpreter.cpp 的思路）
- [ ] ECL 解释器（同上）
- [ ] STD / MSG / SHT 资源加载
- [ ] 字体与文字渲染（AsciiText / GlyphAtlas 可参考）

### P2：游戏逻辑

- [ ] 会话与关卡流程（9 关 × 波次）
- [ ] 自机
- [ ] 弹幕管理器
- [ ] 敌人与 Boss
- [ ] 卡牌系统（85 张）
- [ ] 黑市商店与资金系统

### P3：周边系统

- [ ] 菜单、标题、暂停
- [ ] 对话与结局
- [ ] 存档 scoreth185.dat
- [ ] 录像 th185_XX.rpy

### P4：运行时与集成

- [ ] SDL3 / WebGL2 运行时
- [ ] 启动器接入（eagler-touhou/1 协议）
- [ ] 触控、高刷新率、thprac / thcrap 适配

---

## 5. 验收标准

参考 TH11 的 docs/2026-09-27-最终验收.md：

| 项 | 要求 |
|---|---|
| 模块回归 | 全部通过、0 失败 |
| 原版录像覆盖 | 至少覆盖全关卡常规难度 + Over Drive |
| 逐帧比对 | 运动、资源、RNG、终局奖励全部一致 |
| 浏览器实测 | 音频、字体、触控、存档、录像导入导出 |
| 启动器 | 离线启动、同源更新、包管理 |

**固定样本的通过不能视为所有输入的保证。** 必须明确写出测试边界。

---

## 6. 与 TH18 decomp 项目的关系

本机同时存在 ../th18_decomp（TH18 字节级 matching decomp）。两者**目标不同但可互补**：

- TH18 是 TH185 的**前作**，卡牌系统、ECL 结构、ANM 格式高度相似。
- TH18 decomp 项目积累的地址表、函数识别、资源分析成果可以直接迁移。
- 但 **PBG4 加密问题两边都没解决**——这是共同的前置障碍。
