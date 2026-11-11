# TH185 关卡布局（.std）

> 状态：**9 个 `.std` 全部解析成功；波次项与实体项结构已解出，字段语义部分待确认。**
> 日期：2026-11-11

---

## 1. `.std` 是什么

`th185.dat` 里有 **9 个 `.std`**，与调研的「9 个场景」完全对应：

| 文件 | 波次数 | 组数 | 实体数 |
|---|---|---|---|
| `world01t.std` | 7 | 7 | 88 |
| `world01.std` | 4 | 10 | 24 |
| `world02.std` | 9 | 9 | 88 |
| `world03.std` | 7 | 43 | 68 |
| `world04.std` | 4 | 35 | 52 |
| `world05.std` | 3 | 14 | 12 |
| `world06.std` | 5 | 23 | 37 |
| `world07.std` | 1 | 8 | 5 |
| `world08.std` | 1 | 8 | 5 |

**重要区分**：`.std` **不是**波次模板池。
111 项的波次名表（VA `0x4B5D50`，见 `docs/WAVES.md`）是全局表；
`.std` 存的是**每个场景自己的布局**。

---

## 2. 文件结构

### 2.1 头部（16 字节）

```c
struct StdHeader {
    uint16 wave_count;      // +0x00  波次槽位数
    uint16 group_count;     // +0x02  实体组数
    uint32 entity_offset;   // +0x04  实体表绝对偏移
    uint32 unknown;         // +0x08  观察到 500..2704，语义待定
    uint32 reserved;        // +0x0C  恒为 0
};
// +0x10: char anm_name[];  NUL 结尾，如 "world01t.anm"
```

### 2.2 波次偏移表（`+0x90`）

`wave_count` 个 `uint32`，指向各波次项的绝对偏移。

`world01t.std` 实测：`172, 232, 292, 352, 412, 472, 532`

**步长恒为 60 字节。**

### 2.3 波次项（60 字节）

```c
struct WaveEntry {
    uint32 marker;    // +0x00  0x00010000 / 0x00060001 / 0x00020004 ...
                      //        低 16 位像是某个索引
    uint8  pad[0x0C];
    float  f1;        // +0x10  256.0 / 300.0
    float  f2;        // +0x14  256.0 / 1024.0
    float  f3;        // +0x18  256.0 / 512.0
    uint32 flags;     // +0x1C  0x001C0000
    uint32 slot_id;   // +0x20  槽位 id（不是序号，见下）
    uint8  pad2[0x0C];
    float  g1, g2;    // +0x30 与 f1/f2 同值
    uint32 marker2;   // +0x38  0x0004FFFF
};
```

#### 关于 `+0x20` 字段

**它不是序号。** 实测序列：

| 场景 | `+0x20` 序列 |
|---|---|
| `world01t` | `0,1,2,3,4,5,6` |
| `world03` | `0,1,2,2,2,9,10` |
| `world04` | `1,0,5,4` |
| `world05` | `0,5,6` |

同一个值会重复出现，且不连续，说明它是**波次类型/槽位 id**，
可能指向 111 项波次名表的某个子集。

### 2.4 实体项（16 字节）

```c
struct EntityEntry {
    uint32 kind;   // 高字节 0x01，如 0x01000000
                   //   bits 16..23 = type
                   //   bits  0..15 = index
    float  x, y, z;
};
```

9 个场景合计 **379 个实体**。

坐标范围实测：`-256` 到 `4352`（`world04` / `world06` 有 `y=4352` 的大值）。

---

## 3. 与波次系统的关系

```
全局波次名表 (111 项, VA 0x4B5D50)
        ↓  被 .std 的 slot_id 引用？
.std 波次项 (每场景 wave_count 个)
        ↓  每个波次项对应一个 ECL 脚本
.ecl 弹幕脚本 (114 个)
        ↓
实体布局 (每场景 5..88 个)
```

**待验证**：`.std` 的 `slot_id` 是否就是 111 项名表的索引。
如果是，就能把「场景 → 波次模板」的映射完全解出来，
这正好补上 `SYS-GAME-WAVE` 缺的「场景如何筛选模板池」。

---

## 4. 工具

```powershell
# 自检（6 项）
python th185_web/scripts/native/std.py --selftest

# 列出 9 个场景摘要
python th185_web/scripts/native/std.py --list

# 详细解析一个场景
python th185_web/scripts/native/std.py --scene world01t
```

自检 6 项：

```
[PASS] found 9 .std files (expect 9)
[PASS] every scene has a valid .anm name
[PASS] wave count matches the offset table for every scene
[PASS] wave slot ids vary (observed values: [0,1,2,3,4,5,6,7])
[PASS] parsed 379 entity entries across all scenes
[PASS] all entity coordinates are plausible (|v| < 8192)
```

---

## 5. 状态

| 项 | 状态 |
|---|---|
| 9 个 `.std` 全部解出 | ✅ |
| 头部结构 | ✅ |
| 波次偏移表 | ✅ 步长 60 |
| 波次项结构 | ✅ 60 字节 |
| 实体项结构 | ✅ 16 字节，379 个 |
| `+0x08` 字段语义 | ❌ 未确认 |
| `slot_id` 与 111 项名表的关系 | ❌ **关键待验证** |
| 波次项的 float 语义 | ⚠️ 推测为坐标/尺寸 |

**`SYS-GAME-STAGE` 保持 `in-progress`。**
结构解出来了，但字段语义和与波次表的关联还没确认。
