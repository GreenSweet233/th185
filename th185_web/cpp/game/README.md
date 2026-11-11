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

1. **关卡是「关卡 × 波次」两层**，不是「面 × 阶段」。thprac 有 9 个 warps 文件（stage 0–8），每关内是 WAVE_1..N + BOSS。
2. **卡牌系统独立成管理器**，85 张卡，涉及主动卡（C 键使用）、被动卡、商店购买。
3. **随机性是核心机制**，不是装饰。RNG 必须与原版逐位一致。
4. **难度有 6 档**（Very Easy / Easy / Normal / Hard / Very Hard / Over Drive），比 TH11 多一档 Over Drive。

## 可复用来源

- 渲染与 ANM 数学：`../th11/th11_web/cpp/game/AnmGeometry.cpp`、`AnmProjection.cpp`
- 弹幕基础：`BulletManager.cpp`、`BulletMotion.cpp`（TH185 的弹幕系统与 TH11 同族，但参数表不同）
- 存档框架：`ScoreFile.cpp`
- 录像框架：`Replay.cpp`、`ReplayRecorder.cpp`

> **不要直接复制粘贴**。TH185 的每个管理器都必须以原版机器码与录像为准重新验证；TH11 只提供架构参考。
