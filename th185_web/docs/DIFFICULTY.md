# TH185 难度系统

> 状态：**8 档难度索引、分数上限表、访问器函数族已解出；数值倍率未解。**
> 日期：2026-11-11

---

## 1. 难度档位（8 档）

难度索引在 **`0x4D1038`**（uint32，值 0..7）。

| 索引 | 名称 | 备注 |
|---|---|---|
| 0 | Very Easy | |
| 1 | Easy | |
| 2 | Normal | |
| 3 | **Little Hard** | **TH185 独有，TH18 没有** |
| 4 | Hard | |
| 5 | Very Hard | |
| 6 | Lunatic | |
| 7 | Over Drive | 最高难度 |

证据：`th185.exe` 字符串实测命中全部 8 个名称；
thprac 的 `difficulties[8]` 数组完全一致。

---

## 2. 分数上限表（5 项）

### 2.1 初始化代码

```asm
03107C: movaps xmm0, xmmword ptr [0x4bcb00]   ; 读 16 字节常量
031083: movups xmmword ptr [ebp - 0x18], xmm0  ; 存到栈上局部数组
031087: mov dword ptr [ebp - 8], 0xf4240       ; 第 5 项 = 1000000
03109A: mov eax, dword ptr [0x4d1038]          ; 难度索引
0310A1: mov eax, dword ptr [ebp + eax*4 - 0x18]  ; ★ 查表
0310A5: imul eax, dword ptr [0x4d1004]         ; × 关卡号
0310AC: mov dword ptr [edi + 0x7c], eax        ; 存入对象 +0x7C
0310AF: mov dword ptr [edi + 0x80], eax        ; 同时存 +0x80
```

### 2.2 表内容

常量 `VA 0x4BCB00` 的前 16 字节：

```
20 A1 07 00 | 40 42 0F 00 | 60 E3 16 00 | 80 84 1E 00
  = 500000  |  = 1000000  |  = 1500000  |  = 2000000
```

加上 `0x31087` 的立即数 `0xF4240`：

| 索引 | 值 |
|---|---|
| 0 | 500,000 |
| 1 | 1,000,000 |
| 2 | 1,500,000 |
| 3 | 2,000,000 |
| 4 | 1,000,000 |

### 2.3 一个必须说清楚的问题

**这张表只有 5 项，但难度有 8 档。**

代码用 `[ebp + eax*4 - 0x18]` 查表，`eax` 是难度索引 0..7。
索引 5/6/7 会读到 `[ebp-8]` 之后的栈上相邻数据——
**这是原版的越界读，不是我的误读。**

结合下一节的发现（只有 `is_difficulty_0..3` 四个判断函数），
最合理的解释是：**这个函数只在难度 0..4 的场合被调用**，
5..7 走另一条路径。

---

## 3. 全局访问器函数族

RVA `0x3E735` – `0x3E7C5` 是一串极短的 getter，每个 3-4 条指令：

| RVA | 行为 | 等价 C |
|---|---|---|
| `0x3E735` | `mov eax, [0x4d1040]` | `return g_4d1040;` |
| **`0x3E740`** | `mov eax, [0x4d1038]` | **`return difficulty;`** |
| `0x3E74B` | `mov eax, [ecx+0x6224]` | `return self->field_6224;` |
| `0x3E757` | `cmp [0x4d1038],0; sete al` | `return difficulty == 0;` |
| `0x3E768` | `cmp [0x4d1038],1; sete al` | `return difficulty == 1;` |
| `0x3E77A` | `cmp [0x4d1038],2; sete al` | `return difficulty == 2;` |
| `0x3E78C` | `cmp [0x4d1038],3; sete al` | `return difficulty == 3;` |
| `0x3E79E` | `mov eax, [EM+0x3c]` | `return em->field_3c;` |
| `0x3E7AC` | `mov eax, [EM+0x40]` | `return em->field_40;` |
| `0x3E7BA` | `mov eax, [EM+0x44]` | `return em->field_44;` |
| `0x3E7C8` | `mov eax, [EM+0x4f44]` | `return em->field_4f44;` |

> `EM` = `[0x4D7AF4]`（ENEMY_MANAGER）。

### 3.1 关键观察：只有 4 个难度判断

**没有 `is_difficulty_4..7`。**

这说明游戏逻辑里只对**前 4 档**（Very Easy / Easy / Normal / Little Hard）
做了特殊分支，Hard 及以上走通用路径。

这也解释了 §2.3 的疑问：分数上限表只需要 5 项就够用了。

---

## 4. 与其他作品的对照

| 作品 | 难度档数 | 特殊档 |
|---|---|---|
| TH11 地灵殿 | 4 + Extra | — |
| TH18 虹龙洞 | 6 | — |
| **TH185 黑市** | **8** | **Little Hard（索引 3）** |

TH185 比 TH18 多 2 档，且第 4 档名称是 `Little Hard` 而非 `Hard`。

---

## 5. 工具

```powershell
python th185_web/scripts/native/difficulty.py --list
python th185_web/scripts/native/difficulty.py --selftest
```

自检 5 项：档位数 8、索引 3 = Little Hard、索引 7 = Over Drive、
判断函数只有 4 个、分数上限表内容。

---

## 6. 状态

| 项 | 状态 |
|---|---|
| 难度档位与索引 | ✅ 完全解出 |
| 分数上限表 | ✅ 完全解出（5 项） |
| 访问器函数族 | ✅ 完全解出（11 个） |
| 仅前 4 档有特殊分支 | ✅ 已确认 |
| **各档的弹速/密度倍率** | ❌ **未解**（核心数值） |
| 分数上限表的实际用途 | ⚠️ 推测为「本关分数上限基数」 |

**`SYS-GAME-DIFFICULTY` 保持 `in-progress`**——
结构解出来了，但**最关键的数值倍率（弹速、密度、判定）还没找到**。

> 下一步：搜 `.rdata` 里的 float 数组，找 8 元素且递增的浮点表，
> 那些很可能就是各档难度的倍率。
