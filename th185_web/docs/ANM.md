# TH185 ANM 动画容器

> 状态：**容器层解出（55/55 文件解析，552 个纹理块）；sprite 定义记录未解。**
> 日期：2026-11-12

---

## 1. 概览

`th185.dat` 里有 **55 个 `.anm`**，存放精灵图集与使用它们的脚本动画。
最大的 `front.anm` 有 8.9 MB。

```
+0x00  uint32  version = 8
+0x04  uint32  文件大小（回写）
+0x08  uint32  常量 0x02000000（bullet.anm 是 0x01000000）
+0x0C  uint32  常量 0x00050200
+0x10  uint32  ★ 图集路径表偏移
+0x14  uint32  保留 = 0
+0x18  uint32  ★ sprite 定义数（不是路径数）
+0x1C  uint32  ★ 第一个脚本偏移
```

---

## 2. 图集路径表（`+0x10`）

NUL 结尾、4 字节对齐的**完整路径**：

```
world01t.anm: +0x10 = 76   -> "background/world01t/st01a.png"
enemy.anm:    +0x10 = 1256 -> "enemy/enemy.png"
bullet.anm:   +0x10 = 2112 -> "bullet/bullet1.png"
```

### 2.1 ★ 一个关键区分：路径数 ≠ `sprite_count`

`+0x18` 的 `sprite_count` 数的是**sprite 定义**，不是路径。
实测差值：

```
sprite_count - 路径数 = -1 / 9 / 10
```

如果把 `sprite_count` 当路径数用，走几步就会撞进定长的 sprite 定义记录，
读出空字符串——我在这上面错了两次。

### 2.2 正确做法

按「可打印 ASCII 且以 `.png`/`.jpg` 结尾」判定路径，遇不符合即停：

```python
while pos < len(blob):
    end = blob.find(b'\x00', pos)
    text = blob[pos:end]
    if not text: break
    if not all(32 <= b < 127 for b in text): break
    if not (text.endswith(b'.png') or text.endswith(b'.jpg')): break
    paths.append(text.decode('ascii'))
    pos = (end + 1 + 3) & ~3
```

---

## 3. THTX 纹理块

自描述，全文件扫描即可找到：

```c
struct ThtxBlock {
    char   magic[4];    // "THTX"
    uint16 format;      // 观察值 0
    uint16 width;       // 512 / 256
    uint16 height;      // 512 / 256
    uint16 pad;
    uint32 data_size;   // 524288 = 512*512*2  或  262144 = 256*256*4
    uint8  pixels[data_size];
};
```

**共找到 552 个纹理块，全部满足 `data_size == width * height * bpp`。**

> 注意：两个块的 bpp 不同（2 vs 4），所以**像素格式不能只看头部**，
> 必须由消费方决定。`data_size` 才是可靠字段。

---

## 4. 脚本

**ANM 脚本与 `.std` / `.ecl` 用同一种编码**：

```c
uint16 id; uint16 size; payload[size - 4];
// id == 0xFFFF 终止
```

`world01t.anm` 第一个脚本（@0x80）的 6 条指令：

```
id=302  size=12
id=300  size=12
id=401  size=20
id=403  size=12
id=5    size=12
id=3    size=8
```

**这是第三个复用同一套指令编码的格式**（前两个是 `.std` 和 `.ecl`），
说明 TH185 有一层统一的脚本 VM。

---

## 5. 特例：字体图集

4 个文件是字体图集，其表项是**字形槽位**而非图片路径：

| 文件 | 说明 |
|---|---|
| `text.anm` | 日文/中文文字图集 |
| `ascii.anm` | ASCII 位图字体 |
| `ascii_960.anm` | 960 分辨率变体 |
| `ascii1280.anm` | 1280 分辨率变体 |

**不要把「没有 .png 名字」当成解析错误。**

---

## 6. 验证（10 项）

```
[PASS] found 55 .anm files (expect 55)
[PASS] all 55 .anm files parse (0 errors)
[PASS] every file reports version 8
[PASS] every non-font file has at least one atlas path (51 files)
[PASS] parsed 54 atlas paths across all files
[PASS] every sprite path ends with an image extension (0 bad)
[PASS] 4 font atlases parsed: ascii.anm, ascii1280.anm, ascii_960.anm, text.anm
[PASS] found 552 THTX texture blocks
[PASS] THTX data_size equals width * height * whole bpp
[PASS] parsed 117 ANM script instructions
```

> 自检耗时约 2 分钟（要解压 55 个文件，最大 8.9 MB），是 CI 里最慢的一项。

---

## 7. 工具

```powershell
python th185_web/scripts/native/anm.py --selftest      # 10 项
python th185_web/scripts/native/anm.py --list          # 55 个文件摘要
python th185_web/scripts/native/anm.py --file world01t.anm
```

---

## 8. 未解项

| 项 | 状态 |
|---|---|
| 头部字段 | ✅ 完全解出 |
| 图集路径表 | ✅ 54 个路径 |
| THTX 纹理块 | ✅ 552 个，头部完全解出 |
| 脚本指令编码 | ✅ 与 std/ecl 同款 |
| **sprite 定义记录（定长）** | ❌ **未解** |
| **THTX 像素格式判定** | ❌ 需消费方决定 |
| **117 条 ANM 指令的语义** | ❌ 未解 |
| **脚本与 sprite 的关联** | ❌ 未解 |

**`SYS-CORE-ANM` 保持 `in-progress`。**
容器能完整遍历，但 sprite 定义和指令语义是渲染的前置，尚未解出。

> 本轮在 ANM 上花了较多轮次，边际收益已下降。
> 已明确记录未解部分，转向其他子系统更划算。
