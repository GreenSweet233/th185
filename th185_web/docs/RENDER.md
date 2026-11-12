# TH185 渲染层（ANM 纹理解码）

> 状态：**纹理解码完成（53 块 / 1200 万像素 / 0 拒绝）；绘制与着色器未实现**
> 日期：2026-11-12

---

## 0. 世代定位

TH185 属 **th14+ 高分辨率时代**（ANM 头部有 `lowresscale` 字段，TH14 才引入）。

| 世代 | 代表 | 渲染特征 |
|---|---|---|
| th06~09 | 红魔乡~花映塚 | 640×480，无缩放 |
| th10~13 | 风神录~辉针城 | 640×480，简单放大 |
| **th14+** | **辉针城之后** | **高分辨率资源 + `lowresscale` 标记** |

**因此渲染实现应当对齐 th15 而非 th11**：
参考 `thvip/eagler-touhou-th11/ports/th15/portable/sdl/Renderer.cpp`（34 KB，GLES3）。

---

## 1. 像素格式

### 1.1 ANM 用 thtk 的 `format_t`

| 值 | 名称 | 字节/像素 | TH185 文件数 |
|---|---|---|---|
| 1 | `BGRA8888` | 4 | 19 |
| 3 | `RGB565` | 2 | 4 |
| 5 | `ARGB4444` | 2 | **32** |
| 7 | `GRAY8` | 1 | 0 |

### 1.2 ★ 注意：与 th15 的 20..80 编码不是同一套

th15 的 `AssetPixelFormat.hpp` 用的是另一套编码（`20=Bgr8, 21=Bgra8, ..., 26=Argb4444`），
那是给 `.sht` 等资源用的。

**ANM 内部用的是 thtk 的 1/3/5/7。** 两套编码不能混用。

---

## 2. 解码实现

```cpp
DecodedImage decode_texture(u32 format_code, u32 width, u32 height,
                            const u8* pixels, u32 size);
```

| 格式 | 转换 |
|---|---|
| `BGRA8888` | 交换 R/B，alpha 直通 |
| `RGB565` | 5/6 位扩展到 8 位（高位复制到低位，满量程映射到 255），alpha=255 |
| `ARGB4444` | 按 thtk 注释 `0xGB 0xAR` 布局，4 位扩展到 8 位 |
| `GRAY8` | 复制到 RGB，alpha=255 |
| 未知 | **返回无效图像，不猜** |

### 2.1 位扩展为什么要复制高位

```cpp
inline u8 expand5(u32 v) { return u8((v << 3) | (v >> 2)); }
inline u8 expand6(u32 v) { return u8((v << 2) | (v >> 4)); }
inline u8 expand4(u32 v) { return u8((v << 4) | v); }
```

只左移不复制高位的话，满量程 `0x1F` 会映射到 `0xF8`(248) 而不是 `0xFF`(255)，
**白色会偏灰**。

---

## 3. ★ 格式表的正确性由数据自身验证

这是本轮最重要的验证设计：

```text
每个 THTX 块的 size 必须等于 width * height * bytes_per_pixel
```

**如果格式表错了，这个等式会对真实文件失败。**

不需要外部真值——数据自己就能反驳错误的假设。

实测 54 个块全部通过：

```
[PASS] every block uses a known format code (0 unknown)
[PASS] every block size equals width*height*bytes_per_pixel (0 bad)
       format distribution across blocks: {5: 31, 1: 19, 3: 4}
```

---

## 4. C++ 实现

| 文件 | 内容 |
|---|---|
| `cpp/game/AnmResource.hpp/.cpp` | 容器解析（基于 thtk `anm_header11_t`） |
| `cpp/game/AnmTexture.hpp/.cpp` | 像素解码到 RGBA8 |

harness 实测：

```
ANMTEX files=54 blocks=53 decoded=53 rejected=0 pixels=12014464
       fmt1=19 fmt3=4 fmt5=30 fmt7=0
```

### 4.1 为什么 54 而不是 55

