# TH185 开发与验证

状态：**脚手架阶段**。本文件描述已确定的构建路线与验证入口；标记 ⬜ 的部分尚未实现。

## 架构（沿用 TH08 / TH10 / TH11 最终版工作区）

```
th185_web/cpp/game/     具名游戏管理器（会话、战斗、资源、ANM、ECL…）
        ↓
th185_web/cpp/sdl/      平台层（输入、音频、文件、字体、图形设备）
        ↓
portable/sdl/           工作区共享 GLES 语义渲染器（Renderer.cpp / GraphicsState.hpp / Shaders.hpp）
        ↓
SDL3 → WebAssembly → WebGL2
```

渲染调用链是：`AnmRenderer` 计算顶点 → `ZunGraphics` 语义接口 → `cpp/sdl/GraphicsDevice` → 工作区共用的 `portable/sdl/Renderer.cpp` → SDL3 / WebGL2。

**生产运行不执行原版 EXE，也不含 x86 转译或 D3D8/9 设备适配层。**

> Windows 版 Chromium 的 WebGL 驱动信息可能显示 ANGLE / Direct3D11，这是浏览器自身的 GPU 后端，不是本项目使用了 D3D。

## 环境

与 TH11 共用同一套工作区工具链（不要另建）：

| 组件 | 位置 | 说明 |
|---|---|---|
| Node.js | `th10_web/tools/node.exe` | 工作区固定版本，勿用系统 Node |
| Emscripten | `tools/emsdk` | 固定版本 |
| WASI SDK | `th10_web/tools/wasi-sdk-34.0-x86_64-windows` | 原版对照用 C++ 核心 |
| Python | 系统 Python 3 | 打包脚本 |
| TypeScript | `tools/architecture/typescript` | 启动器构建 |

## 本地构建

以下命令从工作区根目录（`thvip/`）运行。

```powershell
# 原版比较用 C++ / WASI 核心（⬜ 待实现）
.\th10_web\tools\node.exe th185_web/scripts/cpp/build.mjs

# 实际 SDL3 / WebGL2 运行时（⬜ 待实现）
.\th10_web\tools\node.exe th185_web/scripts/build-sdl.mjs --release

# 隔离的本地开发站点；不更改公网服务（⬜ 待实现）
.\th10_web\tools\node.exe th185_web/scripts/package-release.mjs --development
.\th10_web\tools\node.exe th185_web/artifacts/architecture-preview/scripts/serve.mjs --port 8115
```

> 端口 **8115** 预留（TH09 用 3007 区段，TH11 用 8113，TH185 用 8115 以避免冲突）。

## 验证入口

```powershell
# 原版模块对照（⬜ 待实现）
.\th10_web\tools\node.exe --test --test-concurrency=1 'th185_web/tests/cpp/*.test.mjs'

# 目标二进制与资源指纹校验（✅ 已可用，见 scripts/verify-target.mjs）
.\th10_web\tools\node.exe th185_web/scripts/verify-target.mjs
```

## 验证原则

TH185 是**功能还原重建**，验收标准与 TH11 一致：

1. **原版录像逐帧比对**。用原版 `th185.exe` 录制参考录像（`reference/replays/`），重实现逐帧比较运动、资源、RNG 和终局奖励。
2. **模块级原版对照**。用 Unicorn 执行原版函数，与 C++ 实现对比返回值与内存副作用（见 `scripts/native/machine.mjs` 的既有实现）。
3. **固定样本不等于穷举**。通过的录像不能承诺所有输入或所有手机都无 bug。

## 已知障碍

### 1. `th185.dat` 是 PBG4 加密

```
th185.dat 头部: 13 5A B3 B6 F0 C7 54 D4 CB 7B 9F 8E F4 FF 5C 00 ...
th18.dat  头部: 13 5A B3 B4 F0 CD 54 D4 CB 3C 9F 06 F4 F3 5C 00 ...
```

两者同族（社区称 PBG4）。thtk 12 的 `thdat.exe` **不支持 version 18 及以后**（`no module found for version 18`），需要自己实现解密，或从运行时内存 dump。

**这是当前第一优先待解决项**——资源层不解包，游戏逻辑无从谈起。

### 2. 无 PDB、无符号表

函数边界需要靠三重手段确定：
- thcrap 的断点地址（`base_tsa/th185.v1.00a.js`，见下表）
- thprac 的全局变量地址（`thprac_th185.cpp` 的 `enum addrs`）
- 递归下降反汇编 + 字符串交叉引用

