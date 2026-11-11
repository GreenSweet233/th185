# TH185 还原工作流

> 本文件记录 TH185 功能还原重建的工程方法。
>
> **来源**：从同工作区的 `th095`（713 commits 的东方 source reconstruction 项目）
> 和 `th11`（15 commits 的 portable 重实现）的 **commit 历史**反推，不是照抄文档。

---

## 0. 与 th095 / th11 的根本差异

| | th095（matching decomp） | th11（portable 重实现） | **TH185（本项目）** |
|---|---|---|---|
| 目标 | 字节级一致 | 行为一致 | **行为一致** |
| 组织单位 | 函数地址（1831 个） | 子系统 | **子系统 + 行为断言** |
| 核心账本 | functions.csv + matches.csv | 无独立账本 | **subsystems.csv + behaviors.csv** |
| 最强证据 | 零字节差异 | 原版录像逐帧比对 | **内存状态快照 + Force Wave 注入** |
| Replay | 有 | 有 | **无**（小数点作传统） |

**结论：不能照搬 th095 的账本结构。** th095 的 `matches.csv` 是「这个函数逐字节对上了」，
TH185 没有字节级目标，所以改成「这个子系统实现了」+「这条行为验证了」两个独立维度。

---

## 1. 四事实分离

**「有源码」「能编译」「行为一致」「已验证」是四个独立事实，任何一个都不蕴含另一个。**

| 事实 | 记录位置 | 谁写 |
|---|---|---|
| 子系统有实现 | `subsystems.csv` 的 `status` | 人工审查后写 |
| 行为已实现 | `behaviors.csv` 的 `status=implemented` | 写完代码后写 |
| 行为已验证 | `behaviors.csv` 的 `status=verified` + `test_ref` | **必须**有可复现测试 |
| 子系统完成 | `subsystems.csv` 的 `status=complete` | 该子系统全部行为 verified 之后 |

违反这条的典型表现：

- 写了 `cpp/game/CardManager.cpp` 就说「卡牌系统搞定了」—— 那只是**有源码**
- 编译通过就说「能跑了」—— 那是**能编译**，不是行为一致
- 肉眼看游戏「感觉一样」—— 那不是**已验证**

---

## 2. 会话前置检查（每次动手前必跑）

```powershell
node th185_web/scripts/ci.mjs
```

必须三项全 PASS。任一失败就停。

| 检查 | 脚本 | 作用 |
|---|---|---|
| target | `verify-target.mjs` | 4 文件哈希 + PE 指纹 + CheckSum 自洽 + overlay=0 |
| ledgers | `validate-ledgers.mjs` | 三个账本的一致性、引用完整性、依赖无环 |
| progress | `progress.mjs --stdout` | 进度快照可生成 |

**不要用汉化版、Steam 加壳版或改版替代目标。**

---

## 3. 有界还原循环（单子系统级）

这是日常工作的核心节奏。**一次只推进一个子系统的一个行为面**，不要并行铺开。

### 步骤

1. **从 `config/subsystems.csv` 选一个 `not-started` 的子系统。**
   先看它的 `deps`——依赖没就绪就不要开工。

2. **从 `config/symbols.csv` 收集已知地址。**
   目前 23 条，全部来自 thprac（实战验证过）。

3. **拆成可验证的行为断言，写进 `behaviors.csv`。**
   每条断言必须是**可观察的**，不能是「实现了卡牌系统」这种笼统描述。
   反例：`卡牌系统工作正常`
   正例：`AddCard(84) 后卡牌管理器持有 85 张卡，第 85 张的 id 为 84`

4. **写实现。** 放在 `th185_web/cpp/game/` 或 `cpp/sdl/`。
   可复用的架构参考 th11，但**不要直接复制**——TH185 的机制必须重新验证。

5. **写测试，把路径填进 `test_ref`。**
   没有测试的断言只能是 `implemented`，不能是 `verified`。

6. **跑 CI，全 PASS 后提交。**

### 命令对照

```powershell
# 账本校验（改完 csv 立刻跑）
node th185_web/scripts/validate-ledgers.mjs

# 更新进度快照
node th185_web/scripts/progress.mjs

# 提交前总校验
node th185_web/scripts/ci.mjs
```

---

## 4. 验证手段（TH185 的短板）

th11 之所以能做到 100,672 帧六面录像比对，是因为**原版有 Replay**。
TH185 **没有**（见 `docs/RECONSTRUCTION.md` §3.2.1 的考证）。所以验证手段要降级并组合使用。

| 手段 | 强度 | 现状 | 说明 |
|---|---|---|---|
| **内存状态快照** | 中 | ⬜ 待建 | 用 thprac 地址表在关键节点 dump 原版内存，逐项 diff |
| **Force Wave 注入** | 中 | ⬜ 待建 | 用 `0x43D156` 固定波次，绕过随机，做单波次级比对 |
| **模块级原版对照** | 中 | ⬜ 待建 | Unicorn 执行原版函数，对比返回值/内存副作用 |
| **录像逐帧比对** | 高 | ❌ **不可用** | TH185 无 Replay |
| **视觉比对** | 低 | — | 只能当烟雾测试，不能当验收 |

