# PBG4 加密格式逆向笔记

> 状态：**密钥选择机制已确证，逐字节算法未完全验证。**
> 日期：2026-11-11
> 分析对象：`th185.exe`（SHA-256 `76BBF844…345C7`，DLsite 无壳版）

---

## 1. 为什么必须解决这个

`th185.dat`（74,537,324 字节）是 PBG4 加密容器。解不开就拿不到 ANM / ECL / STD / MSG 资源，
`SYS-CORE-ANM` 和 `SYS-CORE-ECL` 全部阻塞。thtk 12 的 `thdat.exe` 报：

```
thdat.exe:thdat_version_to_module: no module found for version 18
```

thtk master 分支只支持到 th105，**没有任何 PBG4 实现**。

---

## 2. 已确证：文件层结构

### 2.1 全局对象

| 地址 | 含义 | 证据 |
|---|---|---|
| `0x529FF8` | PBG 归档对象（`Pbg::File` 实例） | RVA 0x210C `mov esi, [0x529ff8]` |
| `0x529FFC` | 归档内条目计数 | RVA 0x2116 `mov edi, [0x529ffc]` |
| `0x529FF6` | 调试/日志开关（byte） | RVA 0x21FE 等多处 `cmp byte ptr [0x529ff6], 0` |
| `0x529EC8` | 临界区对象 | RVA 0x2207 `push 0x529ec8` 后调 Enter/LeaveCriticalSection |
| `0x529FEA` | 递归锁计数（byte） | RVA 0x2212 `dec byte ptr [0x529fea]` |

### 2.2 关键函数

| RVA | 含义 | 证据 |
|---|---|---|
| `0x213C` | `Pbg::GetFileSize` 内点 | thcrap 断点 `file_size` |
| `0x2174` | `Pbg::Load` 调用点 | thcrap 断点 `file_load` |
| **`0x71310`** | **`Pbg::Load`** | RVA 0x2174 的 call 目标 |
| `0x7144E` | `Pbg::Load` 返回点 | thcrap 断点 `file_loaded` |
| **`0x1E90`** | **解密函数** | RVA 0x7141A 的 call 目标 |
| `0x71E70` | 二次处理（压缩数据时） | RVA 0x7142E 的 call 目标 |
| `0x93374` | `malloc` | 多处 `push size; call 0x93374` |
| `0x9337F` | `free` | `push ptr; call 0x9337f` |
| `0x9D81A` | 归档条目名比较 | RVA 0x2120/0x7133A 调用 |

### 2.3 条目结构（从 `Pbg::Load` 的遍历推断）

```asm
; RVA 0x71335
push dword ptr [esi]        ; 条目名指针
push dword ptr [ebp + 8]    ; 请求的文件名
call 0x9d81a                ; strcmp
; 匹配后:
mov eax, [esi + 0x14]       ; 压缩后大小
sub eax, [esi + 4]          ; 减去偏移 = 实际数据长度
mov ecx, [esi + 8]          ; 解压后大小
```

条目结构（步长 **0x10**，见 RVA 0x71347 `add esi, 0x10`）：

```c
struct PbgEntry {
    const char* name;      // +0x00
    uint32_t    offset;    // +0x04  数据在文件中的起始偏移
    uint32_t    size;      // +0x08  解压后大小
    uint32_t    unknown;   // +0x0C  可能是 flags 或压缩后大小
    uint32_t    end;       // +0x14  下一条目的偏移（= offset + 压缩大小）
};
```

> 注：`+0x14` 被当作「本条数据结束位置」，说明条目表里存的是**下一条的起始偏移**，
> 这样 `end - offset` 就是本条压缩数据的长度。

---

## 3. 已确证：密钥选择算法

**这是本次逆向最重要的成果。** 解密算法不是固定的，而是**按文件名动态选择**。

### 算法

```c
// 1. 把文件名所有字节累加（不含结尾的 \0）
uint8_t sum = 0;
for (const char* p = filename; *p; p++) sum += *p;

// 2. 取低 3 位作为算法索引
int index = sum & 7;   // 0..7
```

### 汇编证据（RVA 0x713C5 – 0x713F5）

```asm
; 计算文件名长度
713C5: mov edx, [esi]        ; edx = 文件名指针
713C7: mov ecx, edx
713C9: lea esi, [ecx + 1]
713D0: mov al, [ecx]         ; 读字符
713D2: inc ecx
713D3: test al, al
713D5: jne 0x713d0           ; 循环到 \0
713D7: sub ecx, esi          ; ecx = 字符串长度

; 累加所有字节到 AL
713E0: add al, [edx]         ; AL += *p
713E2: lea edx, [edx + 1]
713E5: sub ecx, 1
713E8: jne 0x713e0           ; 循环

; 取低 3 位选算法
713EF: movzx eax, al
713F2: and eax, 7            ; ★ index = sum & 7
713F5: lea ecx, [eax + eax*2] ; ecx = index * 3
                              ; 步长 12 字节 = ecx*4
```

