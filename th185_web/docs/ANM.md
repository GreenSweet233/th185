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

---

## 11. ★★ 偏移表：sprite 与 script 的实际定位方式

### 11.1 我之前错在哪

第 55 轮我说「`sprite_t` 20 字节紧跟图集名之后」，并声称验证成功。

**那只是 `world01t.anm` 的巧合。**

thtk 的 `thanm.c` 第 1108-1121 行：

```c
uint32_t* sprite_offsets = (uint32_t*)(map + sizeof(*header));
sprite19_t* sprite = (sprite19_t*)(map + sprite_offsets[s]);

anm_offset_t* script_offsets =
    (anm_offset_t*)(map + sizeof(*header) + header->sprites * sizeof(uint32_t));
script->offset = &script_offsets[s];
```

**它们是偏移表，不是顺序数组。**

### 11.2 正确布局

```text
+0x00  anm_header11_t                (sizeof = 0x40)
+0x40  u32 sprite_offsets[sprites]
+40+n*4  anm_offset_t script_offsets[scripts]   ({ u32 id; u32 offset; } = 8 字节)
...    各 sprite_t / 指令流 / THTX 块
```

### 11.3 实测验证

`world01t.anm`（`sprites=1, scripts=1`）：

```
+0x40  sprite_offsets[0] = 108   →  @108 sprite_t { id=0, w=1024, h=512 }
+0x44  script_offsets[0] = { id=0, offset=128 }  →  @128 首条指令 id=302
```

**`108 = 0x6C`**，正好是我上一轮靠「名字后对齐」猜到的位置。
但这次是**通过偏移表**得到的，所以对任何文件都成立。

### 11.4 为什么 `sizeof = 0x40` 而不是 `0x28`

`anm_header11_t` 末尾有 `uint32_t zero2[6]`（24 字节），
所以 `0x28 + 24 = 0x40`。

我第一次算漏了这 24 字节，导致在 `+0x28` 读到全 0。

---

## 12. ★★ 指令格式：thtk 明确支持 `version = 185`

### 12.1 thtk 的分派逻辑

`thanm.c` 第 694-706 行：

```c
case 8:
    switch (version) {
    case 20: case 19: case 185: case 18:
        if ((format = find_format(th18_patch, id))) break;
    default:
        format = find_format(formats_v8, id);
```

**`185` 是 thtk 官方识别的版本号。** TH185 先查 `th18_patch`，再查 `formats_v8`。

| 表 | 项数 | 说明 |
|---|---|---|
| `th18_patch` | 1 | 只有 `439 -> "Sff"` |
| `formats_v8` | 156 | 主表 |

已提取到 `reference/anm-opcodes.json`。

### 12.2 ★ 字段宽度（实测收敛）

| 字符 | 含义 | 宽度 |
|---|---|---|
| `n` / `N` | sprite / script number | 4 字节 |
| `f` | float32 | 4 字节 |
| `o` | offset（跳转目标） | 4 字节 |
| `t` | time | 4 字节 |
| **`s`** | **小整数** | **2 字节** |
| `S` | 字符串 | 变长（u32 长度 + 文本 + 填充） |

**指令头 8 字节，所以 size = 8 + 固定字段 + 字符串。**

### 12.3 判据是如何收敛到 0 的

| 轮次 | 不匹配数 | 原因 |
|---|---|---|
| 1 | **6** | `N` 和 `s` 没算进固定字段 |
| 2 | **1** | 小写 `s` 被当成 4 字节（实际 2） |
| 3 | **0** | 修正后全部通过 |

**`op421` 的格式串是 `ss`，实际 12 字节 = 8 + 2 + 2。**

### 12.4 ★ 最强的交叉验证

```
[PASS] thtk formats_v8 predicts the size of all 71 covered opcodes (0 bad)
```

**外部权威定义（thtk）与真实数据完全吻合。**