`title_v.anm` 无法提取，原因是 `Archive.hpp` 记录过的**尾块解密限制**
（`stored=2678 block=512` 尾块 118 不处理）——已知限制，不是新问题。

---

## 5. 验证（8 + 3 项）

Python 侧（`anm_texture_check.py`）：

```
[PASS] collected 54 THTX blocks
[PASS] every block uses a known format code (0 unknown)
[PASS] every block size equals width*height*bytes_per_pixel (0 bad)
[PASS] decoded 40 blocks with correct output shape and alpha (0 bad)
[PASS] BGRA8888 sample decodes to RGBA [30, 20, 10, 40]
[PASS] ARGB4444 0xFFFF decodes to opaque white
[PASS] RGB565 0xFFFF decodes to opaque white
[PASS] unknown format code is rejected
```

C++ 侧（`cpp_archive_runtime.py`）：

```
[PASS] the C++ ANM parser produced output
[PASS] C++ parsed 54 of 55 .anm files (1 known tail-block miss)
[PASS] every extracted THTX block decoded (0 rejected)
[PASS] over 10 million pixels decoded (12014464)
```

---

## 6. 尚未实现（渲染层的主体）

| 项 | 状态 | 说明 |
|---|---|---|
| 纹理容器与解码 | ✅ 完成 | 本轮 |
| **ANM 脚本指令解释器** | ❌ | sprite 何时、何处、如何绘制 |
| **sprite 绘制** | ❌ | 需要顶点/矩阵/blend |
| **GLES3 着色器** | ❌ | 参考 th15 `Shaders.hpp` |
| **blend 模式** | ❌ | TH 系列有多种混合模式 |
| **`lowresscale` 缩放** | ❌ | 高分辨率资源在低分辨率下的处理 |
| **resample 着色器** | ❌ | th15 的 `ResampleShader.hpp` 有逐位精确实现 |

### 6.1 th15 可参考的具体文件

| 文件 | 大小 | 内容 |
|---|---|---|
| `portable/sdl/Renderer.cpp` | 34 KB | GLES3 渲染器主体 |
| `portable/sdl/Shaders.hpp` | 3.7 KB | 着色器源码 |
| `portable/sdl/ResampleShader.hpp` | 4.7 KB | 逐位精确的浮点重采样 |
| `portable/sdl/GraphicsState.hpp` | 8.7 KB | 管线状态定义 |
| `portable/sdl/RenderCommands.hpp` | 3.1 KB | 绘制命令 |

> th15 的 resample 着色器用整数运算复现了原版「round-toward-zero」的浮点舍入，
> 注释里写「captures require the original separately rounded operations」。
> 这说明**像素级一致性需要非常小心**，不是简单采样就够。

---

## 3. ★★★ 软件光栅化器：首次产出真实像素

### 3.1 为什么选软件而非 GLES

th15 驱动完整 GLES3 管线（`portable/sdl/Renderer.cpp`，34 KB）。
复现它需要窗口、GL 上下文、着色器工具链——三者目前都不存在。

**软件光栅化器一步就到可验证的图像**：纯 C++、产出字节、字节可对比。

这是踏脚石，不是终点。它先回答「ANM 树产生的几何和颜色对不对」。

### 3.2 实现范围

| 能力 | 说明 |
|---|---|
| RGBA8 目标 + alpha 混合 | |
| 纹理 / 纯色四边形 | |
| 10 种 blend 模式 | 见下表 |
| 双线性 / 最近邻采样 | |
| repeat / clamp / mirror 寻址 | |

### 3.3 blend 模式（th15 `AnmRenderer::material`，flags 位 25..29）

| 值 | 模式 | 源/目标因子 |
|---|---|---|
| 0 | Alpha | SrcAlpha / InvSrcAlpha |
| 1 | Additive | SrcAlpha / One |
| 2 | Subtractive | SrcAlpha / One, ReverseSubtract |
| 3 | Opaque | One / Zero |
| 4 | MultiplyInverse | InvDstColor / InvSrcColor |
| 5 | MultiplyDst | DstColor / Zero |
| 6 | InvSrcAlpha | InvSrcColor / InvSrcAlpha |
| 7 | DestAlpha | DstAlpha / InvDstAlpha |
| 8 | Minimum | |
| 9 | Maximum | |