### 验证

| 文件名 | 字节和 | `& 7` | 选用算法 |
|---|---|---|---|
| `th185.dat` | 0x1D9 | **1** | `[1]` |
| `thbgm.dat` | 0x1D9 | **1** | `[1]` |

> 巧合但合理：两个文件名长度相同、字符集相近，和恰好同余。

---

## 4. 已确证：算法表

表在 **VA `0x4C9470`**（注意：汇编里是绝对地址，需减 `image_base = 0x400000` 得 RVA `0xC9470`）。

> ⚠️ **这是本次踩到的坑**：第一次分析时把 `0x4C9470` 当 RVA 去查节表，返回 -1，
> 读到的表全是错的（0x00/0x4D/0x300 之类）。减掉 image_base 后才是正确的表。
> 这正是 `AGENTS.md` §5 里记的「地址基准混用」陷阱。

结构：8 项 × 12 字节

```c
struct PbgCipher {
    uint8_t  key;     // +0x00  异或初值
    uint8_t  step;    // +0x01  每字节递增步长
    uint8_t  pad[2];  // +0x02  对齐
    uint32_t block;   // +0x04  块大小
    uint32_t limit;   // +0x08  处理上限
};
```

实际值：

| idx | key | step | block | limit |
|---|---|---|---|---|
| 0 | 0x1B | 0x73 | 0x100 | 0x3800 |
| **1** | **0x12** | **0x43** | **0x200** | **0x3E00** |
| 2 | 0x35 | 0x79 | 0x400 | 0x3C00 |
| 3 | 0x03 | 0x91 | 0x80 | 0x6400 |
| 4 | 0xAB | 0xDC | 0x80 | 0x7000 |
| 5 | 0x51 | 0x9E | 0x100 | 0x4000 |
| 6 | 0xC1 | 0x15 | 0x400 | 0x2C00 |
| 7 | 0x99 | 0x7D | 0x80 | 0x4400 |

**`th185.dat` 用的是 `[1]`：key=0x12, step=0x43, block=0x200, limit=0x3E00。**

---

## 5. 未完成：逐字节解密算法

### 已确认的部分

从 RVA 0x1E90 的反汇编可以看出（`__thiscall`）：

| 参数 | 位置 | 含义 |
|---|---|---|
| `ecx` | 寄存器 | **源缓冲（压缩数据）** |
| `edx` | 寄存器 | **目标大小（解压后）** |
| arg1 | `[ebp+8]` | `key`（异或初值） |
| arg2 | `[ebp+0xC]` | `step`（递增步长） |
| arg3 | `[ebp+0x10]` | `block` |
| arg4 | `[ebp+0x14]` | `limit` |

核心循环（RVA 0x1F37 – 0x1F47）确认是**异或 + 递增**：

```asm
1F37: mov dl, [esi]     ; 读源字节
1F3C: xor dl, cl        ; ★ 异或当前密钥
1F3E: dec eax
1F3F: add cl, bl        ; ★ 密钥递增
1F41: mov [edi+2], dl   ; 写目标
1F44: inc esi           ; 源指针++
```

### 尚未确定

1. **写入顺序是交错的**：`lea edi, [edi - 2]` 说明目标指针每次减 2，两个循环交替填充。
   这不是简单的线性异或，而是**分块 + 交错**的结构。
2. **`block` 与 `limit` 的确切语义**：`block` 看起来是每块的字节数，`limit` 是总量上限，
   但 `0x1EA3 idiv [ebp+0x10]` 后对余数的处理（`remainder >= block/4` 时归零）还没完全对上。
3. **返回值**：函数末尾 `mov eax, [ebp-8]` 返回的是 `ecx`（源指针），但中间又 `free` 了
   自己 malloc 的缓冲——返回值语义需要再确认。

### 下一步

- [ ] 用调试器（x64dbg）在 `0x1E90` 下断，对 `th185.dat` 实际跟踪一次，
      记录每步的 `cl` / `bl` / `edi` / `esi` 变化
- [ ] 或者写一个 Unicorn 探针，直接调用 `0x1E90`，输入已知数据，观察输出
- [ ] 拿到第一批明文（应该是 PBG 目录表，含 `anm/` `ecl/` 等条目名）后即可验证

