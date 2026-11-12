# TH185 弹幕系统

> 状态：**可发射、可移动、可回收**
> 限制：**无碰撞**——没有玩家判定点，也没有敌人

---

## 1. 为什么用固定池

弹幕游戏每秒生成和回收数千发子弹。
逐发 `new` 会**碎片化堆并让帧时间不可预测**。

```cpp
constexpr u32 BULLET_CAPACITY = 4096;
std::vector<Bullet> bullets_;      // 平铺数组
std::vector<u32> free_slots_;      // 空闲槽列表
```

**一次分配，永不增长。** 这是原版的做法，也是实时系统的常规做法。

---

## 2. ★ 两个容易错的细节

### 2.1 update 循环中不能观察刚释放的槽

```cpp
for (u32 i = 0; i < BULLET_CAPACITY; ++i) {
    if (!bullet.alive) continue;
    ...
    if (off_screen) {
        bullet.alive = false;
        free_slots_.push_back(i);   // 本帧不再复用
    }
}
```

如果循环里同时 `spawn`，新子弹可能占用**循环已经走过的槽**——
那一发就不会被本帧更新，表现为「偶尔有一发子弹不动」。

### 2.2 射击是按住动作，不是边沿动作

```cpp
// 错误：只在按下瞬间开火
if (pressed && !previous_pressed) fire();

// 正确：按住就按固定间隔开火
if (held) {
    if (cooldown == 0) { cooldown = SHOT_INTERVAL_FRAMES; fire(); }
    else --cooldown;
} else cooldown = 0;
```

**边沿计数仍然单独保留**，因为输入测试要验证「按一次算一次」。
两个语义不同，不能合并。

---

## 3. 素材

`bullet.anm` 实测：

| 项 | 值 |
|---|---|
| sprite 数 | 368 |
| script 数 | 72 |
| 图集 | 256×256 |
| 格式 | 1 = BGRA8888 |

尺寸分布：

| 尺寸 | 数量 |
|---|---|
| 16×14 | 96 |
| 16×16 | 83 |
| 8×8 | 64 |
| 14×16 | 64 |
| 14×14 | 53 |
| 30×30 | 8 |

---

## 4. 实测

```
bullets     : fired 4 -> 12, alive 10
after 120f  : alive 0
```

### 4.1 14 项断言（弹幕相关 3 项）

```
[PASS] holding shoot keeps firing
[PASS] bullets are alive on screen
[PASS] bullets retire when off-screen
[PASS] bullets were spawned and tracked (fired 4 -> 12, alive 10)
[PASS] bullets retired after leaving the screen (alive 0)
```

### 4.2 画面

![含弹幕的帧](../artifacts/app-frame.png)

quads 从 33 增到 37（4 发子弹），Additive 混合下呈现为亮白方块。

---

## 5. 未完成

| 项 | 说明 |
|---|---|
| **碰撞** | 无玩家判定点、无敌人 |
| **曲线/加速弹** | 只有直线匀速 |
| **ECL 驱动弹幕** | ECL 能跑但未与弹幕池连接 |
| **真实弹型** | 双管直射是占位，非 TH185 实际弹幕 |
| **子机 / option** | 未做 |
| **擦弹 / 得分** | 未做 |
