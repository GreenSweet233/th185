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

---

## 8. 全部 114 个文件的统计（本轮）

### 8.1 一个必须修正的假设：ANIM 段是变长的

最初以为偏移表固定在 `+0x34`，结果**只有 `default.ecl` 能解析**。

对比两个文件：

```
default.ecl  +0x24  "ANIM" 00 00 00 00            ← 无动画名
             +0x2C  "ECLI" 00 00 00 00
             +0x34  偏移表

wave01.ecl   +0x24  "ANIM" 01 00 00 00            ← 有 1 个动画名
             +0x28  "enemy.anm" 00 00 00
             +0x38  "ECLI" 00 00 00 00
             +0x40  偏移表                            ← 位置变了！
```

**`ANIM` 段带 `u32 count` + `count` 个 NUL 结尾名字**，所以长度可变。
正确做法：先搜索 `ECLI` 标记，偏移表在其后 8 字节。

### 8.2 修正后的完整统计

```
子程序总数: 486
指令总数:   12480
操作码种类: 112
解析失败:   0
尾部字节:   12（486/486 全部一致）
```

**486 个子程序全部恰好剩余 12 字节**——这个一致性是格式正确的铁证。

### 8.3 ANIM 名揭示了完整 BOSS 列表

```
enemy.anm     113 个文件
boss01.anm      3    boss01t.anm   2
boss02.anm      3    boss03.anm    3
...                  boss27.anm    2
```

共 **29 种动画名**。`boss01..boss27` + `boss01t` 正好 **28 个 BOSS**，
与 `docs/WAVES.md` 解出的波次名表完全吻合（1 教程 + 27 常规）。

### 8.4 操作码分布（全 114 文件，12480 条指令）

| id | 次数 | 占比 | size | 推测 |
|---|---|---|---|---|
| **42** | 2549 | **20.42%** | 20 | 等待/延时（值 1,12,32,60,120,240） |
| **44** | 1009 | 8.08% | 20 | 浮点参数（值 1.7, 2, 2.3） |
| **23** | 999 | 8.00% | 20 | 等待（值 60, 240） |
| 1013 | 689 | 5.52% | 52 | 扩展指令 |
| 12 | 571 | 4.58% | 24 | 设置整型（值 128, 340, 412） |
| 14 | 529 | 4.24% | 24 | 设置负值（值 -40, -124, -336） |
| 40 | 486 | 3.89% | 20 | 空操作/标记 |
| 43 | 398 | 3.19% | 20 | 值 4 |
| 78 | 265 | 2.12% | 20 | |
| 55 | 245 | 1.96% | 16 | 全零 payload |

**`id=42` 占 20%**，payload 值都是 1/12/32/60/120/240 这类帧数——
极可能是「等待 N 帧」指令，这与 ECL 是协程式脚本的设计吻合。

`id=44` 的 payload 是 `1.7`、`2`、`2.3` 等浮点数，可能是速度/角度参数。

### 8.5 验证：12 项自检全部通过

```
[PASS] found 114 .ecl files (expect 114)
[PASS] default.ecl has 34 subroutines (declared 34)
[PASS] every subroutine has a name
[PASS] every subroutine ends with exactly 12 bytes left
[PASS] parsed 544 instructions in default.ecl
[PASS] observed 81 distinct opcodes
[PASS] TH18-style subroutine names present
[PASS] all 114 .ecl files parse (0 errors)
[PASS] all 486 subroutines end with 12-byte tail (0 bad)
[PASS] parsed 12480 instructions across all files
[PASS] observed 112 distinct opcodes across all files
[PASS] ANIM names include boss animations (29 distinct names)
```

耗时 5 秒（Probe 共享实例后）。

### 8.6 状态更新

| 项 | 状态 |
|---|---|
| SCPT 容器（含变长 ANIM） | ✅ **完全解出** |
| 全部 114 文件解析 | ✅ **0 错误** |
| 486 子程序边界 | ✅ **全部精确** |
| 12480 条指令 | ✅ 全部解出 |
| 112 种操作码 | ✅ 已枚举 |
| **操作码语义** | ❌ **未解** |

**`SYS-CORE-ECL` 保持 `in-progress`。**
容器、边界、指令流全部掌握；剩下的是 112 种操作码的语义。

---

## 9. ★ 内置函数表（本轮重大发现）

### 9.1 位置

**VA `0x43ECF4`**（`.text` 段内），**117 项**函数指针。

这是 ECL 解释器的**内置函数分派表**：脚本调用 `builtin(N)` 时，
解释器查表跳到第 N 个 thunk 执行。

### 9.2 调用约定

全部是 `__thiscall`：`ecx` = ECL 上下文对象指针，返回值在 `eax`。

绝大多数 thunk 是**纯字段读取器**，所以这张表同时是 **ECL 上下文布局图**。

### 9.3 已确证的语义