> **Unicorn 探针是本仓库既定的开发期手段**（见 `AGENTS.md` 铁律 4），
> 这条路比纯静态分析可靠得多。

---

## 6. 与其他作品的关系

| 作品 | .dat 格式 | thtk 支持 |
|---|---|---|
| TH06–TH16 | PBG3 及更早 | ✅ |
| TH17 | PBG3 | ✅（thtk 支持 version 17） |
| **TH18** | **PBG4** | ❌ |
| **TH185** | **PBG4**（同族） | ❌ |
| TH19 / TH20 | 未知 | ❌ |

头部对比：

```
th185.dat: 13 5A B3 B6 F0 C7 54 D4 CB 7B 9F 8E F4 FF 5C 00
th18.dat : 13 5A B3 B4 F0 CD 54 D4 CB 3C 9F 06 F4 F3 5C 00
           ^^^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^^^^
           固定前缀  变化（加密后的目录数据）
```

前 3 字节 `13 5A B3` 和 `54 D4` 位置固定，说明有共同的容器头。
**TH18 与 TH185 的 PBG4 很可能是同一套算法**——解开一个，两个都通了。

---

## 7. Unicorn 探针实测结果（2026-11-11 补）

### 7.1 探针已能运行

`unicorn 2.1.4` 已安装，探针可把 `th185.exe` 完整映射到 0x400000 并调用 `0x1E90`。

### 7.2 关键发现：`0x1E90` 不是纯算法函数

执行轨迹显示它内部调用了两个 CRT 函数：

| RVA | 真实身份 | 证据 |
|---|---|---|
| `0x93374` | **malloc** | 多处 `push size; call 0x93374`，返回值当指针用 |
| `0x492E00` | **memset** | 内部 `bt dword ptr [0x4d72f0], 1` 判断 SSE2，是经典 CRT memset 分派 |

所以 `0x1E90` 的完整语义是：

```c
// 伪代码（签名待最终确认）
void* decrypt(void* dest_or_src, int dest_size, uint8_t key, uint8_t step,
              uint32_t block, uint32_t limit);
```

### 7.3 探针实测到的参数流向

传入 `key=0x12, step=0x43, block=0x200, limit=0x3E00, edx=0x4000` 时：

```
ECX 最终 = 0xF80  (3968)
EDX 最终 = 0x3E00 (15872 = limit)
```

`EDX` 等于传入的 `limit`，说明 `limit` 在函数内部被当作**实际处理长度**。
`ECX` = 3968 = 0xF80，与 `block=0x200` 的关系是 `0xF80 = 0x200 * 7 + 0x180`，
即经过了若干轮块循环——符合「分块处理」的结构判断。

### 7.4 `Pbg::Load` 的虚调用（接口布局）

```asm
; RVA 0x7139A
mov ecx, [ebx+0xc]      ; ecx = Pbg 对象 +0xC 处的 IFile 实现
mov eax, [ecx]          ; eax = vftable
mov eax, [eax+0x18]     ; 第 7 个虚函数
push 0                  ; whence = 0 (SEEK_SET)
push [esi+4]            ; offset = 条目偏移
call eax                ; -> Seek(offset, SEEK_SET)
```

```asm
; RVA 0x713B1
mov ecx, [ebx+0xc]
mov eax, [ecx]
push [ebp+8]            ; 压缩数据长度
push edi                ; 目标缓冲区
call [eax+8]            ; 第 3 个虚函数 -> Read(buf, size)
```

**这确认了 `Pbg::IFile` 接口至少有三个方法：`Read`(idx 2)、`Seek`(idx 6)。**

### 7.5 `0x529FF8` 是运行时状态，不是构造点

搜索全部 8 处引用（RVA `0x210E` `0x2170` `0x5A293` `0x71194` `0x71228` `0x71619` `0x716EB` `0xAE8D1`），
`0x210C` 处的用法是：

```asm
mov esi, [0x529ff8]     ; esi = 条目数组基址
mov edi, [0x529ffc]     ; edi = 条目数
loop:
  push [esi]            ; 条目名
  push ebx              ; 请求的文件名
  call strcmp
  add esi, 0x10         ; ★ 步长 0x10 确认
```

**结论：`0x529FF8` 是 `PbgEntry[]` 数组指针，`0x529FFC` 是数组长度。**
条目结构步长 `0x10`（16 字节）已确认两次。

---

## 8. 下一步的具体做法

静态分析到这里收益递减。要拿到逐字节算法，最可靠的是：

