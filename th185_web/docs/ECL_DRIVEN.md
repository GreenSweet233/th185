# TH185 ECL 驱动游戏

> 状态：**ECL 的波次时间表真正驱动敌人生成**
> 限制：**只用了 op1013 一个操作码**——ECL 的其余语义仍未解

---

## 1. 连接点在哪里

之前 ECL 只是「能跑」，跑出来的结果全部记录在 `EclContext` 里，
**没有任何东西消费它**。

本轮找到的连接点是 **`op1013`**：

```
u32 reserved(0), u32 slot, u32 time[8], u32 reserved(0)
```

实测（`world01t.ecl` 的 `main`）：

| slot | times | 步长 |
|---|---|---|
| 0 | 100, 115, 130, 145, 160, 175, 190, 205 | 15 |
| 4 | 300, 400, 500, 600, 700, 800, 900, 1000 | 100 |
| 8 | 2000, 2300, 2600, 2900, 3200, 3500, 3800, 4100 | 300 |

**这是「在第 N 帧生成第 X 号波次」的时间表。**

---

## 2. 场景主控是 `main`

`world01t.ecl` 的子程序：

| 子程序 | 指令数 | 含 op1013 |
|---|---|---|
| LogoEnemy | 3 | 否 |
| MapleEnemy | 2 | 否 |
| WorldWave01 | 17 | 否 |
| WorldWave02 | 5 | 否 |
| WorldWave03 | 5 | 否 |
| WorldWaveB00 | 7 | 否 |
| **main** | **67** | **是（5 个）** |

**只有 `main` 含调度。**

### 2.1 ★ 但 main 不是「全实现」的

Application 原本只运行「所有 opcode 都实现」的子程序。
`main` 含未实现操作码，因此被过滤掉——**调度永远读不到**。

修正：`main` 强制运行。
解释器遇到不支持的操作码会干净停止，所以部分执行仍然有效——
而 `op1013` 就在停止点之前。

---

## 3. ★ main 的前导指令

```
id=40    payload=全 0
id=502   payload=[0][0x20][0][0]
id=23    payload=[0][1][0][0]
id=524   payload=全 0
id=1009  payload=全 0（16 字节）
id=1013  ← 从这里开始 5 个调度
```

**`op524` 和 `op1009` 未实现会阻断调度。**

`op1009` 的证据很强——它**恒邻接 `op1013`**：

```
op=1009  prev=524   next=1013   x8
op=1009  prev=14    next=1013   x1
```

**9/9 都在 `op1013` 前面**，读作「开启调度块」的标记。

`op524` 出现 138 次，上下文分散，但 payload 在 `main` 里全 0。
两者都按 net 0 的 data-only 处理。

---

## 4. ★★ 解释器不是协程

这是本轮最重要的认识。

`main` 每帧**从头重跑**，所以 `op1013` 每帧都被重新记录。

### 4.1 症状一：调度项无限增长

```
f=0    sched=5
f=100  sched=505
f=400  sched=2005
```

500 帧后 2500 项——**内存泄漏**。

修正：`op1013` 记录时按 `(slot, times)` 去重。

```
f=0    sched=5
f=400  sched=5      ← 稳定
```

### 4.2 症状二：同一调度项重复触发

即使调度表去重了，触发也必须去重：

```
f=0    fired=0
f=400  fired=2000   ← 每个调度项每帧都触发
```

修正：按 `(slot << 32 | time)` 键去重。

```
f=0    fired=0
f=200  fired=3
f=400  fired=7
final  fired=9      ← 正确
```

**一个调度项是一次性定时器，不是每帧事件。**

---

## 5. 实测

```
schedules   : total=5
after 400f  : fired=9 enemies=9
```

### 5.1 画面

![ECL 驱动场景](../artifacts/app-frame.png)

左上方**大型敌人**（金绿色）由 `op1013` 第 60 帧生成，
与背景网格里的合成精灵明显不同。

### 5.2 24 项断言

```
[PASS] idle does not move the player
[PASS] left decreases x
[PASS] right increases x
[PASS] focus is slower than normal
[PASS] player is clamped to the field
[PASS] diagonal is not faster than straight
[PASS] one shoot press counts once
[PASS] one bomb press counts once
[PASS] holding shoot keeps firing
[PASS] bullets are alive on screen
[PASS] player bullets retire when off-screen
[PASS] there were enemies to shoot
[PASS] bullets hit enemies
[PASS] enemies died
[PASS] enemy count dropped
[PASS] test wave can be spawned
[PASS] enemy count matches what was spawned
[PASS] enemies fired at the player
[PASS] the player took hits
[PASS] the player lost lives
[PASS] invulnerability prevents instant death
[PASS] the scene controller declared schedules
[PASS] schedules fired over time
[PASS] scheduled enemies exist
```

---

## 6. 未完成

| 项 | 说明 |
|---|---|
| slot → 实际编队 | 原版从 `.std` 的 28 字节组表读，未接入 |
| 敌人 sprite 选择 | 全部用第 0 号 sprite，ECL 未驱动 |
| 敌人运动模式 | 目前统一缓慢下移 |
| 其余 ECL 语义 | op42/op44/op12/op14 等高频操作码仍未知 |
| 弹幕图案 | 敌人只会单发瞄准 |