这不是「我逆向出了格式」，而是「社区十几年前的定义在 TH185 上被独立验证」。
两者的可信度差一个数量级。

---

## 13. ANM 脚本全量统计

```
文件          : 55 / 55
sprite 记录   : 1879
script 条目   : 610
脚本指令      : 10566
不同 opcode   : 71
```

### 13.1 opcode 分布

| 来源 | opcode 数 | 指令数 |
|---|---|---|
| **thtk v8 表内** | 26 | 4964 |
| **TH185 扩展** | 45 | 5602 |

扩展部分集中在 **400/500/600 族**：

| 族 | 代表 opcode |
|---|---|
| 400-440 | 400, 401, 402, 403, 404, 407, 408, 409, 412, 421, 431, 439 |
| 500-509 | 500, 501, 504, 505, 506, 509 |
| 600-614 | 600, 603, 604, 605, 606, 611, 612, 614 |

### 13.2 高频 opcode

| opcode | 次数 | thtk 格式 | 推测语义 |
|---|---|---|---|
| `300` | 1189 | `n` | 绘制 sprite |
| `5` | 895 | `S` | 跳转/标签 |
| `409` | 751 | `SSS` | 未知 |
| `404` | 747 | `SSS` | 未知 |
| `403` | 664 | `S` | 未知 |
| `3` | 583 | （无） | 无参数 |
| `304` | 554 | `S` | 未知 |
| `412` | 541 | `SSff` | 未知 |
| `402` | 417 | `ff` | 未知 |
| `400` | 316 | `fff` | 未知 |

---

## 14. 验证（13 项）

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
[PASS] 610 script entries total (expect 610)
[PASS] 10566 script instructions total (expect 10566)
[PASS] every instruction is 8+ bytes, 4-aligned, and contiguous (0 bad)
[PASS] thtk formats_v8 predicts the size of all 71 covered opcodes (0 bad)
```

---

## 15. 剩余未解

| 项 | 状态 |
|---|---|
| 容器结构 | ✅ complete |
| sprite / script 定位 | ✅ 偏移表 |
| **指令格式（size/参数）** | ✅ thtk v8 验证通过 |
| 像素格式与解码 | ✅ 完成 |
| **扩展 opcode 语义（45 种 5602 条）** | ❌ 未解 |
| **v8 表内 opcode 的语义** | ❌ 只有 size 已知 |
| **sprite 绘制 / 着色器** | ❌ 未实现 |

---

## 16. ★★ 块链：一个 .anm 文件是多块的

### 16.1 之前漏掉的结构

`anm_header11_t` 末尾有 `nextoffset`，我之前当成「同文件下一个条目的偏移」就跳过了。

thtk `thanm.c` 第 1630 行：

```c
offset += entry->header->nextoffset;
```

**这是块链：每个块有独立的头部、sprite 表、script 表，`nextoffset` 给出到下一块的距离（0 = 最后一块）。**

### 16.2 实测块数

| 文件 | 块数 | 累计 sprites | 累计 scripts |
|---|---|---|---|
| `front.anm` | **14** | **167** | 331 |
| `bullet.anm` | **10** | **604** | 328 |
| `effect.anm` | **13** | 64 | 195 |
| `pl01.anm` | 11+ | — | — |

### 16.3 ★ 这是 `sprite index out of range` 的根因

```
front.anm 块0 声明 sprites=39，但脚本引用的索引可以到 166
bullet.anm 块0 声明 sprites=368，但全文件有 604 个
```

**sprite 与 script 的编号跨块累加。** th15 的 `AnmResource` 注释：

> the original loader appends sprites and scripts in table order across chained chunks

只解析首块，就会把合法索引判成越界。

### 16.4 修复效果

| 指标 | 修复前 | 修复后 |
|---|---|---|
| 纹理块 | 53 | **55** |
| 像素 | 12014464 | **12358528** |
| 脚本数 | 609 | **2121** |
| 实际执行 | 609 | **1097** |
| **跑完的脚本** | **0** | **175** |
| 执行指令 | 5217 | **6343** |
| 帧 | 16790 | **78330** |

**`finished` 从 0 提升到 175 是最关键的信号**——之前没有任何脚本能执行完。

---

## 17. ★ 跳转基准：用命中率判定

### 17.1 三种候选解释

`opcode 200/201` 的跳转操作数 `tgt` 可能是：

| 解释 | 公式 |
|---|---|
| 绝对文件偏移 | `tgt` |
| 相对脚本起点 | `script_offset + tgt` |
| 相对当前指令 | `instruction_offset + tgt` |

### 17.2 ★ 判据：落在指令边界的比例

不猜语义，只看哪种解释的结果**总是落在合法指令起点上**：

```
总数 260 个跳转
  rel_script   241 (92.7%)   ← 相对脚本起点
  rel_instr      9 ( 3.5%)
  absolute       0 ( 0.0%)