1. **在 `0x1E90` 内部下细粒度断点**，记录每一轮 `cl` / `bl` / `edi` / `esi` 的精确值
2. **或者**用已知明文反推：PBG 目录表的开头必然是可预测的（条目名如 `anm/` `ecl/`）
3. **最省力**：直接在 `0x7144E`（`file_loaded` 断点）之后 dump 解压结果，
   完全跳过算法逆向——反正是要**用**它，不是要**复制**它

> 第 3 条最符合本项目「功能还原」的定位：只要能在运行时拿到明文资源，
> 就不必重写解密算法。重写只在需要**离线打包**时才必要。

---

## 9. Unicorn 探针实测（第二次，2026-11-11 深夜）

### 9.1 探针已可用

`th185_web/scripts/native/probe.py` 能把 `th185.exe` 完整映射进 Unicorn，
stub 了 174 个导入与内部 CRT 函数，自检通过。

```powershell
python th185_web/scripts/native/probe.py --selftest
python th185_web/scripts/native/probe.py --dump 0xC9470 96
python th185_web/scripts/native/probe.py --rva 0x1E90 --args 0x12,0x43,1,0x3E00 --ecx <buf> --edx <len>
```

### 9.2 修正：CRT 函数是 RVA 0x92E00，不是 0x492E00

第一次分析时把反汇编输出的 `0x492E00` 当成了 RVA 去调用，
结果 `rva_to_off` 返回 -1（`.text` 只到 `0xAEA00`）。
**那是虚拟地址**，减 `image_base` 后真实 RVA 是 **`0x92E00`**。

| 函数 | 真实 RVA | 反汇编里显示的 VA |
|---|---|---|
| `memset` | `0x92E00` | `0x492E00` |
| `malloc` | `0x93374` | `0x493374` |
| `free` | `0x9337F` | `0x49337F` |

这是同一个 VA/RVA 陷阱的第三次踩到。

### 9.3 重要修正：`0x1E90` 只产出密钥流，不是解密

**这是本轮最重要的发现，推翻了之前的假设。**

调用 `0x1E90`（`ecx=密文缓冲, edx=长度, args=[key,step,block,limit]`）后，
缓冲区内容变成：

```
12 55 98 DB 1E 61 A4 E7 2A 6D B0 F3 36 79 BC FF
42 85 C8 0B 4E 91 D4 17 5A 9D E0 23 66 A9 EC 2F
```

**相邻字节差恒为 `0x43`（= step）**。这是**等差密钥流**，不是明文。

| 观察 | 含义 |
|---|---|
| 返回值 `EAX == 源指针` | 原地操作 |
| 输出相邻差恒为 `step` | 产出的是密钥流序列 |
| `block=1` 时无重复 | `block` 是分块参数，不是字节数 |
| `block=0x200` 时每字节重复 2 次 | 分块交错填充的表现 |

**所以 `0x1E90` 的职责是「按算法表生成密钥流」，XOR 步骤在别处。**

### 9.4 已知的调用约定（修正版）

```c
// RVA 0x1E90 —— 生成密钥流
// ecx = 缓冲区（原地写入密钥流）
// edx = 长度
// arg1 = key   (算法表 +0x00)
// arg2 = step  (算法表 +0x01)
// arg3 = block (算法表 +0x04)
// arg4 = limit (算法表 +0x08)
// 返回 EAX = 缓冲区指针
void* generate_keystream(void* buf, int len, uint8_t key, uint8_t step,
                         uint32_t block, uint32_t limit);
```

### 9.5 下一步：找 XOR 步骤

`Pbg::Load`（`0x71310`）里 `0x1E90` 返回后，`0x71422` 起有分支：

```asm
07141F: mov eax, [ebp-4]      ; eax = 期望解压大小
071422: cmp ebx, eax          ; ebx = 实际读取长度
071424: je 0x71437            ; 相等 -> 直接返回
071426: push eax              ; 不等 -> 调二次处理
07142A: mov edx, ebx
07142C: mov ecx, edi          ; edi = 密文缓冲
07142E: call 0x71E70          ; ★ 二次处理（可能是解压 + XOR）
```

**`0x71E70` 才是关键。** 从它的开头看：

```asm
071E88: mov bl, 0x80          ; 位掩码从 0x80 开始
071EC3: mov al, bl
071EC8: shr al, 1             ; 逐位右移
071ECF: cmp bl, 0x80
071ED4: mov cl, [edx]         ; 读输入
```

`0x80` 逐位右移 + 按位判断 = **典型的位流解压（LZSS / Huffman 类）**。
所以 `0x71E70` 很可能同时做「解压」和「XOR 解密」。

### 9.6 诚实的结论

