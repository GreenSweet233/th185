# TH185 碰撞判定

> 状态：**子弹能击中敌人，敌人会死**
> 限制：**玩家不会被击中**——没有敌方弹幕，也没有玩家判定点

---

## 1. 范围

| 做了 | 没做 |
|---|---|
| 敌人固定池（256） | 敌人由 ECL 驱动 |
| 圆形判定 | 真实弹幕图案 |
| 子弹 × 敌人碰撞 | 玩家被弹判定 |
| HP 与死亡 | 擦弹 / 得分 |
| 命中消耗子弹 | 掉落物 |

---

## 2. ★ 为什么用圆形判定

矩形判定在旋转后需要重建包围盒。
**圆形与旋转无关**，所以旋转的敌人不需要每帧重算判定形状。

```cpp
const float dx = bullet.x - enemy.x;
const float dy = bullet.y - enemy.y;
// 比较平方距离：每帧上千次开方是纯开销，比较是等价的
if (dx * dx + dy * dy > radius * radius) continue;
```

---

## 3. ★ 一个测试假失败的完整排查

第一次跑碰撞测试，`hits = 0`，看起来碰撞没实现。
**实际上有三个独立问题叠在一起：**

### 3.1 坐标错开

```
enemy  : x=-140.0  y=-155.8
bullet : x=-6.0    y=12.0
```

子弹沿 y 轴直飞，敌人在 x=-140 处。
**双管只偏移 ±6，永远打不到。**

### 3.2 统计被覆盖而非累计

```cpp
// 错误：每帧覆盖，最后一帧没命中就读到 0
stats_.collision_hits = enemies_.resolve_collisions(&bullets_);

// 正确：累计
stats_.collision_hits += enemies_.resolve_collisions(&bullets_);
```

### 3.3 测试自身污染了状态

探针前面的阶段已经把敌人打光、把玩家推到左边界。
碰撞测试开始时，敌人已经没了、玩家在边上。

**解法：加 `reset_scenario()`。**

```cpp
void Application::reset_scenario() {
    bullets_.clear();
    enemies_.clear();
    player_ = Player{};
    // 重新绑定动画库：重置丢掉了动画句柄
    ...
    stats_.collision_hits = 0;
}
```

### 3.4 还有第四个问题

`spawn_test_wave` 生成后立刻读 `enemies_alive` 得到 0——
因为 `stats_` 只在 `tick()` 里同步。

**生成函数必须立即发布统计**，否则「生成后立刻读」永远是旧值。

---

## 4. 实测

```
enemies     : alive 16, killed 0
after fire  : alive 0, killed 16, hits 48
```

16 个敌人（HP=3）全部击毁，48 次命中（16 × 3）。

### 4.1 17 项断言

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
[PASS] bullets retire when off-screen
[PASS] there were enemies to shoot
[PASS] bullets hit enemies
[PASS] enemies died
[PASS] enemy count dropped
[PASS] test wave can be spawned
[PASS] enemy count matches what was spawned
```

---

## 5. ★★★★★ 完整游戏画面

![完整场景](../artifacts/app-frame.png)

| 元素 | 位置 |
|---|---|
| 玩家 | 底部中央（30×46） |
| 敌人 | 顶部竖列（12 个） |
| 子弹 | 中间（白色方块，Additive） |

**这是项目第一次渲染出完整的游戏场景。**

---

## 6. 未完成

| 项 | 说明 |
|---|---|
| 敌人由 ECL 驱动 | ECL 能跑但未与敌人池连接 |
| 真实弹幕图案 | 敌人不开火 |
| 玩家被弹判定 | 玩家不会死 |
| 擦弹 / 得分 | 未做 |
| 掉落物 | 未做 |