### 3. TH185 的关卡结构不是传统 6 面

thprac 提供了 `thprac_th185_warps0.cpp` 到 `warps8.cpp` **共 9 个关卡文件**，每关内含 `WAVE_1`…`WAVE_N` + `BOSS` 的波次结构。这与 TH11 的「6 面 + Extra」结构差异很大，需要按「关卡 × 波次」建模而非「面 × 阶段」。

## 已获取的逆向参照

### thcrap 断点表（`base_tsa/th185.v1.00a.js`）

已在本机 `th185.exe` 上逐点验证命中：

| 断点 | RVA | 本机字节 |
|---|---|---|
| `file_size` | 0x00213C | `89 45 FC 85 C9` |
| `file_load` | 0x002174 | `E8 97 F1 06 00`（call） |
| `file_loaded` | 0x07144E | `5E 5B 8B E5 5D` |
| `music_title` | 0x06F2BD | `FF 74 24 18 A1 88 9E 52 00` |
| `music_cmt` | 0x06F515 / 0x06F5DE | `50 6A 00 6A 00` |
| `trophy_result_nickname` | 0x070166 | `E8 45 0F 00 00` |
| `abilities_result_name` | 0x01BBF3 | `68 FF FF FF 00` |
| `abilities_result_desc` | 0x01BE10 | `6A 00 6A 00 6A 00` |
| `trophy_ingame` | 0x070E0A | `E8 71 02 00 00` |
| `abilities_ingame` | 0x021AD5 | `50 68 E4 8B 4B 00` |
| `spell_name` | 0x030FAB | `53 6A 00 6A 00` |
| `spell_id` | 0x03B9F6 | `E8 95 53 FF FF` |
| `titlemsg_comment` | 0x06E5B5 | `50 A1 88 9E 52 00` |
| `ruby_offset` | 0x044DF2 / 0x045614 | `89 44 24 24 6A 2C` |
| `textbox_size` | 0x0450ED / 0x045738 | `8B C8 8D 51 01` |

> 注意 `music_title` 处的 `A1 88 9E 52 00` 是 `mov eax, [0x529e88]`，可据此定位音乐管理器指针。

### thprac 全局变量表（`thprac_th185.cpp`）

```cpp
enum addrs {
    BLACK_MARKET_PTR = 0x4d7ac4,
    CARD_DESC_LIST   = 0x4ca370,
    ENEMY_MANAGER    = 0x4d7af4,
    GAME_THREAD_PTR  = 0x4d7b0c,
    SCALE_ADDR       = 0x573DC0,
};

// 加卡函数（Thiscall）：0x414F20，参数 (cardId, 2)，cardId < 85
asm_call<0x414F20, Thiscall>(*(uint32_t*)0x4d7ab8, cardId, 2);
```

**关键推断**：`cardId < 85` 说明 TH185 的卡牌总数是 **85 张**（TH18 是 60 张左右）。`CARD_DESC_LIST = 0x4ca370` 是卡牌描述表。

### thprac 关卡结构（`thprac_th185_warps*.cpp`）

THBWiki 原文：「游戏总共 **9 个场景**，含一个新手引导关卡及一个随机 BOSS RUSH 挑战关卡，共 **85 张能力卡牌**。」

| # | 场景 | 道中波次 | BOSS 候选 | BOSS |
|---|---|---|---|---|
| 0 | Tutorial（妖怪山麓） | 3 | 1 | 豪德寺三花 |
| 1 | 1st Market（秘天崖） | 3 | 4 | 三花 / 穰子 / 爱塔 / 合欢 |
| 2 | 2nd Market（雾之湖） | 4 | 4 | 琪露诺 / 若鹭姬 / 赤蛮奇 / 润美 |
| 3 | 3rd Market（赛之河原） | 5 | 4 | 璎花 / 久侘歌 / 成美 / 小町 |
| 4 | 4th Market（守矢神社地下庙会） | 5 | 5 | 早苗 / 咲夜 / 妖梦 / 灵梦 / 荷取 |
| 5 | 5th Market（妖怪之山） | 7 | 4 | 典 / 龙 / 克劳恩 / 天子 |
| 6 | 6th Market（虹龙洞） | 7 | 5 | 萃香 / 二岩 / 早鬼 / 百百世 / 高岭 |
| 7 | End of Market（月虹市场） | 4 | 1（固定） | 天弓千亦 |
| 8 | Challenge Market（月虹黑市） | **12** | 6（每关各抽 1） | Boss Rush |