### 关键缺口

**Force Wave 能绕过波次随机，但绕不过黑市随机。**
黑市是关卡中途的交互界面（每波结束 3 次购买，卡池随机，带 Discount 判定），
需要单独设计验证方法——例如用内存写直接指定商品，再比对购买后的状态。

---

## 5. 三个硬阻塞

### 5.1 PBG4 加密（P0，阻塞整个资源层）

```
th185.dat 头部: 13 5A B3 B6 F0 C7 54 D4 CB 7B 9F 8E F4 FF 5C 00
th18.dat  头部: 13 5A B3 B4 F0 CD 54 D4 CB 3C 9F 06 F4 F3 5C 00
```

同族加密。thtk 12 的 `thdat.exe` 报 `no module found for version 18`。

**两条路**：
1. 自己实现 PBG4 解密（社区无公开实现，要逆向）
2. 用调试器在 `file_load` 断点（Rx2174）之后 dump 内存镜像

**影响**：解不开 `.dat` 就拿不到 ANM/ECL/STD/MSG，`SYS-CORE-ANM` 和 `SYS-CORE-ECL` 都动不了。

### 5.2 RNG 必须逐位一致

TH185 的设计核心是「重复游玩逐渐变简单」，随机权重被刻意放大
（omake.txt 原文：「わざと振り幅大きくしています」）。

已知数据结构：
- 权重数组：`EnemyManager(0x4D7AF4) + 0x324`
- 名字表：`0x4B5D50`（110 条 ECL 子程序名）
- 模板池上限：**110 条**

任何一处 RNG 调用次序差异都会导致状态分叉。**这是最大的技术风险。**

### 5.3 无 Replay → 验证强度上限低

见 §4。这意味着即使全部子系统标 `complete`，也**无法**给出 th11 那种强度的保证。
必须在验收报告里明确写出这个边界。

---

## 6. 账本说明

| 文件 | 含义 | 关键约束 |
|---|---|---|
| `config/subsystems.csv` | 30 个子系统的状态 | `deps`/`parent` 必须存在且无环 |
| `config/behaviors.csv` | 可观察行为断言 | `verified` **必须**有 `test_ref` |
| `config/symbols.csv` | 已知地址（23 条） | 地址规范、无重复、必须写来源 |
| `config/final-baseline.json` | 共享组件基线 | 待从 th11 pin |
| `th185_web/target.json` | 目标指纹 | sha256 必须 64 位大写 |

### 子系统的六种状态

| status | 含义 |
|---|---|
| `not-started` | 未开始 |
| `in-progress` | 进行中，尚无可用实现 |
| `partial` | 部分实现，行为断言未全验证 |
| `complete` | 该子系统全部行为 verified |
| `blocked` | 被外部条件阻塞（必须在 notes 写明） |
| `excluded` | 明确不做（必须在 notes 说明理由） |

### 证据分级

| evidence_level | 含义 |
|---|---|
| `none` | 无证据 |
| `inferred` | 推断，可被推翻 |
| `corroborated` | 有外部或相邻来源支持（如 thprac/thcrap） |
| `observed` | 直接从原版运行时/内存读出 |
| `behavioral` | 行为等价已通过比对 |

**`verified` 要求 `evidence_level` 是 `behavioral` 或 `observed`**——`corroborated` 不够。
这条由校验器强制。

---

## 7. 提交纪律

从 th095 的 commit 历史反推：

| 观察 | 做法 |
|---|---|
| 首个 commit 全是基础设施，0 行游戏代码 | 先建工作流再动手 |
| 后续 commit 几乎都是「单函数 + 单一动词」 | 一次一个行为面，一个 commit 一个可验证边界 |
| 有专门的 workflow-maintenance batch | 账本/脚本改动单独成批 |

**提交前必做**：
1. `node th185_web/scripts/ci.mjs` 全 PASS
2. `git status` 里没有 `.exe` / `.dat` / `artifacts/`
3. 改了 `config/` 就一并提交 `docs/PROGRESS.md`

---

## 8. 当前状态

**已完成**：
- ✅ 目标身份认证（4 文件哈希 + PE 指纹 + CheckSum 自洽）
- ✅ 三脚本工作流（validate-ledgers / progress / ci）
- ✅ 三账本体系（30 子系统 / 行为断言 / 23 符号）
- ✅ thprac 一手参考资料入库（MIT）
- ✅ CI 三项全绿
- ✅ 校验器负向测试通过（verified 缺 test_ref、subsystem 不存在、依赖环 均被拦截）

**下一步（按优先级）**：
1. **P0：解决 PBG4 解密** —— 阻塞整个资源层，是唯一的硬前置
2. **P1：逆向 RNG** —— 拿到种子算法与消耗次序，这是 TH185 的核心机制
3. **P2：建内存状态快照工具** —— 用 thprac 的 23 个地址，先能 dump 原版状态
4. **P3：挑一个最小子系统端到端走通** —— 建议 `SYS-CORE-FILE`（文件层），不依赖解包

> 当前 **0 个子系统完成、0 条行为验证**。这是诚实的起点。