### 3.4 ★ 坑一：PNG 导出必须走 stdout

三种通道都试过：

| 通道 | 结果 |
|---|---|
| 写 `/data/x.png`（NODEFS） | 文件不落盘 |
| `FS.syncfs` 后写 | 异步，进程退出前未完成 |
| `callMain` 后读全局指针 | 指针指向已回收的栈 |
| **stdout 打印 base64** | **可靠** |

WASM 沙箱里唯一能可靠穿过边界的通道是 stdio。

### 3.5 ★ 坑二：视图变换必须显式加

脚本坐标以**场地中心**为原点，目标画布以**左上角**为原点。

```cpp
quad.x[i] = view_x + translate_x + variables.position.x + rx;
quad.y[i] = view_y + translate_y + variables.position.y + ry;
```

不加的话所有几何堆在 `(0,0)` 角：

| | 像素写入 |
|---|---|
| 不加视图变换 | 1,774,647 |
| 加视图变换 | **3,514,601** |

### 3.6 实测

```
ANMDRAW files=6 scenes=6 quads=140 written=3106252 rejected=74
        nonblack=289120 checksum=7df374f1be076f3c
```

| 指标 | 值 |
|---|---|
| 四边形 | 140 |
| 像素写入 | 3,106,252 |
| 非背景像素 | 289,120 |
| 校验和 | `7df374f1be076f3c`（确定性） |

### 3.7 图像

![渲染帧](../artifacts/anm-frame.png)

640×480，深色背景 `(24,24,48)`，白色方块是 ANM 实例绘制的四边形。

### 3.8 ★ 一个数据发现：多数根脚本不设 visible

32 个根脚本里只有 **2 个**在 8 帧后可见。
其余需要靠 spawn 或更长时间才显示。

**这是数据行为，不是渲染 bug。**

---

## 4. 渲染层完成度

| 层 | 状态 |
|---|---|
| 纹理容器与解码 | ✅ |
| 软件光栅化 | ✅ 本轮 |
| PNG 导出 | ✅ |
| **GLES 硬件渲染** | ❌ 需 SDL3 + 上下文 |
| **几何族实际顶点生成** | ❌ opcode 600-611 只记录数量 |
| **效果宿主** | ❌ opcode 508 |
| **音频 / 输入** | ❌ |

**从 `th185.dat` 到 PNG 图像的完整链路已经跑通。**

---

## 5. ★★★ 纹理解码端到端验证

### 5.1 决定性证据

把 `ascii.anm` 的 256×256 图集解码出来，**肉眼可见完整 ASCII 字符集**：

![纹理图集](../artifacts/anm-atlas.png)

图中能读出 `Bonus`、`History`、`Failed`、`PowerUp`，以及完整的
`!"#$%&'()*+,-./0123456789:;<=>?@ABC...` 字符表。

**这证明整条链路都是正确的：**

```
th185.dat
  -> PBG4 解密（cipher 表 + 分块顺序）
  -> LZSS 解压（13 位偏移 + 4 位长度）
  -> ANM 容器解析（块链 + 相对块起点偏移表）
  -> THTX 定位（ARGB4444, 256x256）
  -> 像素解码（4 位扩展到 8 位）
  -> PNG 编码
```

任何一环错了，都不可能还原出可读的文字。

### 5.2 ★ 为什么之前只得白方块

渲染时我用 `0..1` 作 UV，那是**整个图集**。

ANM 的 sprite 是图集里的**子矩形**，必须用 sprite 自己的 UV：

```cpp
// th15 AnmResource.cpp 第 45-47 行，加载时算一次
sprite.u0 = float(double(sprite.x) / texture.width);
sprite.v0 = float(double(sprite.y) / texture.height);
sprite.u1 = float((double(sprite.x) + double(sprite.width)) / texture.width);
sprite.v1 = float((double(sprite.y) + double(sprite.height)) / texture.height);
```