**合计 50 个道中波次槽位 + 28 个 BOSS 编号。**

> 与 TH18 的「6 面 + Extra」完全不同。TH185 是「选关 + 波次随机」结构：每关由若干 Wave 组成，**Wave 的敌编成从模板池随机抽取**，最后一波必出 BOSS。

### 波次随机系统（核心机制）

thprac 的 Force Wave 功能反推出数据结构：

```cpp
auto* weights   = (uint32_t*)GetMemAddr(ENEMY_MANAGER, 0x324);  // ENEMY_MANAGER = 0x4d7af4
auto** sub_names = (const char**)0x4B5D50;                       // 110 条 ECL 子程序名
for (size_t i = 0; i < 110; i++) if (weights[i]) { ... }
```

- **波次模板池上限 110 条**，每条是一个 ECL sub
- 权重数组在 `EnemyManager + 0x324`
- 名字表在静态地址 `0x4B5D50`
- 波次选择入口 `0x43CF47`，结果写 Eax 后跳 `0x43D57A`
- 强制指定入口 `0x43D156`

屏幕上会显示 `startWave : %s`（exe 字符串实测）。

### 卡牌系统（85 张 + Cost）

| 事实 | 证据 |
|---|---|
| 85 张卡 | THBWiki 原文；thprac `card_names_t::count = 86`（含 0 号"背面"） |
| RTTI 里 `AbilityCard*` 类名 **87** 个 | `th185.exe` RTTI 扫描（含基类 `AbilityCardInf` / `AbilityCardNullInf`） |
| TH18 同法只有 **58** 个 | 卡牌实现量约为 TH18 的 1.5 倍 |
| **Cost 系统（TH18 没有）** | 字符串 `Equip Cost %d%%`、`Total Cost %d%%/%d%%`、`Shop Cost %d(%d)` |
| **双货币** | `funds`（上限 999,990）+ `bulletMoney`（弹币，int32，疮痍即作废） |
| 黑市 | 每波结束后 **3 次购买**，卡池 = 该场景 BOSS 踏破后可获得的卡，带 Discount 标记，**买到的卡是临时的** |
| 可叠卡 | 部分卡牌可持有多张以叠加效果 |

初始装备受「总 Cost 上限」约束；通过踏破场景和击败新 BOSS 提升卡槽数量与 Cost 上限。

### 难度：**8 档**（不是 6 档）

```
Very Easy / Easy / Normal / Little Hard / Hard / Very Hard / Lunatic / Over Drive
```

exe 字符串实测，与 thprac 的 `difficulties[8]` 数组完全对应（`thprac_th185.cpp:334-343`）。难度索引写 `0x4d1038`，滑块范围 0..7。

> **注意**：第 4 档叫 **Little Hard**，TH18 没有这一档。Over Drive 是 TH185 新增的最高难度。

### ECL 格式

`ThModern_ECLGetSub`（`thprac_th185.cpp:515, 608`）→ TH185 使用 **TH10+ 现代 ECL 格式**，指令结构 `{time, id, size, param_mask, rank_mask, param_count, zero}`，**与 TH18 完全一致**。

> 这意味着 **ECL 解释器可以整体复用 TH18 的实现**，是最大的可复用项。

### thprac 全局地址表（可直接作为 Ghidra 锚点）

| 符号 | 地址 | 含义 |
|---|---|---|
| `BLACK_MARKET_PTR` | 0x4d7ac4 | 黑市对象指针 |
| `CARD_DESC_LIST` | 0x4ca370 | 卡牌描述表 |
| `ENEMY_MANAGER` | 0x4d7af4 | 敌机管理器（+0x324 波次权重，+0x4f34 ECL 子程序列表） |
| `GAME_THREAD_PTR` | 0x4d7b0c | 游戏线程对象 |
| `SCALE_ADDR` | 0x573DC0 | 渲染缩放（float） |
| — | 0x4d7ab8 | 卡牌管理器 this 指针（AddCard 用） |
| — | 0x4d1024 | phase / 波次相位 |
| — | 0x4d1038 | difficulty |
| — | 0x4d106c | funds（金钱） |
| — | 0x4d1070 / 0x4d1074 | bulletMoney（两个副本） |
| — | 0x4d10bc | life（残机） |
| — | 0x4d7c68 | 当前 stage 索引 |
| — | 0x4B5D50 | 110 条波次 ECL 子程序名表 |

