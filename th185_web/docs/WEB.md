# TH185 网页外壳

> 状态：**浏览器实测通过**（2026-11-12），但**当时那一版的输入与音频没有接线**，
> 已于 2026-11-24 补齐（见 §7）。现在的画面也不止 32 个精灵了：
> 含背景视差层、敌人、自机、弹幕、HUD 与魔方阵。
>
> ```
> 60 FPS · canvas 640x480 · 状态「就绪」
> ```
>
> 下方 §5.1 与 §7 保留了原始记录并标注了哪些说法后来被推翻，
> 因为「当时说的是错的」比「悄悄改掉」更有用。

---

## 1. 文件构成

| 文件 | 行数 | 职责 |
|---|---|---|
| sdl-runtime/th185.html | 4 | 页面骨架（canvas、加载/暂停/触控面板） |
| sdl-runtime/style.css | 190 | 深色 UI 主题 |
| sdl-runtime/shell.mjs | 273 | 帧循环、像素呈现、输入、生命周期 |
| scripts/serve-shell.mjs | — | 本地静态服务器（路径映射） |

---

## 2. 路径映射

HTML 期望的路径与实际文件的对应关系由 serve-shell.mjs 负责：

```
/                  -> sdl-runtime/th185.html
/app/style.css     -> sdl-runtime/style.css
/app/shell.mjs     -> sdl-runtime/shell.mjs
/th185-app.mjs     -> artifacts/cpp-check/th185-app.mjs
/th185-app.wasm    -> artifacts/cpp-check/th185-app.wasm
/th185.dat         -> 用户自备（不入库）
```

### 2.1 实测

```
/                  200  text/html            1236
/app/style.css     200  text/css             3369
/app/shell.mjs     200  text/javascript      8082
/th185-app.mjs     200  text/javascript    132536
/th185-app.wasm    200  application/wasm   130612
/th185.dat         200  octet-stream     74537324
```

**74 MB 游戏数据也能正常返回。**

---

## 3. 像素呈现

模块渲染到**软件帧缓冲**（RGBA8），外壳把它拷到 canvas：

```javascript
const source = core.HEAPU8.subarray(pointer, pointer + bytes);
imageData.data.set(source);
context.putImageData(imageData, 0, 0);
```

### 3.1 ★ 每帧都要重读 HEAPU8

WASM 堆增长时会**替换** HEAPU8 视图。
缓存一次然后复用，会在堆扩容后读到失效内存。

### 3.2 为什么不用 WebGL

640x480 的软件 blit 已经够用，且让**浏览器路径与测试路径完全一致**。

WebGL 可以以后加，但不应该在还没验证画面正确性之前就引入。

---

## 4. 帧节奏

```
固定逻辑步 : 1000 / 60 ms
追赶上限   : MAX_STEPS_PER_FRAME = 4
溢出处理   : 丢弃积压
```

### 4.1 为什么要上限

如果一帧耗时 1 秒，累积时间够跑 60 步。
不加限制就会连续跑 60 步，导致下一帧又超时——**死亡螺旋**。

上限 4 步 + 丢弃积压，把「卡顿」变成「掉帧」而不是「卡死」。

---

## 5. ★ 没有浏览器时怎么验证外壳

本环境无 Playwright/Puppeteer，也无法启动真实浏览器。

**与其假装测过页面，不如验证契约**——那些才是实际会坏的：

| 检查 | 会坏的场景 |
|---|---|
| HTML 引用的资源都存在 | 改名后忘了同步 |
| 外壳查询的元素 id 都存在 | HTML 改了 id |
| 外壳调用的函数都已导出 | 加了 C 函数忘了加 EXPORTED_FUNCTIONS |
| HEAPU8 在导出列表里 | 读像素直接 TypeError |
| 避免 context 依赖 API | SharedArrayBuffer 需要 COOP/COEP |
| 帧循环有步数上限 | 长停顿导致死亡螺旋 |

**10 项全部通过。**

### 5.1 这个校验不能证明什么（以及后来补上了什么）

| 项 | 契约校验 | 后来如何验证 |
|---|---|---|
| 页面能否渲染 | ❌ 不能 | ✅ **真实浏览器实测通过**（60 FPS） |
| 画面是否正确 | ❌ 不能 | ⚠️ 只能看出「是 TH185 的精灵」，未与原版逐像素对比 |
| 交互是否流畅 | ❌ 不能 | ✅ 键盘与触控都已接线（2026-11-24 修正，见 §7） |

**契约校验与浏览器实测是互补的：**

- 契约校验能自动化、能进 CI、能在每次提交时跑
- 浏览器实测只能人工做一次，但它能证明前者证明不了的东西

**两者都不能证明「画面与原版一致」——那需要录像对比，而 TH185 没有 Replay。**

---

## 6. 运行方式

```powershell
node th185_web/scripts/serve-shell.mjs --port 8170 --data 'C:\path\to\th185.dat'
# 浏览器打开 http://127.0.0.1:8170/
```

---

## 7. 未完成

