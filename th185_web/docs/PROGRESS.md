# TH185 还原进度

> 由 `node th185_web/scripts/progress.mjs` 自动生成，**不要手改**。

目标版本：`th185` v1.00a（Japanese）

## 总览

| 指标 | 数量 |
| --- | ---: |
| 子系统总数 | 33 |
| 已完成 | 6 |
| 阻塞 | 0 |
| 行为断言总数 | 370 |
| 已验证断言 | 345 |
| 已知符号 | 23 |
| 子系统完成率 | 18.2% |

## 子系统状态

| 状态 | 数量 | 子系统 |
| --- | ---: | --- |
| 已完成 | 6 | SYS-CORE-ARCHIVE / SYS-CORE-ANM / SYS-CORE-RNG / SYS-GAME-CARD / SYS-GAME-MARKET / SYS-GAME-STAGE |
| 进行中 | 10 | SYS-CORE-ECL / SYS-RENDER-ANM / SYS-AUDIO-WAVE / SYS-GAME-SESSION / SYS-GAME-WAVE / SYS-GAME-ENEMY / SYS-GAME-DIFFICULTY / SYS-INTEGRATION-LAUNCHER / SYS-GAME-COLLISION / SYS-GAME-SCORE |
| 未开始 | 17 | SYS-CORE-FILE / SYS-RENDER-TEXT / SYS-INPUT-CONTROL / SYS-GAME-PLAYER / SYS-GAME-BULLET / SYS-GAME-ITEM / SYS-UI-TITLE / SYS-UI-PAUSE / SYS-UI-DIALOGUE / SYS-UI-ENDING / SYS-UI-RESULT / SYS-UI-MUSICROOM / SYS-UI-ACHIEVEMENT / SYS-SAVE-SCORE / SYS-PLATFORM-SDL / SYS-PLATFORM-WEB / SYS-INTEGRATION-THPRAC |

## 阻塞项

（无）

## 这些数字不代表什么

- 子系统标 `complete` **不等于**它的行为全部与原版一致——行为断言是独立的账本。
- 有源码 **不等于**行为正确。
- `verified` 断言必须有 `test_ref` 指向可复现的测试，否则校验器会拒绝。
- TH185 **没有 Replay**，所以不存在「录像逐帧比对」这种强证据；
  验证依赖内存状态快照与 Force Wave 确定性注入，强度低于 th11 的录像比对。
