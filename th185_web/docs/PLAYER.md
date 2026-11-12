# TH185 玩家实体

> 状态：**可操控**（八方向、低速、边界钳制、按钮边沿计数）
> 限制：**不是原版玩家**——无射击弹幕、无 option、无判定、无 card

---

## 1. 为什么先做这个

上一轮结束时 `tick(held)` 接收按键位掩码，记录到 `stats_.held_buttons`，
**但没有任何东西消费它**——项目里没有玩家实体。

输入接通了却没有接收方，等于没接通。

---

## 2. 素材

`pl01.anm` 实测：

| 项 | 值 |
|---|---|
| sprite 数 | 61 |
| 单个 sprite | 30×46 像素 |
| script 数 | 43 |
| 图集 | 256×512 |
| 格式 | 1 = BGRA8888 |

脚本结构统一：`302, 304, 311` 开头，然后一串 `300`（选 sprite），
循环的用 `opcode 200` 跳回。

---

## 3. 实现范围

| 做了 | 没做 |
|---|---|
| 八方向移动 | 射击弹幕生成 |
| 低速（半速） | option / 子机 |
| 斜向归一化 | 判定点与碰撞 |
| 场地边界钳制 | 死亡 / 复活 |
| 射击/炸弹边沿计数 | 卡牌系统 |
| 动画脚本切换 | 与 ECL 联动 |

---

## 4. ★ 三个容易错的细节

### 4.1 斜向归一化

```cpp
if (dx != 0.0f && dy != 0.0f) {
    dx *= 0.70710678f;   // 1/sqrt(2)
    dy *= 0.70710678f;
}
```

不归一化的话，斜向速度是 `4 * sqrt(2) ≈ 5.66`，**比直行快 41%**。
这在弹幕游戏里是致命的——玩家会一直斜着走。

### 4.2 边沿计数而非电平计数

```cpp
if ((held & ButtonShoot) && !(previous_held_ & ButtonShoot)) ++stats_.shots;
```

按住不放只算一次。电平计数会把「按住 60 帧」记成 60 发。

### 4.3 低速正好一半

实测：

```
focus speed : 20.00 vs normal 40.00
```

10 帧 × 4.0 = 40，10 帧 × 2.0 = 20。**精确一半。**

---

## 5. 实测

```
start       : x=0.0 y=0.0
idle        : x=0.0 y=192.0
move left   : x=-40.0
move right  : x=40.0
focus speed : 20.00 vs normal 40.00
clamped     : x=-192.0
diagonal    : distance=40.00
shots       : 1
bombs       : 1
```

### 5.1 11 项断言

```
[PASS] idle does not move the player
[PASS] left decreases x
[PASS] right increases x
[PASS] focus is slower than normal
[PASS] player is clamped to the field
[PASS] diagonal is not faster than straight
[PASS] one shoot press counts once
[PASS] one bomb press counts once
[PASS] the input probe passed
[PASS] focus speed was measured (20.00 vs normal 40.00)
[PASS] the player clamped to the left bound (x=-192.0)
```

---

## 6. 外壳键盘映射

| 按键 | 功能 | 位 |
|---|---|---|
| Z | 射击 | 0 |
| X | 炸弹 | 1 |
| Shift | 低速 | 2 |
| C | 特殊 | 3 |
| ↑↓←→ | 移动 | 4-7 |

```javascript
function heldMask() {
  let mask = 0;
  for (const code of keys) {
    const button = KEY_TO_BUTTON[code];
    if (button) mask |= button;
  }
  return mask;
}
core._th185_tick(heldMask());
```

状态栏现在显示：`FPS · 精灵数 · [低速] · (x, y)`。

---

## 7. ★ 速度常量是推断的

```cpp
constexpr float PLAYER_SPEED = 4.0f;
constexpr float PLAYER_FOCUS_SPEED = 2.0f;
```

这是 TH 系列的经典值，**TH185 尚未实测**。

用命名常量而非魔法数的理由：将来实测出真值时，改一处即可，
而且搜索 `PLAYER_SPEED` 就能找到所有使用点。

证据等级：`inferred`，不是 `verified`。
