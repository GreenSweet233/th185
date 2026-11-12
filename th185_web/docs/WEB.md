# TH185 网页外壳

> 状态：**浏览器实测通过**（2026-11-12）
>
> ```
> 60 FPS · 32 精灵 · 32 动画 · canvas 640x480 · 状态「就绪」
> ```
>
> 画面为 8×4 的 TH185 敌人精灵，各自播放 ANM 动画。
> 截图与帧率由用户在真实浏览器中确认。

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
| 交互是否流畅 | ❌ 不能 | ⚠️ 帧率正常，但输入映射尚未接线 |

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
| 音频 | 无 SDL_mixer 对应实现 |
| 输入映射 | 触控按钮是占位，键盘只记录按键未传入 tick() |
| 真实关卡流程 | 只加载 enemy.anm |
| 菜单 / 标题画面 | 未做 |
| 浏览器实测 | 本环境无浏览器 |
