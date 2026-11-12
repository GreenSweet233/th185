# TH185 ANM 动画容器

> 状态：**完全解出（55/55 文件，1879 个 sprite 记录，0 错误）**
> 依据：thtk 的 anm_types.h（社区权威定义），非自行逆向

---

## 0. 为什么改用 thtk 的定义

第 38 轮我花整整一轮逆向 ANM 头部，结论有**三处是错的**：

| 我的推断 | thtk 的实际定义 | 后果 |
|---|---|---|
| +0x04 是「文件大小回写」 | 是 sprites（sprite 定义数） | 误判字段语义 |
| +0x18 是「sprite 定义数」 | 是 memorypriority | **整整一轮卡在这里** |
| sprite 记录位置未知 | sprite_t 20 字节，紧跟图集名之后 | 无法解析 |

改用 thtk 定义后：**55/55 全部解析，1879 个 sprite，0 错误**。

> **教训**：TH 系列是社区研究多年的格式，thtk 已经记录了 TH13+ 的完整布局。
> 自己从字节猜结构，是在重复别人十几年前做过的工作，而且更容易错。
> 正确顺序是：**先查权威定义，再用实测数据验证**。

---

## 1. 头部（anm_header11_t，TH13+ 布局）

```c
typedef struct {
    uint32_t version;        // +0x00  TH185 = 8
    uint16_t sprites;        // +0x04  sprite_t 记录数
    uint16_t scripts;        // +0x06  脚本条目数
    uint16_t zero1;          // +0x08
    uint16_t w, h;           // +0x0A, +0x0C  图集尺寸
    uint16_t format;         // +0x0E  像素格式
    uint32_t nameoffset;     // +0x10  图集路径（或 "@"）
    uint16_t x, y;           // +0x14, +0x16
    uint32_t memorypriority; // +0x18  D3D 纹理内存优先级
    uint32_t thtxoffset;     // +0x1C  第一个 THTX 块
    uint16_t hasdata;        // +0x20
    uint16_t lowresscale;    // +0x22  TH14+：低分辨率时需缩小
    uint32_t nextoffset;     // +0x24  同文件下一个条目的偏移
    uint32_t zero2[6];       // +0x28
} anm_header11_t;
```

---

## 2. Sprite 记录

```c
typedef struct {
    uint32_t id;
    float x, y, w, h;
} sprite_t;    // 20 字节
```

**紧跟图集名之后，4 字节对齐。**

这就是第 38 轮我卡住的「定长 sprite 定义记录」。

实测示例（world01t.anm，sprites = 1）：

```
0x6C  sprite_t { id=0, x=0, y=0, w=1024.0, h=512.0 }
0x80  指令流开始（id=302 size=12 ...）
0xD4  THTX 块
```

---

## 3. 像素格式（thtk format_t）

| 值 | 名称 | TH185 文件数 |
|---|---|---|
| 1 | BGRA8888 | 19 |
| 3 | RGB565 | 4 |
| 5 | ARGB4444 | **32** |
| 7 | GRAY8 | 0 |

---

## 4. THTX 纹理块

```c
typedef struct {
    char     magic[4];   // "THTX"
    uint16_t zero;
    uint16_t format;
    uint16_t w, h;
    uint32_t size;
    unsigned char data[];
} thtx_header_t;
```

实测（world01t.anm @212）：

```
magic="THTX" zero=0 fmt=5 w=512 h=512 size=524288
524288 = 512 * 512 * 2  (ARGB4444 = 2 字节/像素)
```

---

## 5. text.anm 的 @ 不是缺失路径

text.anm 的图集名是 **@**。

thtk 文档原文：

> Can also be communicated by setting the first char of the file name to
> '@'. In TH06, this is the only way, that game doesn't use this field.

**@ 表示「图像内嵌在本文件」**，不是路径缺失。

第 38 轮我把 text.anm 定性为「字体图集特例」——定性方向对了，
但理由是猜的。现在有权威依据。

---

## 6. lowresscale：高分辨率时代的特征

thtk 注释：

> From TH14 on, used to mark images as "needing to be scaled down when
> running in lower resolutions".

TH185 有 **8 个文件**置 1。

**这是 TH14+ 才有的字段**，印证了 TH185 属于高分辨率时代，
渲染实现应当对齐 th15 而非 th11。

---

## 7. 全量统计

```
文件数        : 55
解析成功      : 55 / 55
sprite 记录   : 1879
format 分布   : {5: 32, 1: 19, 3: 4}
lowresscale=1 : 8 个文件
```

典型文件：

| 文件 | sprites | scripts | 尺寸 | format | 图集 |
|---|---|---|---|---|---|
| world01t.anm | 1 | 1 | 512x512 | 5 | background/world01t/st01a.png |
| enemy.anm | 120 | 89 | 512x512 | 5 | enemy/enemy.png |
| bullet.anm | 368 | 72 | 256x256 | 1 | bullet/bullet1.png |
| pl01.anm | 61 | 43 | 256x512 | 1 | player/pl01/pl01.png |
| text.anm | 88 | 60 | 1024x1024 | 5 | @ |

---

## 8. 验证（9 项）

```
[PASS] found 55 .anm files (expect 55)
[PASS] all 55 .anm files parse (0 errors)
[PASS] every file reports version 8
[PASS] sprite count matches the header for every file (0 bad)
[PASS] 1879 sprite records total (expect 1879)
[PASS] format distribution is {5: 32, 1: 19, 3: 4}
[PASS] every THTX block has a whole bytes-per-pixel of 1/2/4 (0 bad)
[PASS] text.anm uses the in-file atlas marker "@"
[PASS] 8 files set lowresscale (expect 8)
```

---

## 9. 工具

```powershell
python th185_web/scripts/native/anm.py --selftest      # 9 项
python th185_web/scripts/native/anm.py --list          # 55 个文件摘要
python th185_web/scripts/native/anm.py --file world01t.anm
```

---

## 10. 剩余未解

| 项 | 状态 |
|---|---|
| 容器结构 | ✅ **complete** |
| sprite 记录 | ✅ 1879 个全部解析 |
| THTX 纹理 | ✅ 头与像素格式全部解出 |
| **ANM 脚本指令语义** | ❌ 未解（与 ECL 共用编码，但 opcode 不同） |
| **脚本与 sprite 的关联方式** | ❌ 未解 |
| **图集像素解码** | ⚠️ 格式已知，解码器未写 |

容器层已 complete，**脚本语义是渲染层的事**，属于 SYS-RENDER-ANM 的范围。
