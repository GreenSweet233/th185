# TH185 存档格式（scoreth185.dat）

> 状态：**格式完全解出，15 项自检通过，C++ 实现完成。**
> 日期：2026-11-12

---

## 1. 位置

```
%APPDATA%\ShanghaiAlice\th185\scoreth185.dat      20819 字节
%APPDATA%\ShanghaiAlice\th185\scoreth185bak.dat  同大小（备份）
%APPDATA%\ShanghaiAlice\th185\th185.cfg          136 字节
```

---

## 2. 格式

### 2.1 头部（24 字节）

```c
struct SaveHeader {
    char   magic[4];         // +0x00  "581T" (0x54313835)
    uint32 file_size;        // +0x04  文件大小的回写（20819）
    uint16 version;          // +0x08  6
    uint16 reserved;         // +0x0A  0
    uint32 constant;         // +0x0C  恒为 0x100
    uint32 compressed_size;  // +0x10  20795
    uint32 decompressed_size;// +0x14  158240
    // +0x18 起是加密 + LZSS 压缩的数据
};
```

### 2.2 ★ 复用 PBG4 的解密与 LZSS

这是本轮最省力的发现——**存档用的就是归档的同一套代码**，只是参数不同：

| 阶段 | 函数 | 参数 |
|---|---|---|
| 解密 | RVA `0x1E90`（同 PBG4） | `key=0xAC step=0x35 block=0x10 limit=压缩大小` |
| 解压 | RVA `0x71E70`（同 LZSS） | 目标 `decompressed_size` |

反汇编证据（RVA `0x69440`）：

```asm
069452: cmp dword ptr [eax], 0x54313835   ; "581T" 魔数
06945E: cmp word  ptr [eax + 8], 6        ; 版本 6
069469: mov edx, [eax + 0x10]             ; 压缩大小
06946C: lea ecx, [eax + 0x18]             ; 数据起点
069471: push edx                          ; limit = 压缩大小
069472: push 0x10                         ; block
069474: push 0x35                         ; step
069476: push 0xac                         ; key
06947B: call 0x1e90                       ; ★ 同一个解密函数
0694A0: call 0x71e70                      ; ★ 同一个 LZSS
```

### 2.3 解压后：自描述记录序列

```c
struct SaveRecord {
    uint16 magic;    // "CR" = 0x5243 或 "ST" = 0x5453
    uint16 version;  // CR=3, ST=4
    uint32 value;    // CR: 分数类值; ST: 未用
    uint32 length;   // 总长度（含本头）
};
```

**实测一条真实存档**：

| # | magic | version | value | length |
|---|---|---|---|---|
| 0 | `CR` | 3 | 45532 | 78176 |
| 1 | `CR` | 3 | 42531 | 78176 |
| 2 | `ST` | 4 | 17334 | 1888 |

`78176 + 78176 + 1888 = 158240`，**精确等于声明的解压大小**——
记录遍历自校验，这是格式正确的最强证据。

### 2.4 CR 记录内部：32 字节槽位

```c
struct SaveSlot {          // 32 字节，从 CR 记录 +0x10 起
    uint32 score;          // 分数
    uint8  flag_a;         // ★ 槽位类型：1=空榜, 7=实绩
    uint8  flag_b;         // 子字段（0 或 1）
    char   name[8];        // "--------" 表示空
    uint8  extra[20];      // 观察值全零
};
```

### 2.5 ★ 一个差点搞错的细节：`flag` 不是 u16

我最初把 `+0x04` 当成 `uint16 flag`，并加了 `flag > 1` 的合法性检查。
结果**第 2 条 CR 记录解析出 0 个槽位**。

对比两条记录：

```
第 1 条  A0 86 01 00 | 01 00 | 2D2D...   ← flag_u16 = 1
第 2 条  A0 86 01 00 | 01 01 | 2D2D...   ← flag_u16 = 257  ✗ 被当成无效
```

**它其实是两个独立的 u8 字段**，`01 00` 和 `01 01` 都合法。

### 2.6 槽位类型

| `flag_a` | 含义 | 特征 |
|---|---|---|
| **1** | 空榜条目 | 名字 `"--------"`，分数递减 |
| **7** | 真实成绩 | 名字 `"        "`（8 个空格） |

空榜条目**按 10 个一组**（6 组 = 60 个），组内分数递减：

```
组 0: 100000, 90000, 80000, 70000, 60000, 50000, 40000, 30000, 20000, 10000
组 1: 100000, 90000, ...（重新开始）
...
组 5: 100000, ..., 10000
```

**6 组对应 6 个排行榜**（TH185 有 8 档难度，排行榜只记录部分）。

实测还有 2 个实绩槽：`score=10001` 和 `score=9618`。

---

## 3. 验证（15 项）

```
[PASS] magic is "581T"
[PASS] version is 6
[PASS] header echoes the file size (20819)
[PASS] 24 + compressed size == file size
[PASS] +0x0C is the constant 0x100
[PASS] LZSS produced exactly 158240 bytes
[PASS] records consume the whole buffer (158240 / 158240 bytes)
[PASS] every record has a known magic (['CR', 'CR', 'ST'])
[PASS] found 2 CR record(s)
[PASS] first CR record has 62 slots
[PASS] 60 empty slots form 6 groups of 10
[PASS] empty-slot scores decrease within each group
[PASS] every empty slot is named '--------'
[PASS] every slot name is 8 characters
[PASS] found 1 ST record(s)
```

---

## 4. 工具

```powershell
# 自检（15 项）
python th185_web/scripts/native/save.py --selftest

# 转储结构
python th185_web/scripts/native/save.py --dump

# 指定文件
python th185_web/scripts/native/save.py --file <path> --dump
```

C++ 实现：`cpp/game/SaveFile.hpp` / `.cpp`

---

## 5. 未解项

| 项 | 状态 |
|---|---|
| 头部与记录结构 | ✅ 完全解出 |
| 解密与解压 | ✅ 复用 PBG4，字节级验证 |
| 槽位结构 | ✅ 32 字节，字段已识别 |
| **`ST` 记录的设置字段语义** | ❌ 未解（1888 字节） |
| **CR 记录 `+0x04` 的 value 含义** | ⚠️ 疑似总分数或时间 |
| **`flag_b` 的确切含义** | ⚠️ 只观察到 0 和 1 |
| `th185.cfg`（136 字节） | ❌ 未解 |

**`SYS-SAVE-SCORE` 保持 `in-progress`。**
读写链路完全打通，但部分字段语义待确认。
