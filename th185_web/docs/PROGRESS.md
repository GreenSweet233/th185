# TH185 还原进度

> 由 `node th185_web/scripts/progress.mjs` 自动生成，**不要手改**。

目标版本：`th185` v1.00a（Japanese）

## 总览

| 指标 | 数量 |
| --- | ---: |
| 子系统总数 | 30 |
| 已完成 | 1 |
| 阻塞 | 0 |
| 行为断言总数 | 22 |
| 已验证断言 | 20 |
| 已知符号 | 23 |
| 子系统完成率 | 3.3% |

## 子系统状态

| 状态 | 数量 | 子系统 |
| --- | ---: | --- |
| 已完成 | 1 | SYS-CORE-RNG |
| 进行中 | 5 | SYS-CORE-ARCHIVE / SYS-GAME-SESSION / SYS-GAME-WAVE / SYS-GAME-CARD / SYS-GAME-DIFFICULTY |
| 未开始 | 24 | SYS-CORE-ANM / SYS-CORE-ECL / SYS-CORE-FILE / SYS-RENDER-ANM / SYS-RENDER-TEXT / SYS-AUDIO-WAVE / SYS-INPUT-CONTROL / SYS-GAME-PLAYER / SYS-GAME-BULLET / SYS-GAME-ENEMY / SYS-GAME-MARKET / SYS-GAME-ITEM / SYS-UI-TITLE / SYS-UI-PAUSE / SYS-UI-DIALOGUE / SYS-UI-ENDING / SYS-UI-RESULT / SYS-UI-MUSICROOM / SYS-UI-ACHIEVEMENT / SYS-SAVE-SCORE / SYS-PLATFORM-SDL / SYS-PLATFORM-WEB / SYS-INTEGRATION-LAUNCHER / SYS-INTEGRATION-THPRAC |

## 阻塞项

（无）

## 这些数字不代表什么

- 子系统标 `complete` **不等于**它的行为全部与原版一致——行为断言是独立的账本。
- 有源码 **不等于**行为正确。
- `verified` 断言必须有 `test_ref` 指向可复现的测试，否则校验器会拒绝。
- TH185 **没有 Replay**，所以不存在「录像逐帧比对」这种强证据；
  验证依赖内存状态快照与 Force Wave 确定性注入，强度低于 th11 的录像比对。
