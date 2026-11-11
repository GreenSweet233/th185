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

| 关卡文件 | 波次结构 |
|---|---|
| `warps0` | 教程 + WAVE_1..3 + BOSS + ATTACK |
| `warps1` | WAVE_1..3 + BOSS |
| `warps2` | WAVE_1..4 + BOSS |
| `warps3` | WAVE_1..5 + BOSS |
| `warps4` | WAVE_1..5 + BOSS |
| `warps5` | WAVE_1..6 |
| `warps6` | WAVE_1..6 |
| `warps7` | WAVE_1..4 + TENKYU_CHIMATA + ATTACK |
| `warps8` | WAVE_1..6 |

> 共 **9 个关卡**（stage 0–8）。这与 TH18 的「6 面 + Extra」结构完全不同——TH185 是「多关卡 × 多波次」的循环结构。

## 待办

- [ ] **P0**：解决 PBG4 解密，解出 `th185.dat`
- [ ] **P0**：建立 `scripts/verify-target.mjs`（校验目标 exe/dat 哈希）
- [ ] **P1**：从解包资源中提取 ANM / STD / MSG / SHT，建立资源层
- [ ] **P1**：ECL 脚本解释器（TH185 的 ECL 与 TH18 同族，可参考 TH18 的分析）
- [ ] **P1**：卡牌系统（85 张卡）
- [ ] **P2**：自机、弹幕、敌人、Boss
- [ ] **P2**：黑市商店与随机性
- [ ] **P3**：菜单、对话、结局
- [ ] **P3**：存档（`scoreth185.dat`）与录像（`th185_XX.rpy`）
- [ ] **P4**：SDL3 / WebGL2 运行时
- [ ] **P4**：启动器接入（`eagler-touhou/1`）
