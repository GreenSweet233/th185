# cpp/game

TH185 的具名游戏管理器。目录结构与 `../th11/th11_web/cpp/game/` 对齐，但按 TH185 的实际机制调整。

## 计划中的管理器

| 文件 | 职责 | 状态 |
|---|---|---|
| `GameSession.cpp/.hpp` | 会话、菜单与游戏流程 | ⬜ |
| `GameBattle.cpp/.hpp` | 自机、敌人、弹幕、Boss、道具与场景联动 | ⬜ |
| `GameResources.cpp/.hpp` | 资源加载与生命周期 | ⬜ |
| `Archive.cpp/.hpp` | `.dat` 解包（**PBG4，待实现**） | ⬜ |
| `AnmInterpreter.cpp` / `AnmManager` / `AnmVm` | ANM 动画解释与绘制 | ⬜ |
| `EclInterpreter.cpp` / `EclContext` / `EclResource` | ECL 敌人脚本 | ⬜ |
| `Stage.cpp` / `StageScript` / `StageDraw` | 关卡与波次 | ⬜ |
| `CardManager.cpp/.hpp` | **卡牌系统（85 张）** | ⬜ |
| `BlackMarket.cpp/.hpp` | **黑市商店与资金** | ⬜ |
| `Rng.hpp` | **随机数（必须逐位一致）** | ⬜ |
| `PlayerFrame` / `PlayerMotion` / `PlayerCollision` | 自机 | ⬜ |
| `BulletManager` / `BulletLaunch` / `BulletMotion` | 弹幕 | ⬜ |
| `EnemyManager` / `EnemyState` / `EnemyCommands` | 敌人 | ⬜ |
| `ItemManager` / `ItemRewards` | 道具 | ⬜ |
| `Hud.cpp` / `AsciiText` / `GlyphAtlas` | HUD 与文字 | ⬜ |
| `TitleMenu` / `PauseMenu` / `Ending` | 菜单与结局 | ⬜ |
| `ScoreFile` / `Replay` / `ReplayRecorder` | 存档与录像 | ⬜ |

## TH185 相对 TH11 的结构差异

1. **关卡是「场景 × 波次」两层**，不是「面 × 阶段」。thprac 有 9 个 warps 文件（stage 0–8），每场景内是 WAVE_1..N + BOSS，**没有传统 Extra**。
2. **波次敌编成是随机抽取的**，模板池上限 **110 条**（权重数组在 EnemyManager(0x4d7af4)+0x324，名字表 0x4B5D50）。这是本作核心机制，TH11/TH18 都没有。
3. **BOSS 是候选池**：28 个 BOSS 编号，每场景 4~5 选 1（Challenge Market 是 6 组 Boss Rush）。
4. **卡牌系统独立成管理器**：85 张可用卡（86 含背面，87 个 RTTI 类），含 **Cost 系统**（TH18 没有）、**双货币**（funds + bulletMoney）、**黑市 3 次购买**、**可叠卡**。
5. **随机性是核心机制**，不是装饰。RNG 必须与原版逐位一致。
6. **难度有 8 档**：Very Easy / Easy / Normal / **Little Hard** / Hard / Very Hard / Lunatic / **Over Drive**。TH11 只有 4 档，TH18 没有 Little Hard。
7. **自机只有 1 组**（雾雨魔理沙），靠卡牌组合产生玩法分化。这是相对 TH11 的**减项**。
8. **Replay 在游戏层被取消**，验证体系需另建（见 docs/RECONSTRUCTION.md §3.3）。

## 可复用来源

- 渲染与 ANM 数学：`../th11/th11_web/cpp/game/AnmGeometry.cpp`、`AnmProjection.cpp`
- 弹幕基础：`BulletManager.cpp`、`BulletMotion.cpp`（TH185 的弹幕系统与 TH11 同族，但参数表不同）
- 存档框架：`ScoreFile.cpp`
- 录像框架：`Replay.cpp`、`ReplayRecorder.cpp`

> **不要直接复制粘贴**。TH185 的每个管理器都必须以原版机器码与录像为准重新验证；TH11 只提供架构参考。