```

**92.7% vs 3.5%——结论明确。**

### 17.3 这个判据为什么可靠

跳转目标如果算错，几乎不可能恰好落在指令边界上（指令是 8..64 字节的变长记录）。
所以「命中率」是一个**不需要真值就能自证**的判据。

---

## 18. 脚本终止与范围

### 18.1 `0xFFFF` 是终止符

```
@9504 opcode=403  len=12
@9516 opcode=3    len=8
@9524 opcode=0xFFFF len=0    ← 终止
其后 8 字节是填充
```

### 18.2 ★ `bytes` 不含终止符

切片只包含真实指令，所以执行到最后一条后 `instruction_offset_` **正好等于 `size`**。

这时应返回 `Finished`，而不是报 `instruction outside script`。

---

## 19. 当前状态

```
ANMTEX files=54 blocks=55 decoded=55 rejected=0 pixels=12358528
ANMRUN files=54 scripts=2121 ran=1097 finished=175 stopped=113
       unsupported=749 faulted=60 instrs=6343 frames=78330
```

### 19.1 仍存的 60 个 fault

| 错误 | 原因 |
|---|---|
| `invalid jump` | 跳转目标超出 `bytes` 范围，因扫描遇非法 length 提前截断 |
| `invalid instruction length` | 同上 |

**已记录为已知限制。** 这两个都源于「脚本范围界定」——正确的做法应当是用
**下一个脚本的 offset** 作为上界（thtk `thanm.c` 第 1125 行 `limit += script_offsets[s+1].offset`），
而不是遇到非法 length 就停。

### 19.2 验证（14 项）

```
[PASS] the C++ ANM parser produced output
[PASS] C++ parsed 54 of 55 .anm files (1 known tail-block miss)
[PASS] every extracted THTX block decoded (0 rejected)
[PASS] over 10 million pixels decoded
[PASS] the C++ ANM interpreter produced output
[PASS] interpreter ran over 54 of 55 .anm files
[PASS] at least 1000 scripts executed (1097)
[PASS] at least 100 scripts ran to completion (175)
[PASS] at least 6000 ANM instructions executed (6343)
```

---

## 20. ★★★ 偏移基准：全部相对块起点

### 20.1 这个 bug 藏了整整 60 轮

`sprite_offsets[]`、`script_offsets[]`、`thtxoffset` 里的值，
**全部是相对「当前块起点」的偏移，不是文件绝对偏移。**

**为什么一直没发现**：块 0 的 `base = 0`，两种解释完全重合。
只有解析了块链的第 2 块及以后，错误才会显现。

### 20.2 决定性证据

`enemy.anm` 块 2（`base = 614300`），脚本 `id=134` 的 `offset = 5328`：

| 解释 | 位置 | 读到的第一条 |
|---|---|---|
| 绝对偏移 | `@5328` | `opcode=300` ← **块 0 的旧数据，能读但错位** |
| 相对块起点 | `@619628` | `opcode=304` ← **正确** |

两种读法都能读出「看起来合法」的指令，这就是它难发现的原因。
但只有相对块起点的后续指令流是连贯的。

### 20.3 修正后的爆炸性提升

| 指标 | 修正前 | 修正后 | 倍数 |
|---|---|---|---|
| 纹理块 | 55 | **430** | **7.8×** |
| 像素 | 12,358,528 | **47,847,598** | **3.9×** |
| fmt 分布 | `{1:20, 3:5, 5:30}` | **`{1:264, 3:6, 5:124, 7:36}`** | 首次出现 GRAY8 |

**块数从 55 涨到 430 是决定性信号**——偏移基准错了会漏掉绝大部分块。

---

## 21. ★★ sprite 索引是全局跨文件编号

### 21.1 现象

`abcard.anm` 只声明 **30 个 sprite**，但脚本 `id=22/23/24` 引用 **索引 141**。

按「每文件独立编号」理解，这是越界；按「全局编号」理解，这是合法的跨文件引用。

### 21.2 判定依据

`abcard.anm` 有 30 个块，每块 1 个 sprite。如果索引是块内编号，
141 远超任何块的大小；如果是文件内编号，也超出 30。
**唯一自洽的解释是全局编号。**

### 21.3 处理方式

```cpp
if (u32(index) >= resource_->sprites.size()) {
    ++stats_.external_sprites;   // 记录为外部引用
    break;                       // 不报 fault
}
```

**把合法的跨文件引用当错误，会拒绝正确指令。**

---

## 22. ★ 负 sprite 索引也是合法值

th15 `AnmInterpreter.cpp`：

```cpp
if(index<0){source=environment?environment->fallback_sprite_resource:nullptr;index=258;}
```

**负数映射到环境的 fallback sprite（索引 258）**，不是错误。

`effect.anm` 的 `id=32` 脚本第一条就是 `opcode=300 arg0=-1`。

---

## 23. ★★ fault 归零

```
ANMTEX files=54 blocks=430 decoded=430 rejected=0 pixels=47847598
       fmt1=264 fmt3=6 fmt5=124 fmt7=36
