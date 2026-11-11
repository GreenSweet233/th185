# portable

工作区共享的渲染器与输入代码。**不是本仓库原创**，从 TH11 最终版固定（pin）而来。

## 来源

`../th11/portable/`：

| 文件 | 职责 |
|---|---|
| `sdl/Renderer.cpp` / `Renderer.hpp` | GLES 语义批处理渲染器 |
| `sdl/GraphicsState.hpp` | 图形状态机 |
| `sdl/Shaders.hpp` | 着色器源码 |
| `sdl/FrameCadence.hpp` | 帧调度 |
| `sdl/ResampleShader.hpp` | 重采样 |
| `sdl/LegacyGraphics.hpp` | 旧版图形兼容 |
| `input/KeyboardMap.inc` | 键盘映射 |
| `input/TouchController.hpp` | 触控控制器 |
| `input/MotionTrack.hpp` | 触摸轨迹 |

## 固定方式

首次接入时把上述文件从 `../th11/portable/` 复制过来，并把实际 SHA-256 写进 `th185_web/config/final-baseline.json` 的 `files` 段（当前为 `TO-BE-PINNED` 占位）。

**这些文件是本仓库的副本，不是符号链接**——上游更新不会自动同步，需要手动重新固定并更新哈希。
