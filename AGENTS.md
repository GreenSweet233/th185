# AGENTS.md — th185 工程规约

面向所有协作者（人类或 AI）的工作约定。**动手前必读。**

本仓库是 [Eagler Touhou](https://github.com/YomotsuHisami/eagler-touhou) 计划的 TH185 接入分支，规格对齐同工作区的 `th10` / `th11` 仓库。

---

## 0. 铁律

1. **不得把原版游戏文件提交进仓库。**
   `th185.exe` / `th185.dat` / `thbgm.dat` / `custom.exe` / `th185.sdb` 一律在 `.gitignore` 中。仓库只保存 `th185_web/target.json` 里的哈希与 `scripts/verify-target.mjs` 校验脚本。

2. **任何改动前后都要跑目标校验。**
   ```powershell
   .\th10_web\tools\node.exe th185_web/scripts/verify-target.mjs
   ```
   必须 exit 0。若 FAIL，先查是不是拿错目录（汉化版 / 改版 / 非 v1.00a）。

3. **本仓库不做字节级 matching。** 目标是**行为等价**，不是二进制相同。不要为了"让 .text 段哈希对上"去折腾编译器版本——那是 `../th18_decomp` 的路线，不是这条。

4. **生产运行时不执行原版 EXE，不含 x86 转译，不使用 D3D8/9 适配层。** 原版 exe 只用于开发期行为对照（Unicorn）与录像录制。

5. **RNG 必须逐位一致。** TH185 随机权重极大，任何 RNG 调用次序差异都会导致录像失同步。改动任何涉及随机数的代码后，必须重跑录像比对。

6. **不把"能跑起来"当成验收。** 验收标准见 `th185_web/docs/RECONSTRUCTION.md` §5。

---

## 1. 环境

| 项 | 值 | 说明 |
|---|---|---|
| 工作目录 | `D:\Windows\GreenSweet\Game\Tasks\thvip\th185` | 本仓库 |
| 工作区根 | `D:\Windows\GreenSweet\Game\Tasks\thvip` | 共享工具链在这里 |
| 原版游戏 | 见 `th185_web/local.target.json`（本机） | 不入库 |
| Node.js | `th10_web/tools/node.exe` | **勿用系统 Node** |
| Emscripten | `tools/emsdk` | 固定版本 |
| WASI SDK | `th10_web/tools/wasi-sdk-34.0-x86_64-windows` | 原版对照用 C++ 核心 |
| Python | 系统 Python 3 | 打包脚本 |

---

## 2. 目录约定

| 目录 | 用途 | 是否入库 |
|---|---|---|
| `th185_web/docs/` | 文档（Markdown） | ✅ |
| `th185_web/scripts/` | 构建与验证脚本（.mjs / .py） | ✅ |
| `th185_web/cpp/` | 游戏与平台层源码 | ✅ |
| `th185_web/tests/` | 测试 | ✅ |
| `th185_web/config/` | 基线配置 | ✅ |
| `th185_web/launcher/` | 启动器（沿用工作区共用） | ✅ |
| `th185_web/sdl-runtime/` | Web 运行时壳 | ✅ |
| `th185_web/target.json` | 目标指纹 | ✅ |
| `th185_web/local.target.json` | 本机路径覆盖 | ❌ |
| `th185_web/artifacts/` | 构建产物 | ❌ |
| `th185_web/assets/` | 原版资源 | ❌ |
| `th185_web/reference/` | 参考录像 | ❌ |
| `portable/` | 共享渲染器与输入 | ✅ |
| `deploy/` | 部署脚本 | ❌（与 th11 一致） |

---

## 3. 验证流程

### 3.1 目标指纹校验

```powershell
.\th10_web\tools\node.exe th185_web/scripts/verify-target.mjs
```

校验内容：
1. 四个核心文件的 SHA-256（`th185.exe` / `th185.dat` / `thbgm.dat` / `custom.exe`）
2. PE 指纹：`TimeDateStamp=1659144319` + `.text SizeOfRawData=711168` + `PECheckSum=0x000DE79F`
3. PE CheckSum 自洽性（重算值 == 存储值，否则文件被改过）
4. 分节 SHA-256 与 overlay 检查

### 3.2 PE CheckSum 重算的正确算法

**CheckSum 是 4 字节 DWORD，必须整体跳过**（等价于先清零再算）：

```js
const ckOff = peOffset + 24 + 64;
let total = 0;
for (let i = 0; i < buf.length; i += 2) {
  if (i >= ckOff && i < ckOff + 4) continue;   // ← 跳过全部 4 字节
  const lo = buf[i];
  const hi = i + 1 < buf.length ? buf[i + 1] : 0;
  total += lo + (hi << 8);
  total = (total & 0xffff) + (total >> 16);
}
total = (total & 0xffff) + (total >> 16);
total += buf.length;
```

> **踩坑记录**（来自 `../th18_decomp`）：只跳过低 2 字节会多累加高 16 位，TH18 实测偏差 0x0C。TH185 同样必须跳过 4 字节。

---

## 4. 代码风格

- C++：C++17，`-ffp-contract=off`、`-fno-strict-aliasing`、`-fno-exceptions`、`-fno-rtti`（与 th11 的 `build-sdl.mjs` 一致）。
- 脚本：ESM（.mjs），Node 22+；Python 3.8+。
- 所有脚本必须支持 `--help` 或至少有顶部注释说明用法与退出码。
- 输出中文，文件编码 UTF-8。
- 不得静默失败：出错必须打印原因并返回非零退出码。

---

## 5. 已知陷阱

| 陷阱 | 表现 | 应对 |
|---|---|---|
| PowerShell 把 `[...]` 当通配符 | 对含 `[th185]` 的路径返回空 | 用 `-LiteralPath` |
| 哈希大小写不一致 | 比较误报不匹配 | 统一大写 |
| `thdat.exe` 不支持 version 18+ | `no module found for version 18` | 待自己实现 PBG4 |
| `th185.dat` 是 PBG4 加密 | 头部 `13 5A B3 B6 F0 C7 54 D4` | 先解包，否则一切无从谈起 |
| Windows 控制台 GBK | 中文乱码 | 设 `PYTHONIOENCODING=utf-8` |

---

## 6. 与外部项目的对照原则

- **thcrap**（`thpatch/thcrap-tsa`）：地址表可信度高，但其 `binhacks` 的 `expected` 字段**不保证准确**。用它定位，别用它断言。
- **thprac**（`touhouworldcup/thprac`）：全局变量表经过实战验证，可信度最高。
- **TH11 仓库**（`../th11`）：架构与流程的直接参考，但关卡结构与随机性差异巨大，不能照搬。
- **TH18 decomp 项目**（`../th18_decomp`）：前作，卡牌/ECL/ANM 结构相似，成果可迁移。