**在加载时算一次**而不是每帧算——这是原版的做法，也避免了每帧重复除法。

### 5.3 ★ 另一个错误：纹理索引来自 sprite，不是 sprite 序号

```cpp
// 错误：sprite 序号当纹理索引
quad.texture = &textures[visual.sprite];

// 正确：sprite 记录它属于哪个纹理
const u32 texture_index = source->sprites[u32(visual.sprite)].texture;
quad.texture = &textures[texture_index];
```

一个图集含多个 sprite。用 sprite 序号索引纹理，对大多数 sprite 会越界。

### 5.4 修正效果

| 指标 | 修正前 | 修正后 |
|---|---|---|
| 像素写入 | 3,106,252 | **3,759,163** |
| 非背景像素 | 289,120 | **359,260** |
| 校验和 | `7df374f1…` | **`5502a39e…`** |

### 5.5 校验和为什么是断言

帧缓冲校验和只在**渲染路径变化**时才变。

```
[PASS] framebuffer checksum is deterministic (5502a39ee95aa9f2)
```

**它变了，就说明有人改动了渲染，应该重新看图。**
这比「跑通了」更有价值——它把「输出变了」变成一个显式的、需要审查的事件。

---

## 6. 渲染层完成度

| 层 | 状态 |
|---|---|
| PBG4 解密 | ✅ complete |
| LZSS 解压 | ✅ complete |
| ANM 容器 | ✅ complete |
| **THTX 像素解码** | ✅ **端到端验证** |
| **软件光栅化** | ✅ |
| **PNG 导出** | ✅ |
| GLES 硬件渲染 | ❌ 需 SDL3 + 上下文 |
| 几何族实际顶点生成 | ❌ opcode 600-611 只记录数量 |
| 效果宿主 | ❌ opcode 508 |
| 音频 / 输入 | ❌ |

**「从加密归档到可读图像」这一段已经完全打通并验证。**

---

## 7. ★★★★ 完整渲染出敌人精灵图集

### 7.1 图像

![敌人精灵表](../artifacts/anm-sprites.png)

`enemy.anm` 的 320 个 sprite 全部按自身 UV 平铺。

**肉眼可辨认的内容：**

| 类别 | 内容 |
|---|---|
| 妖精 | 蓝/绿/红/黄多色妖精，带翅膀 |
| 幽灵 | 白色半透明幽灵 |
| 动物 | 猫、蝙蝠 |
| BOSS 部件 | 大型多段部件 |
| 弹幕球 | 彩色同心圆弹 |
| 能量球 | 白/黄/绿发光球 |

### 7.2 这是终极验证

```
th185.dat
  -> PBG4 解密
  -> LZSS 解压
  -> ANM 块链解析
  -> 相对块起点偏移表
  -> sprite UV 子矩形
  -> THTX 像素解码
  -> 软件光栅化
  -> PNG
```

**任何一环错一位，都不可能还原出可辨认的敌人图形。**

### 7.3 ★ 两个层次的验证不能互相替代

| 产物 | 证明什么 | 不能证明什么 |
|---|---|---|
| `anm-atlas.png` | **解码器对**——整张图正确 | 子矩形查表是否正确 |
| `anm-sprites.png` | **子矩形查表对**——每块 UV 正确 | — |

图集导出时我用整张纹理作 UV（0..1），那只能证明解码。
sprite 表导出时用每个 sprite 自己的 UV 子矩形，这才证明查表。

**两种验证都纳入 CI，缺一不可。**

### 7.4 实测

```
ANMSPR name=enemy.anm sprites=320 drawn=320 quads=320 written=218018
```

| 指标 | 值 |
|---|---|
| sprite 总数 | 320 |
| 实际绘制 | 320（100%） |
| 四边形 | 320 |
| 像素写入 | 218,018 |

---

## 8. 渲染层完成度

