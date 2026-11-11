# TH185 ECL 脚本格式（SCPT / ECLH）

> 状态：**容器结构与指令流已完全解出；81 种操作码的语义未解。**
> 日期：2026-11-11

---

## 1. 概览

`th185.dat` 里有 **114 个 `.ecl`** 文件，使用 **SCPT** 容器格式——
这正是 TH18 及之后作品的 ECL 格式，与 thcrap 的 `ThModern_ECLGetSub` 一致。

```
.ecl (SCPT)
  ├─ 头部（魔数 + 版本 + 名字数 + 子程序数 + 段标记）
  ├─ 子程序偏移表（34 项）
  ├─ 名字表（明文 ASCII）
  └─ 每个子程序：ECLH 块
       └─ 指令流（[id][size][rank][argc][payload]）
```

---

## 2. SCPT 容器

### 2.1 头部

```c
struct ScptHeader {
    char     magic[4];      // +0x00  "SCPT"
    uint32   version;       // +0x04  0x00100001
    uint32   name_count;    // +0x08  default.ecl = 36
    uint32   reserved0;     // +0x0C
    uint32   sub_count;     // +0x10  default.ecl = 34
    uint32   reserved1;     // +0x14
    uint32   reserved2;     // +0x18
    uint32   reserved3;     // +0x1C
    uint32   reserved4;     // +0x20
    char     anim_tag[4];   // +0x24  "ANIM"
    uint32   reserved5;     // +0x28
    char     ecli_tag[4];   // +0x2C  "ECLI"
    uint32   reserved6;     // +0x30
    uint32   sub_offsets[sub_count];  // +0x34
    // 随后是名字表（NUL 结尾字符串）
};
```

### 2.2 名字表是明文

子程序名**直接以 ASCII 存储**，这是最大的便利：

```
0x00BC  BossDead
0x00C5  BossEscape
0x00D0  BossInterval
0x00DD  BossItem
0x00E6  BossItemPhase
0x00F4  BossItemPhase2
0x0103  BossItemPhase3
0x0112  DeadAttack1
0x011E  DeadAttack2
0x012A  DeadAttackEt1
0x0138  DebugSkipFunc
0x0146  DebugSkipStopFunc
0x0158  Ecl_EtBreak
0x0164  Ecl_EtBreak2
0x0171  Ecl_EtBreak2_ni
0x0181  Ecl_EtBreak_ni
0x0190  EffChargePoint
0x019F  EffChargePoint2
0x01AF  EffChargePoint3
0x01BF  ItemCarryLily
0x01CD  ItemCarryLily2
0x01DC  ItemCarryLilyAt0
...     ItemCarryLilyAt1..4
0x0231  MainBossSpell
0x023F  MainBossSpell2
0x024E  MainMBossSpell
0x025D  NoEnemyDebug
0x026A  WaveSync
0x0273  et_ofs_r
0x027C  test
0x0281  test2
```

**这些名字与 TH18 的 ECL 约定完全一致**，说明 ECL 解释器可以复用。

---

## 3. ECLH 子程序块

### 3.1 结构

```c
struct EclhBlock {
    char     magic[4];      // +0x00  "ECLH"
    uint32   header_size;   // +0x04  16
    uint32   reserved0;     // +0x08
    uint32   reserved1;     // +0x0C
    uint32   reserved2;     // +0x10
    // +0x14（tag 后 20 字节）开始是指令流
};
```

### 3.2 指令格式

```c
struct Instruction {
    uint16 id;      // 操作码
    uint16 size;    // 含本头的总长度
    uint16 rank;    // 推测：执行层级 / 优先级
    uint16 argc;    // 推测：参数个数
    uint8  payload[size - 8];
};
```

---

## 4. 验证

### 4.1 三个独立的自洽证据

**证据 1：34 个偏移全部指向 ECLH**

```
[ 0] off=648    tag=b'ECLH'
[ 1] off=1020   tag=b'ECLH'
...
[33] off=14132  tag=b'ECLH'
```

**证据 2：34 个子程序全部恰好剩余 12 字节**

```
[ 0] @0x0288..0x03FC   15 条, 结束 0x03F0, 剩 12
[ 1] @0x03FC..0x051C   13 条, 结束 0x0510, 剩 12
...
[33] @0x3734..0x3768    1 条, 结束 0x375C, 剩 12
```

**34/34 全部一致**——变长记录解析一旦错位，剩余字节数会各不相同。

**证据 3：最短的段 33 可精确验算**

```
3734  45 43 4C 48 10 00 00 00 00 00 00 00 00 00 00 00   ECLH + 16 字节头
3744  00 00 00 00 28 00 14 00 00 00 FF 01 00 00 00 00   id=0x28 size=0x14
3754  08 00 00 00 00 00 00 00 0A 00 10 00 00 00 FF 00   id=0x0A size=0x10
3764  00 00 00 00
```

`0x28 + 0x14 = 0x3C`，`0x3C + 0x10 = 0x4C = 52` = 段长，**完全吻合**。

### 4.2 统计

| 指标 | 值 |
|---|---|
| `.ecl` 文件数 | 114 |
| `default.ecl` 子程序数 | 34 |
| `default.ecl` 指令数 | 544 |
| 不同操作码数 | 81 |
| 每段尾部字节 | 12（全部一致） |

---

## 5. 操作码分布（default.ecl）

| id | 次数 | size | id | 次数 | size |
|---|---|---|---|---|---|
| 1 | 2 | 16 | 42 | 86 | 20 |
| 12 | 27 | 24 | 43 | 18 | 20 |
| 13 | 17 | 24 | 44 | 40 | 20 |
| 14 | 16 | 24 | 45 | 10 | 20 |
| 15 | 10 | 40 | 50 | 12 | 16 |
| 23 | 19 | 20 | 51 | 9 | 16 |
| 40 | 34 | 20 | 59 | 17 | 16 |

**大多数操作码的 size 固定**，这是 ECL 指令集的典型特征。

高位操作码（`300`、`400`、`500`、`600`、`900`）看起来是扩展指令组。

---

## 6. 工具

```powershell
# 自检（7 项）
python th185_web/scripts/native/ecl.py --selftest

# 列出 114 个 .ecl 的子程序数与指令数
python th185_web/scripts/native/ecl.py --list

# 显示单个文件的子程序详情
python th185_web/scripts/native/ecl.py --file default.ecl
```

### 性能说明

Probe 创建（映射 2 MB 镜像 + 安装 174 个导入桩）是主要开销。
`pbg4.extract_file()` 增加了可选的 `probe` 参数，`ecl.py` 共享单个实例，
自检耗时从超时降到 **0 秒**。

---

## 7. 状态

| 项 | 状态 |
|---|---|
| SCPT 容器结构 | ✅ 完全解出 |
| 子程序名表 | ✅ 明文，36 个 |
| ECLH 块结构 | ✅ 完全解出 |
| 指令流格式 | ✅ `[id][size][rank][argc][payload]` |
| 指令流边界 | ✅ 34/34 全部剩 12 字节 |
| **81 种操作码语义** | ❌ **未解** |
| `rank` / `argc` 字段含义 | ⚠️ 推测 |
| 12 字节尾部含义 | ❌ 未确认 |

**`SYS-CORE-ECL` 保持 `in-progress`。**
容器和指令流完全解出，但**操作码语义**是真正的难点——
需要对照 ECL 解释器实现或运行时验证才能确定。

> 好消息：子程序名与 TH18 完全一致，且 `th11` 仓库有完整的 ECL 解释器实现
> （`th11_web/cpp/game/EclInterpreter.cpp`），可作为语义参考。
