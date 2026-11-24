# AGENTS.md — th185 工程规约

面向所有协作者（人类或 AI）的工作约定。**动手前必读。**

本仓库是 [Eagler Touhou](https://github.com/YomotsuHisami/eagler-touhou) 计划的 TH185 接入分支，规格对齐同工作区的 `th10` / `th11` 仓库。

---

## 0. 铁律

1. **不得把原版游戏文件提交进仓库。**
   `th185.exe` / `th185.dat` / `thbgm.dat` / `custom.exe` / `th185.sdb` 一律在 `.gitignore` 中。仓库只保存 `th185_web/target.json` 里的哈希与 `scripts/verify-target.mjs` 校验脚本。

2. **任何改动前后都要跑目标校验。**
   ```powershell
   .\th10_web\tools\node.exe th185_web/scripts/verify-target.mjs
   ```
   必须 exit 0。若 FAIL，先查是不是拿错目录（汉化版 / 改版 / 非 v1.00a）。

3. **本仓库不做字节级 matching。** 目标是**行为等价**，不是二进制相同。
   不要为了「让 .text 段哈希对上」去折腾编译器版本或改源码形态。

   > **2026-11-24 更新**：字节级匹配已独立成独立仓库
   > [`th185-matching`](https://github.com/GreenSweet233/th185-matching)。
   > 那边追求 `diff = 0`，用 `__fastcall` + 哑参数、宏折叠 disp8 等技巧；
   > **那些技巧不要带回本仓库** —— 它们会破坏可读性，正是拆家的原因。

4. **生产运行时不执行原版 EXE，不含 x86 转译，不使用 D3D8/9 适配层。** 原版 exe 只用于开发期行为对照（Unicorn 执行原版函数 + 内存状态 dump）。

5. **RNG 必须逐位一致。** TH185 随机权重极大（波次从 110 条模板池抽取），任何 RNG 调用次序差异都会导致状态分叉。改动任何涉及随机数的代码后，必须重跑状态比对。

6. **不要假设有录像可用。** TH185 **取消了 Replay 系统**（THBWiki 确证；且小数点作历来就不提供 Replay 菜单，见 §3.2.1）。验证走「内存状态快照比对」+「Force Wave 确定性注入」而非「录像逐帧比对」。**不要试图靠自建 Replay 来解决验证问题**——那是替 ZUN 解决他主动绕开的问题，风险高（见 §3.3）。

7. **不把"能跑起来"当成验收。** 验收标准见 `th185_web/docs/RECONSTRUCTION.md` §5。

---

## 0.5 工作流（从 th095 / th11 的 commit 历史反推）

完整规范见 **[th185_web/docs/WORKFLOW.md](th185_web/docs/WORKFLOW.md)**。要点：

### 四事实分离

**「有源码」「能编译」「行为一致」「已验证」是四个独立事实，互不蕴含。**

| 事实 | 记录位置 |
|---|---|
| 子系统有实现 | `config/subsystems.csv` 的 `status` |
| 行为已实现 | `config/behaviors.csv` 的 `status=implemented` |
| 行为已验证 | `status=verified` + **必须有 `test_ref`** |
| 子系统完成 | `status=complete`（该子系统全部行为 verified 之后） |

### 会话前置检查

```powershell
node th185_web/scripts/ci.mjs
```

三项必须全 PASS：target（哈希+PE指纹）/ ledgers（账本一致性）/ progress（进度可生成）。

### 账本

| 文件 | 内容 | 校验器强制 |
|---|---|---|
| `config/subsystems.csv` | 30 个子系统 | deps/parent 必须存在且无环 |
| `config/behaviors.csv` | 可观察行为断言 | verified 必须有 test_ref 且 evidence_level ∈ {behavioral, observed} |
| `config/symbols.csv` | 23 个已知地址 | 规范地址、无重复、必须写来源 |

### 一次一个行为面

从 th095 的历史看，它 712 个后续 commit 几乎都是「单函数 + 单一动词」。
**不要并行铺开**，一个 commit 一个可验证边界。

### 三个硬阻塞

1. **PBG4 加密**（P0）—— `th185.dat` 解不开，资源层全部阻塞
2. **RNG 必须逐位一致** —— 波次从 110 条模板池抽取，调用次序差一步就分叉
3. **无 Replay** —— 验证强度上限低于 th11，验收报告必须写明这个边界

---

## 1. 环境

| 项 | 值 | 说明 |
|---|---|---|
| 工作目录 | `D:\Windows\GreenSweet\Game\Tasks\thvip\th185` | 本仓库 |
| 工作区根 | `D:\Windows\GreenSweet\Game\Tasks\thvip` | 共享工具链在这里 |
| 原版游戏 | 见 `th185_web/local.target.json`（本机） | 不入库 |
| Node.js | `th10_web/tools/node.exe` | **勿用系统 Node** |
| Emscripten | `tools/emsdk` | 固定版本 |
| WASI SDK | `th10_web/tools/wasi-sdk-34.0-x86_64-windows` | 原版对照用 C++ 核心 |
| Python | 系统 Python 3 | 打包脚本 |

---

## 2. 目录约定

| 目录 | 用途 | 是否入库 |
|---|---|---|
| `th185_web/docs/` | 文档（Markdown） | ✅ |
| `th185_web/scripts/` | 构建与验证脚本（.mjs / .py） | ✅ |
| `th185_web/cpp/` | 游戏与平台层源码 | ✅ |
| `th185_web/tests/` | 测试 | ✅ |
| `th185_web/config/` | 基线配置 | ✅ |
| `th185_web/launcher/` | 启动器（沿用工作区共用） | ✅ |
| `th185_web/sdl-runtime/` | Web 运行时壳 | ✅ |
| `th185_web/target.json` | 目标指纹 | ✅ |
| `th185_web/local.target.json` | 本机路径覆盖 | ❌ |
| `th185_web/artifacts/` | 构建产物 | ❌ |
| `th185_web/assets/` | 原版资源 | ❌ |
| `th185_web/reference/` | 参考录像 | ❌ |
| `portable/` | 共享渲染器与输入 | ✅ |
| `deploy/` | 部署脚本 | ❌（与 th11 一致） |

---

## 3. 验证流程

### 3.1 目标指纹校验

```powershell
.\th10_web\tools\node.exe th185_web/scripts/verify-target.mjs
```

校验内容：
1. 四个核心文件的 SHA-256（`th185.exe` / `th185.dat` / `thbgm.dat` / `custom.exe`）
2. PE 指纹：`TimeDateStamp=1659144319` + `.text SizeOfRawData=711168` + `PECheckSum=0x000DE79F`
3. PE CheckSum 自洽性（重算值 == 存储值，否则文件被改过）
4. 分节 SHA-256 与 overlay 检查

### 3.2 PE CheckSum 重算的正确算法

**CheckSum 是 4 字节 DWORD，必须整体跳过**（等价于先清零再算）：

```js
const ckOff = peOffset + 24 + 64;
let total = 0;
for (let i = 0; i < buf.length; i += 2) {
  if (i >= ckOff && i < ckOff + 4) continue;   // ← 跳过全部 4 字节
  const lo = buf[i];
  const hi = i + 1 < buf.length ? buf[i + 1] : 0;
  total += lo + (hi << 8);
  total = (total & 0xffff) + (total >> 16);
}
total = (total & 0xffff) + (total >> 16);
total += buf.length;
```

> **踩坑记录**：只跳过低 2 字节会多累加高 16 位。TH18 上实测偏差 0x0C（得 0xCFE7D 而非 0xCFE71）；TH185 同样必须跳过全部 4 字节。

---

## 4. 代码风格

- C++：C++17，`-ffp-contract=off`、`-fno-strict-aliasing`、`-fno-exceptions`、`-fno-rtti`（与 th11 的 `build-sdl.mjs` 一致）。
- 脚本：ESM（.mjs），Node 22+；Python 3.8+。
- 所有脚本必须支持 `--help` 或至少有顶部注释说明用法与退出码。
- 输出中文，文件编码 UTF-8。
- 不得静默失败：出错必须打印原因并返回非零退出码。

---

## 5. 已知陷阱

| 陷阱 | 表现 | 应对 |
|---|---|---|
| PowerShell 把 `[...]` 当通配符 | 对含 `[th185]` 的路径返回空 | 用 `-LiteralPath` |
| 哈希大小写不一致 | 比较误报不匹配 | 统一大写 |
| `thdat.exe` 不支持 version 18+ | `no module found for version 18` | 待自己实现 PBG4 |
| `th185.dat` 是 PBG4 加密 | 头部 `13 5A B3 B6 F0 C7 54 D4` | 先解包，否则一切无从谈起 |
| Windows 控制台 GBK | 中文乱码 | 设 `PYTHONIOENCODING=utf-8` |

---

## 6. 与外部项目的对照原则

- **thcrap**（`thpatch/thcrap-tsa`）：地址表可信度高，但其 `binhacks` 的 `expected` 字段**不保证准确**。用它定位，别用它断言。
- **thprac**（`touhouworldcup/thprac`）：全局变量表经过实战验证，可信度最高。
- **TH11 仓库**（`../th11`）：架构与流程的直接参考，但关卡结构与随机性差异巨大，不能照搬。
- **TH18 前作**：卡牌/ECL/ANM 结构与 TH185 高度相似（`ThModern_ECLGetSub` 确证 ECL 格式完全相同）。TH18 的分析成果可直接迁移，但本机没有独立的 TH18 项目目录，参考资料统一放在 `_references/`。