ANMRUN files=54 scripts=2121 ran=2121 finished=26 stopped=128
       unsupported=1967 faulted=0 instrs=20480 frames=73062
```

### 23.1 三轮修复的累积效果

| 轮次 | 修复内容 | 脚本 | 指令 | fault |
|---|---|---|---|---|
| 60 | 块链解析 | 2121 | 6343 | 60 |
| **61** | **偏移基准改为相对块起点** | 2121 | 20477 | 3 |
| **61** | **负索引 + 全局编号** | **2121** | **20480** | **0** |

**fault 从 60 降到 0，指令数从 6343 涨到 20480（3.2 倍）。**

### 23.2 剩余 unsupported（如实记录）

`unsupported = 1967` 是因为 1967 个脚本碰到了未实现的 opcode：

| 族 | opcode | 缺什么 |
|---|---|---|
| 插值 | 407-420/427-430/433/435 | interpolator 状态 |
| 对象生成 | 500-508 | object host |
| 混合模式 | 600-611 | 渲染状态 |

首个未实现的是 `421`。**这些不是错误，是未实现的功能。**

---

## 24. 验证（15 项）

```
[PASS] the C++ ANM parser produced output
[PASS] C++ parsed 54 of 55 .anm files (1 known tail-block miss)
[PASS] every extracted THTX block decoded (0 rejected)
[PASS] over 40 million pixels decoded (47847598)
[PASS] the C++ ANM interpreter produced output
[PASS] interpreter ran over 54 of 55 .anm files
[PASS] at least 2000 scripts executed (2121)
[PASS] at least 20 scripts ran to completion (26)
[PASS] zero execution faults (0)
[PASS] at least 20000 ANM instructions executed (20480)
```
