# TH185 动画库分层

> 结论：**一个场景需要多个 `.anm` 库，不是一个**

---

## 1. ★ 一个关键误解的纠正

`.std` 里有 `anm_name` 字段，例如 `world01t.std` 指向 `world01t.anm`。

看起来「加载这个文件就能显示这一关」。**但实测：**

| 文件 | sprites | scripts |
|---|---|---|
| `world01t.anm` | **1** | 1 |
| `world01.anm` | **1** | 1 |
| `world02.anm` | **1** | 3 |
| `world03.anm` ~ `world08.anm` | **1** | 1-2 |

**`worldNN.anm` 只有一张背景图。**

敌人、子弹、特效在**独立的库**里：

| 库 | sprites | scripts | 内容 |
|---|---|---|---|
| `enemy.anm` | 120 | 89 | 敌人 |
| `bullet.anm` | 368 | 72 | 子弹 |
| `effect.anm` | 64 | 195 | 特效 |
| `pl01.anm` | 61 | 43 | 自机 |

### 1.1 为什么之前看起来「通了」

我用硬编码的 `enemy.anm` 渲染，画面里有 32 个可辨认的敌人精灵。

**那是对的，但那不是「关卡」——那是敌人库单独渲染。**

改成按 `anm_name` 加载后画面变空，因为背景只有 1 个 sprite 且初始不可见。

**这不是 bug，是数据事实。**

---

## 2. 正确做法：多库叠加

```cpp
struct AnimationBank {
    AnmResource resource;
    std::vector<DecodedImage> textures;
    std::string name;
};
std::vector<AnimationBank> banks_;
```

### 2.1 加载顺序即绘制顺序

```
1. 场景背景  worldNN.anm   （script_limit = 1）
2. 敌人      enemy.anm     （script_limit = 32）
```

背景先加载，因此先绘制——在敌人后面。

### 2.2 ★ sprite 编号按库独立

`enemy.anm` 的 sprite 0 和 `world01.anm` 的 sprite 0 是**不同的图**。

所以每个库必须保留**自己的**纹理数组：

```cpp
u32 AnmManager::render_bank(SoftwareRaster& target, const AnmResource* resource,
                            const DecodedImage* textures, u32 texture_count,
                            float view_x, float view_y) const {
    for (const auto& slot : pool_) {
        if (resource && slot->resource != resource) continue;  // 只画本库
        ...
    }
}
```

**跨库混用会采到错误纹理。**

### 2.3 实测

```
frame      : 640x480
bytes      : 1228800
quads      : 33          (32 敌人 + 1 背景)
animations : 33
```

---

## 3. 输入

`tick(u32 held)` 已经接收按键位掩码，记录到 `stats_.held_buttons`。

**但没有消费它**——项目里还没有玩家实体，没有对象可操控。

保持签名不变的意义：将来接玩家实体时，**外壳与导出的接口不需要改**。

---

## 4. 未完成

| 项 | 说明 |
|---|---|
| `bullet.anm` / `effect.anm` / `pl01.anm` | 可加载但未接入 |
| 玩家实体 | 无，所以输入无对象 |
| ECL 驱动的波次生成 | ECL 能跑但未与 ANM 连接 |
| 背景可见性 | 背景脚本需要更长时间才设 visible |
| 音频 | 未做 |