| 项 | 说明 |
|---|---|
| ~~音频~~ | ✅ **已完成**（本节此前写「无 SDL_mixer 对应实现」是错的）。模块导出 `th185_audio_*` 五个函数，外壳用 ScriptProcessor 每 tick 拉一次立体声，22050 Hz / 每 tick 367 帧。裸测：`probe-input.mjs` 断言「音频缓冲非静音」与「峰值不超过 1.0」均通过。未验证的是**听感与原版一致**。 |
| ~~输入映射~~ | ✅ **已完成**（本节此前写「触控按钮是占位」是错的）。2026-11-24 修复：`TOUCH_TO_BUTTON` 把四个触控 id 映射到 `BUTTON` 位，`heldMask()` 把它 OR 进 tick 的入参；键盘键位表同样已接线。裸测：`player_input_check.py` 断言低速速度 20 vs 常速 40、子弹发射与回收、左边界钳制 x=-192 全部通过。未验证的是**真机触屏手感**。 |
| ~~关卡流程~~ | ✅ **已完成**（2026-11-24）。标题页新增场景选择器（9 项，带波次数）与难度选择器（8 项，默认 Normal）。此前外壳对 `_th185_load_stage` 零引用，**8 个场景不可达**。 |
| ~~暂停菜单~~ | ✅ **已完成**（2026-11-24）。暂停时 frame() 不 tick，所以位掩码送不进模块 —— 改为按键驱动 `_th185_pause_move` / `_th185_pause_confirm`，面板新增「当前：xxx」状态行。 |
| ~~事件监听器泄漏~~ | ✅ **已修**（2026-11-24）。`wakeAudio` 原本在 frame() 内部注册，每帧新增 2 个监听器且不被去重；已提到模块作用域。 |
| ~~无生命/无卡组/无魔方阵~~ | ✅ **已修**（2026-11-24，本轮）。**这一项之前的清单里完全没有**，是本轮实测发现的：`load_stage()` 只换场景数据，而「让一局可玩」的全部初始化都在 `reset_scenario()`（玩家生命、开局卡组、`magic_` 的 new、难度/分数配置），外壳从不调用它。用模块实测外壳的真实调用序 `initialize → load_stage → start`：`player_lives=0`、`loadout_size=0`、`cards_owned=0`、`magic_ready=0`、`magic_reload/break=0/0` —— **浏览器里玩家零条命、没有卡组、魔方阵不存在**。补上 `applySelection()` 里的 `_th185_reset_scenario()` 后同一序测得 `lives=3 / loadout=3 / cost=45/100 / owned=3 / magic_ready=1 / -33%/+33%`。**370 条断言全部没抓到**，因为 `probe-input.mjs` 有 28 处自己先调 `reset_scenario()`，测的是探针自己的准备动作而不是浏览器路径。已加契约检查（并做过红绿验证：去掉修复则 FAIL）。 |
| 市场 / 卡牌 UI | 模块侧有完整实现（`market_open` / `market_offers` / `market_buy` / 卡槽 / Cost），渲染也画得出来（见 `docs/media/market.png`），但**外壳没有显式入口**：靠游戏中 `Phase::Cleared` 自动进入，玩家无法主动开市场、也无法在标题页配卡组。另注：`_th185_market_open(seed)` 的签名与探针调用处不一致（探针传 2 个实参、导出只收 1 个），JS 侧多余实参被忽略，所以「同种子同报价」的断言其实只靠阶段推进的种子在起作用。 |
| 卡组编成 | 未做。`_th185_cards_equip` / `_th185_cards_loadout_*` 已导出但外壳零引用 —— 原版「买卡 → 编成 → 进关」的循环在网页里缺后半段。 |
| 菜单 / 标题画面 | 未做（原版的标题、MusicRoom、Achievement 都没有）。现在只有一个欢迎面板 + 场景/难度选择。 |
| 真机浏览器实测 | **本环境无浏览器**。所有 web 结论都是「契约检查 + Node 侧模块探测」，不是真机。 |
| 触屏真机手感 | 触控已接线（见上），但没有在任何真实触屏设备上验证过。 |
| 高 DPI / 全屏缩放 | 未做。canvas 固定 640×480，CSS 缩放，没有 devicePixelRatio 处理。（「全屏」按钮 `#fullscreen` 是有的，走 `requestFullscreen`，但那是页面级放大，不是 DPR 感知的整数倍缩放。） |
| BGM（背景音乐） | 未做。**本轮新发现**：`_th185_attach_bgm` / `_th185_bgm_play` / `_th185_bgm_tracks` 全部已导出，外壳零引用 —— 上表「音频已完成」说的是**音效（SE）**，BGM 这条链路在网页里根本没接，所以玩起来没有背景音乐。模块侧 `Application::attach_bgm` 只接受 `thbgm.dat`（约 255 MB）的 ZWAV 流，网页端要先解决按轨道分段下载，不是补一行调用就够。 |
| 卡牌图标 / 名称（配组界面前置） | 未做。模块导出 `_th185_card_name` / `_th185_card_cost` / `_th185_card_price` / `_th185_card_is_active`，但**没有**「某张卡是否已拥有」与「按索引列出已装备槽位内容」的单一导出（`_th185_cards_loadout_at` 有，`owns` / `unequip` 没导出）。做卡组编成界面需要先补 2 个导出：`th185_cards_owns(id)` 与 `th185_cards_unequip(index)`。 |
