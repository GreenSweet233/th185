# reference/ — 外部权威定义

这个目录存放**不是本项目逆向出来的**格式定义，来源标注清楚，用于避免重复发明。

## thtk/

来自 `thvip/eagler-touhou-th11/ports/th15/web/reference/tools/thtk/`，是
thtk（Touhou Toolkit）的格式头文件。thtk 是社区长期维护的东方格式库。

| 文件 | 用途 |
|---|---|
| `anm_types.h` | **ANM 容器结构**（`anm_header11_t` / `sprite_t` / `thtx_header_t`） |
| `dattypes.h` | PBG4 归档条目结构 |
| `provenance.json` | thtk 版本与来源记录 |

## 为什么用它

TH185 的 ANM 头部是 `version = 8`，属于 thtk 文档里的 **TH13+ 布局**（`anm_header11_t`）。

我在第 38 轮花了整整一轮逆向这个结构，结论有三处是错的：

| 我的推断 | thtk 的定义 | 后果 |
|---|---|---|
| `+0x04` 是「文件大小回写」 | 是 `sprites`（sprite 定义数） | 误判字段语义 |
| `+0x18` 是「sprite 定义数」 | 是 `memorypriority` | 整整一轮卡在这里 |
| sprite 记录位置未知 | `sprite_t` 20 字节，紧跟图集名之后 | 无法解析 |

改用 thtk 定义后：**55/55 文件全部解析，1879 个 sprite 记录，0 错误**。

## 用法约定

1. **不要修改这里的文件**——它们是外部来源，改了就无法追溯。
2. 本项目的实现（`scripts/native/anm.py`、`cpp/game/`）引用这里的定义，
   并在注释里标注「依据 thtk anm_types.h」。
3. 如果发现 thtk 定义与实测数据冲突，**记录冲突而不是静默修改**——
   冲突本身说明 TH185 在这个格式上有改动，是有价值的信息。

## 其他可参考来源

| 来源 | 位置 | 可参考内容 |
|---|---|---|
| th15 完整实现 | `thvip/eagler-touhou-th11/ports/th15/` | 高分辨率渲染（`portable/sdl/Renderer.cpp` 34KB）、工作流 |
| th15 参考数据 | 同目录 `web/reference/*.json` | `anm-formats.json`、`sht-layout.json`、`bullet-appearance.json` |
| th11 实现 | `thvip/th11/th11_web/` | ECL 解释器、SDL 宿主、启动器 |

**th185 与 th15 同属 th14+ 的高分辨率时代**，渲染层的做法应当对齐 th15 而非 th11。

