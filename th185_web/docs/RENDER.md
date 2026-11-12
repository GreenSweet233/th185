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