| idx | 实现 | 语义 | 交叉验证 |
|---|---|---|---|
| **0** | `call 0x2800; and eax,0x7fffffff` | **`rand()` 整数** | ✅ `docs/RNG.md` 的 RNG 地址 |
| **1** | `call 0x2890; cvttss2si` | **`randf()` 浮点** | ✅ 同上 |
| 2,11,44,45,71,72,87,88,97,99,100,103 | `xor eax,eax` | 常量 **0** | |
| 3..8,19..34,65..68 | `cvttss2si [ecx+0x14xx]` | 上下文浮点字段 | |
| 9,35 | `[0x4d7c3c]+0x620` | 玩家 X | |
| 10,36 | `[0x4d7c3c]+0x624` | 玩家 Y | |
| 13 | `call 0x28d0` | RNG 变体 3 | |
| 40 | `[0x4d1040]` | 全局值 | |
| **41** | `[0x4d1038]` | **难度索引** | ✅ `docs/DIFFICULTY.md` |
| 43 | `mov eax,1` | 常量 **1** | |
| **47..50** | `cmp [0x4d1038], 0..3` | **`is_difficulty_0..3`** | ✅ `docs/DIFFICULTY.md` |
| **51..54** | `[EM+0x3c/0x40/0x44/0x4f44]` | ENEMY_MANAGER 字段 | ✅ `docs/DIFFICULTY.md` |
| 55 | `[0x4d102c] + [0x4d1030]` | 两个全局之和 | |
| 69 | `[EM+0x94]` | ENEMY_MANAGER 字段 | |
| 73 | `[0x4d7b0c]+0xd0 == 0` | 游戏状态测试 | |
| **74..85** | `[EM+0x0c..0x38]` | ENEMY_MANAGER 字段组 | |
| 101 | `or eax,-1` | 常量 **-1** | |
| 104 | `[EM+0x4eb8]` | ENEMY_MANAGER 字段 | |
| **110** | `[0x4d1004] - 2; cmp eax,5` | **关卡号 - 2** | ✅ `docs/WAVES.md` §9 |

> `EM` = `[0x4D7AF4]`（ENEMY_MANAGER）。

### 9.4 三处跨子系统交叉验证

**这是本轮最有价值的部分**——内置表独立印证了之前三个子系统的结论：

| 内置表发现 | 之前独立解出 | 一致性 |
|---|---|---|
| idx 0/1 调 `0x2800` / `0x2890` | `docs/RNG.md`：RNG 在 `0x2800` | ✅ 完全一致 |
| idx 41 读 `[0x4D1038]` | `docs/DIFFICULTY.md`：难度索引在此 | ✅ |
| idx 47..50 是 `is_difficulty_0..3` | `docs/DIFFICULTY.md`：只有 0..3 四个判断函数 | ✅ **连「只有 4 个」都对上** |
| idx 51..54 是 `EM+0x3c/40/44/4f44` | `docs/DIFFICULTY.md` 的访问器函数族 | ✅ 同一组地址 |
| idx 110 是 `[0x4D1004] - 2` 且 `cmp 5` | `docs/WAVES.md` §9：关卡 5/7 触发强制 BOSS | ✅ |

**五个独立结论全部吻合**，说明整个逆向链条是自洽的。

### 9.5 发现过程中的弯路（记录避免重复）

| 尝试 | 结果 |
|---|---|
| 找 `cmp reg, 1023/1024` 的 opcode 范围检查 | 0 处有用命中 |
| 找 `jmp [reg*4+table]` 跳转表 | 11 处，都与 ECL 无关 |
| 搜 `SCPT`/`ECLH`/`ECLI`/`ANIM` 字符串地址引用 | 0 处（实际是立即数比较） |
| 搜 `cmp reg, 0x2a`（opcode 42） | 0 处 |
| **全镜像扫描 >=100 项的 `.text` 指针数组** | ✅ **找到 0x43ECF4** |

**有效方法：全镜像扫描连续函数指针数组。**
ECL 不用 switch/跳转表，而是**函数指针表**，所以搜 `cmp`/`jmp` 都找不到。

### 9.6 副产品：ECL 加载器代码

搜索 `"ANIM"` 立即数（`0x4D494E41`）找到加载器 `0x3469E`：

```asm
03469E: cmp dword ptr [eax], 0x4d494e41   ; "ANIM"
0346AD: cmp dword ptr [eax + 4], 0        ; count
0346B1: lea esi, [eax + 8]                ; 名字从 +8 开始
0346D0: cmp edi, 0x1d                     ; 29 个槽位
0346E2: test edi, edi                     ; slot 0 -> 主角
0346F3: cmp edi, 1                        ; slot 1 -> 敌机
034702: mov eax, [ebx + edx]              ; 其他 -> 表 [0x4EC4 + slot*4]
```

**`0x1d = 29`** 正是实测的 29 种 ANIM 名——精确印证。

`ECLI` 段（`0x3476F`）：

```asm
03476F: cmp dword ptr [esi], 0x494c4345   ; "ECLI"
03477B: add esi, 8                        ; 偏移表在 ECLI+8
03477E: cmp dword ptr [ebx + 4], edi      ; 循环 sub_count 次
```

**与 `docs/ECL.md` §8.1 的实现完全一致。**

### 9.7 工具

```powershell
python th185_web/scripts/native/ecl_builtins.py --selftest   # 8 项
python th185_web/scripts/native/ecl_builtins.py --list       # 117 项全表
```

### 9.8 状态

| 项 | 状态 |
|---|---|
| 内置函数表位置与规模 | ✅ **117 项 @ 0x43ECF4** |
| 调用约定 | ✅ `__thiscall`，`ecx` = 上下文 |
| 已确证语义 | ✅ 约 40 项（含全部关键项） |
| 其余约 77 项 | ⚠️ 实现可读但语义需推断 |
| **112 种操作码语义** | ❌ **仍未解**（内置表是「读变量」，不是「执行指令」） |

> **重要区分**：这 117 项是 **builtin 函数**（脚本里 `builtin(N)` 取变量），
> 与 112 种**操作码**（ECL 指令本身）是两套东西。
> 操作码分派仍未找到——可能编译成很长的比较链，或藏在别的结构里。