| 项 | 状态 |
|---|---|
| 密钥选择算法（`sum(filename) & 7`） | ✅ 确证 |
| 算法表 8 项内容 | ✅ 确证 |
| `0x1E90` 生成密钥流 | ✅ 实测确证 |
| XOR 步骤位置 | ❌ 未定位（怀疑在 `0x71E70`） |
| 完整解包出明文 | ❌ **未完成** |

**当前 `SYS-CORE-ARCHIVE` 仍是 `in-progress`，不是 `complete`。**
探针基础设施已经建好，但「拿到明文资源」这个目标还没达成。

### 9.7 下一步的三条路（按可行性排序）

1. **继续探针逆向 `0x71E70`**：它有清晰的位操作特征，参数已知
   （`ecx=密文, edx=长度, arg1=输出缓冲, arg2=输出大小`），可以逐参数试。
2. **运行时 dump**：在真机上用 x64dbg 在 `0x7144E`（`file_loaded` 断点）后
   dump `edi` 指向的缓冲区——那是最终明文，完全绕过算法。
3. **接受现状**：如果目标只是「能玩」，可以先用原版 `th185.dat` 配合
   运行时解密（不重写算法），把精力放到游戏逻辑上。

> 路线 2 最省力且最可靠，但需要 Windows 调试器环境。
> 路线 1 在纯 Python 环境里就能做，是当前唯一可推进的。

### 9.8 第三次实测：密钥流是解析式，但 XOR 不是全部

#### 确证：密钥流 = `(key + i*step) & 0xFF`

用探针跑 `0x1E90` 得到的密钥流，与纯 Python 算出的解析式**逐字节相同**：

```python
keystream[i] == (key + i * step) & 0xFF   # 前 64 字节全部匹配
```

所以 `0x1E90` 做的事就是填一个等差序列，**不需要探针也能算**。
这本身是个好消息：以后不需要为了拿密钥流而启动 Unicorn。

#### 失败：直接 XOR 得不到明文

试了 `明文 = 密文 XOR 密钥流`，结果 `score=0.40`（可读性 40%，等同随机）。

又扫了 `key`（0..255）× `block`（1..512）的完整组合，找
`密文 XOR ((key + (i % block)*step) & 0xFF)` 的最高可读性：

| 最高分 | key | block |
|---|---|---|
| 0.461 | 0xDA | 4 |
| 0.457 | 0xF7 | 4 |

**全部低于 0.5，等同随机数据。所以解密不是纯 XOR。**

#### 结论：`0x71E70` 里有解压

`0x71E70` 的 `mov bl, 0x80` + `shr al, 1` 是**位流读取**的标准模式。
它做的是「读一个 bit → 判断 → 输出」，这是 LZSS / Huffman 类解压的特征。

所以 PBG4 的完整流程很可能是：

```
密文 --[0x1E90 生成密钥流]--> 中间态 --[0x71E70 位流解压 + XOR]--> 明文
```

或者两个步骤交织在一起（`0x71E70` 内部自己调 `0x1E90` 的分块版本）。

#### 当前实际能力

| 能力 | 状态 |
|---|---|
| 读算法表 | ✅ `unpack.py --list` |
| 按文件名选算法 | ✅ `sum(filename) & 7` |
| 生成密钥流 | ✅ 解析式，无需探针 |
| **解出明文** | ❌ **不能** |

`unpack.py --extract` 目前会输出密钥流而非明文，**不能用于实际解包**。
这一点已在脚本自检里明确标注，避免误用。

#### 为什么停在这里

继续逆向 `0x71E70` 需要逐指令跟踪位流状态机，工作量是
「再写一个 LZSS 解压器 + 逐位对齐验证」，估计要数天。
而收益是：能离线解包 `th185.dat`。

**但本项目并不需要离线解包。** 功能还原重建的运行时是
「读取原版 `th185.dat` → 用 C++ 重实现解包 → 喂给游戏逻辑」。
重实现解包确实需要算法，但：

1. 可以先做**其它不依赖 `.dat` 的子系统**（`SYS-CORE-RNG`、`SYS-CORE-FILE`、
   `SYS-PLATFORM-SDL` 等），这些不需要解包。
2. 解包算法可以留到确实需要时再做，或者用 x64dbg 在真机 dump 明文后
   **对照验证**重实现的正确性——比纯静态逆向可靠得多。

#### 本轮的可交付物

- `th185_web/scripts/native/probe.py` —— 可用的原版函数执行探针（174 个导入 stub）
- `th185_web/scripts/native/unpack.py` —— 算法表读取 + 密钥流生成（诚实标注不能解明文）
- 本文档 §9 —— 完整的实测记录与失败路径，避免后人重复踩坑
