# cpp/sdl

平台层。结构与 `../th11/th11_web/cpp/sdl/` 一致。

| 文件 | 职责 | 状态 |
|---|---|---|
| `Application.cpp` | SDL 输入、音频、文件与网页主循环 | ⬜ |
| `GraphicsDevice.cpp/.hpp` | `ZunGraphics` 语义接口 → 共享 `portable/sdl/Renderer` | ⬜ |
| `AudioDevice.cpp/.hpp` | miniaudio + SDL3 音频流 | ⬜ |
| `FontDevice.cpp/.hpp` | 字体栅格化 | ⬜ |

渲染链：`AnmRenderer` → `ZunGraphics` → `GraphicsDevice` → `portable/sdl/Renderer.cpp` → SDL3 / WebGL2。

> TH185 使用 D3DX9 的 `d3dx9_43.dll`（与本机 th18.exe 相同的导入表），但这只是原版的实现细节。重实现使用共享的 GLES 语义渲染器，**不移植 D3D9**。