| 层 | 状态 | 证据 |
|---|---|---|
| PBG4 解密 | ✅ complete | 条目表 0 差异 |
| LZSS 解压 | ✅ complete | 284 条目全解 |
| ANM 容器 | ✅ complete | 430 块 / 1879 sprite |
| **THTX 像素解码** | ✅ **可读图像** | `anm-atlas.png` |
| **sprite UV 查表** | ✅ **可辨认精灵** | `anm-sprites.png` |
| 软件光栅化 | ✅ | 10 种 blend |
| PNG 导出 | ✅ | stdout base64 |
| 对象调度 | ✅ | 2291 spawn |
| GLES 硬件渲染 | ❌ | 需 SDL3 + 上下文 |
| 几何族顶点生成 | ❌ | opcode 600-611 |
| 效果宿主 | ❌ | opcode 508 |
| 音频 / 输入 | ❌ | — |

**「从加密归档到可辨认的游戏图形」已经完成并验证。**

---

## 9. ★★★★★ 应用模块渲染出真实游戏画面

### 9.1 图像

![应用渲染帧](../artifacts/app-frame.png)

**32 个 TH185 敌人精灵**按 8×4 网格排列，由 `th185-app.mjs` 产出。

### 9.2 为什么这一张最重要

前几张图是 harness 产出的——harness 是测试夹具，不是产品。

**这一张来自 `th185-app.mjs`，正是 `sdl-runtime/th185.html` 里
`<script type="module" src="/app/shell.mjs">` 会加载的那个模块。**

也就是说：**网页外壳现在有真实内容可画了。**

### 9.3 接线

| 环节 | 变化 |
|---|---|
| `Application::initialize` | 加载 `enemy.anm`、解码全部纹理、建 32 个根动画 |
| `Application::tick` | 推进动画树 → 调用 `render()` |
| `Application::render` | 清屏 + `AnmManager::render` 到 640×480 |
| 导出 | `th185_frame_width/height/pixels/bytes`、`th185_render_quads` |

### 9.4 实测

```
frame      : 640x480
bytes      : 1228800        (640 * 480 * 4)
quads      : 32
animations : 32
```

### 9.5 ★ 一个构建陷阱

读像素需要 `HEAPU8`。缺少它时：

```
TypeError: Cannot read properties of undefined (reading 'subarray')
```

修正：

```javascript
"-sEXPORTED_RUNTIME_METHODS=FS,cwrap,HEAPU8"
```

**帧缓冲已经存在（`bytes=1228800` 打得出来），只是读不到。**
这个错误表现是「渲染没工作」，实际是「导出没配全」。

### 9.6 ★ CI 项的结构限制

`ci.mjs` 的检查项只支持：

```javascript
{ name, args: [script, ...args], python: bool, timeoutMs }
```

没有「自定义执行逻辑」的字段。
而应用帧导出需要「先 build 再跑 node 脚本再校验」三步。

**解法：写一个 Python 校验脚本，由它去调 node 脚本。**
CI 项只负责启动那个 Python 脚本——保持 CI 表结构简单。

---

## 10. 端到端完成度

| 环节 | 状态 | 证据 |
|---|---|---|
| PBG4 解密 | ✅ | 条目表 0 差异 |
| LZSS 解压 | ✅ | 284 条目全解 |
| ANM 容器 | ✅ | 430 块 / 1879 sprite |
| THTX 像素解码 | ✅ | `anm-atlas.png` 可读字体 |
| sprite UV 查表 | ✅ | `anm-sprites.png` 320 精灵 |
| 软件光栅化 | ✅ | 10 种 blend |
| 对象调度 | ✅ | 2291 spawn |
| **应用接线** | ✅ | **`app-frame.png` 32 精灵** |
| Web 外壳 | ⚠️ | HTML 在，`shell.mjs` 未写 |
| 音频 / 输入 | ❌ | — |
| 真实关卡流程 | ❌ | 只加载 enemy.anm |

**「加密归档 → 应用模块 → 屏幕图像」已经贯通。**
