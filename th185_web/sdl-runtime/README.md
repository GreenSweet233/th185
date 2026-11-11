# sdl-runtime

Web 运行时壳。结构沿用 `../th11/th11_web/sdl-runtime/`：

| 文件 | 作用 |
|---|---|
| `th185.html` | 页面壳（canvas + 触控 + 欢迎/暂停面板） |
| `shell.mjs` | 加载 wasm、驱动帧循环、桥接触控与键盘（⬜ 待实现） |
| `managed.mjs` | 启动器托管协议实现（⬜ 待实现） |
| `style.css` / `managed.css` | 样式（⬜ 待实现） |
| `frame-clock.mjs` | 固定 60Hz 帧调度（⬜ 待实现） |
| `keyboard.mjs` | 键盘映射（⬜ 待实现） |

> 这些文件将从 `th11_web/sdl-runtime/` 适配而来。差异点：TH185 有**卡牌系统**，触控需要额外的"使用主动卡"与"切换主动卡"按钮（原版键位 C / D）。