关键函数：

| 地址 | 签名 | 含义 |
|---|---|---|
| 0x414F20 | `thiscall (cardId, 2)` | `AddCard`，this = `*(uint32_t*)0x4d7ab8` |
| 0x479220 / 0x48AAA0 / 0x48A540 / 0x41C7E0 / 0x45E050 | — | 重开流程 |
| 0x468764 | — | `ReplayManager::OnTick` |

### 对象链（从 thcrap 的 spell_id 表达式反推）

```
edi+0x5590              → GameManager
  +0xC                  → Stage 对象
    +0x1018             → EnemyManager
      +0x1218           → 当前 BOSS
        +0x10C          → ECL 子程序表
Stage+0x4               → 当前 sub 索引
```

与 thprac 的 `GetMemContent(0x004d7af4, 0x4f34, 0x10c)` **互相印证**，是一条可直接用于重建的指针链。

---

## ⚠️ 关键风险：**Replay 被取消**

这是本项目相对 TH11 **最大的验证手段损失**，必须提前规划。

### 事实

| 层面 | 状态 | 证据 |
|---|---|---|
| **游戏层** | ❌ **取消了 Replay 系统** | THBWiki 原文：「本作取消了Replay系统。」 |
| **引擎层** | ✅ 代码仍在 | exe 字符串含 `th185_%.2d.rpy`、`replay/%s`；thprac 有钩子 `0x468764 = ReplayManager::OnTick` |

**推测**：ZUN 沿用了 TH18 引擎，Replay 代码未删只是 UI 入口被关掉。`.rpy` 格式大概率与 TH18 同构（TH18 的 `th18_%.2d.rpy` 结构为 magic + 版本 + 各 stage 的 chunk 列表）。

### 影响

TH11 的验收核心是「原版录像逐帧比对」（100,672 帧六面 + 43,256 帧 Extra）。**TH185 没有录像可录**，这条路直接断了。

### 替代方案（四条，建议组合使用）

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| **A. 状态快照比对** | 用 thprac 地址表在关键节点（每波开始/结束、BOSS 出场、符卡宣言）dump 内存状态，与原版逐项 diff | 不依赖录像 | 只能比对离散点，不是逐帧 |
| **B. 确定性种子注入** | 用 thprac 的 Force Wave（0x43D156）固定波次，绕过随机，做单波次级逐帧比对 | 把 110 个模板逐个跑一遍，覆盖完整 | 需要原版可控执行 |
| **C. thprac 练习数据** | `th185.sdb`（6.29 MB）可能含大量已完成关卡状态，作回归语料 | 现成数据 | 格式未公开，需先逆向 |
| **D. 自建 Replay** | 引擎层 ReplayManager 还在（0x468764），逆向 `.rpy` 结构后自己录自己放，做 A/B 比对 | 恢复完整验证能力 | 需先逆向格式，前置成本高 |

> **建议优先做 D**——一旦逆向出 `.rpy` 结构，验证体系就能完全恢复，后续所有模块都能用录像回归。这是投入产出比最高的一步。

---

## 待办

- [ ] **P0**：解决 PBG4 解密，解出 `th185.dat`
- [x] **P0**：建立 `scripts/verify-target.mjs`（✅ 已 PASS）
- [ ] **P0**：**逆向 `.rpy` 格式**，恢复录像验证能力（风险最高）
- [ ] **P0**：逆向 RNG（种子、消耗次序、取值范围），必须逐位一致
- [ ] **P1**：从解包资源中提取 ANM / STD / MSG / SHT，建立资源层
- [ ] **P1**：ECL 解释器（**可整体复用 TH18 实现**）
- [ ] **P1**：卡牌系统（85 张 + Cost + 黑市 + 双货币）
- [ ] **P1**：波次随机系统（110 模板 × 50 槽位）
- [ ] **P2**：自机、弹幕、敌人、Boss（28 个 BOSS 编号）
- [ ] **P2**：8 档难度数值表（含 Little Hard / Over Drive）
- [ ] **P3**：菜单、对话、结局
- [ ] **P3**：存档（`scoreth185.dat`）
- [ ] **P4**：SDL3 / WebGL2 运行时
- [ ] **P4**：启动器接入（`eagler-touhou/1`）
