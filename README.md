# th185

[![QQ Group 1124121427](https://img.shields.io/badge/QQ%20Group-1124121427-12B7F5?logo=tencentqq)](https://qm.qq.com/q/eeUrxIltug?from=tim)

A high-fidelity, portable reimplementation of 東方虹龍洞 外伝 ～ バレットフィリア達の闇市場 ～ 100th Black Market. ver 1.00a.

> **状态：脚手架阶段。** 本仓库目前只完成规格对齐、目标指纹与工具链骨架，尚未开始游戏逻辑实现。

## 概要

本仓库是 [Eagler Touhou](https://github.com/YomotsuHisami/eagler-touhou)（[touhou.vip](https://touhou.vip/)）计划的 TH185 接入分支，沿用 TH08 / TH10 / TH11 已确立的 **C++ / SDL3 → WebAssembly → WebGL2** 路线。

与 TH06/TH07/TH08 的字节级 matching decomp 不同，本项目采用**功能还原重建（source reconstruction）**：

- 目标不是产出与原版逐字节相同的 `th185.exe`，而是**行为等价**——同样的输入产生同样的帧状态、同样的分数、同样的录像。
- 以原版机器码、资源结构、函数调用次序、x87/SSE 舍入行为和原版录像为**行为依据**，恢复具名 C++ 管理器与脚本解释器。
- 生产运行时不执行原版 EXE，也不包含 x86 转译或 D3D9 设备适配层。

### 为什么选功能还原而非字节级 matching

TH185 使用的 MSVC 工具集是 **VS2019 16.11 区间**（Rich Header 显示 build 27412 / 31328 / 31332 三档并存），现代编译器优化远比 TH06（MSVC 2002）激进。字节级 matching 在这种工具链上投入产出比极低，而 Eagler Touhou 的产品目标是"能在浏览器里玩到原版体验"，行为等价即可满足。

## Layout

- `th185_web/`: TH185 game, SDL runtime, launcher integration, documentation, and source tests.
- `portable/`: shared GLES renderer, input code, SDL support code, and bundled third-party source dependencies.

## 目标版本

| 项 | 值 |
|---|---|
| 作品 | 東方虹龍洞 外伝 ～ バレットフィリア達の闇市場 ～ 100th Black Market. |
| 版本 | **1.00a**（2022-08-14，唯一正式版） |
| 语言 | 日文原版 |
| `th185.exe` SHA-256 | `76bbf844e962c16004b64f1b2ef3551593056baa43ea53b9a5df45ad731345c7` |
| `th185.exe` 大小 | 883,200 字节 (0xD7A00) |
| PE TimeDateStamp | 1659144319 (0x62E4887F, 2022-07-30 01:25:19 UTC) |
| ImageBase | 0x00400000 |
| `th185.dat` SHA-256 | `5AF133F02ADD57F27FBEBA1B8D1E7D579F484B40C2595D57D4536537806D7428` |
| `thbgm.dat` SHA-256 | `5224F1311D4FD91325BCC53399DBBEFA3A6D33F14D8E820459753123D036F1EE` |

完整指纹见 [`th185_web/target.json`](th185_web/target.json)。

> 该 SHA-256 与 thcrap-tsa `versions.js` 中标注 **`(original, DLsite release)`** 的条目一致。Steam 渠道（AppID **2097720**）套了 SteamStub 壳，磁盘上的 exe 哈希不同，需脱壳后才与本仓库基准一致。

## Build

The source tree follows the same shared workspace/toolchain layout used by TH08, TH10 and TH11. See [`th185_web/docs/BUILD.md`](th185_web/docs/BUILD.md) for the current build, validation, and packaging commands.

Generated build outputs are written below `th185_web/artifacts/` and are intentionally not tracked.

## 进度

| 系统 | 状态 |
|---|---|
| 目标指纹与版本考证 | ✅ 完成 |
| 目录/文档/脚本骨架 | ✅ 完成 |
| **逆向 `.rpy` 格式（恢复验证能力）** | ⬜ **P0 最高风险** |
| 资源解包（PBG4） | ⬜ 待办 |
| RNG 逆向 | ⬜ 待办 |
| ANM / STD / MSG / SHT 资源层 | ⬜ 待办 |
| ECL 脚本解释器（可复用 TH18） | ⬜ 待办 |
| **波次随机系统（110 模板 × 50 槽位）** | ⬜ 待办 |
| 自机（仅魔理沙）、弹幕、敌人、Boss（28 编号） | ⬜ 待办 |
| **卡牌（85 张 + Cost）/ 黑市 / 双货币** | ⬜ 待办 |
| **8 档难度数值表** | ⬜ 待办 |
| 菜单、对话、结局 | ⬜ 待办 |
| 存档 `scoreth185.dat` | ⬜ 待办 |
| SDL3 / WebGL2 运行时 | ⬜ 待办 |
| 启动器接入（eagler-touhou/1） | ⬜ 待办 |

## 三个必须先知道的事实

**① 这是全球首例。** GitHub 全站搜索（`th185` / `"100th Black Market"` / `touhou 18.5` / `th185 in:readme`）只找到 Mod、随机化工具和资源编译器，**没有任何 TH185 的重建或反编译项目**。Eagler Touhou 进度表里 th185 标为 `Unassigned`。

**② Replay 被取消了。** THBWiki 原文「本作取消了Replay系统」——但引擎层代码还在（`th185_%.2d.rpy` 字符串 + `0x468764` 的 `ReplayManager::OnTick`）。这意味着 **TH11 那套「原版录像逐帧比对」的核心验证手段不可直接用**，必须先逆向出 `.rpy` 格式自建录像。详见 [docs/RECONSTRUCTION.md](th185_web/docs/RECONSTRUCTION.md) §3.3。

**③ 规模约为 TH11 的 3~3.5 倍。** 9 场景 × 50 波次槽位、110 条波次模板池、28 个 BOSS 编号、87 个卡牌类、8 档难度。自机砍到 1 人（仅魔理沙）是唯一减项。

## Assets and licensing

This repository does not include the original Touhou executable, game data, music, replay, save files, or private development reference material. A runnable package must be assembled locally from files you are legally allowed to use.

Licensing is component-specific. Keep the notices and licenses beside each bundled component; no blanket license is asserted for the original game or its assets.
