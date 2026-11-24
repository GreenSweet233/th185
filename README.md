# th185

> 東方虹龍洞 外伝 ～ バレットフィリア達の闇市場 ～ (TH185) 的**功能还原**。
>
> 目标：用可读的 C++ / TypeScript 重建原版行为，逐帧等价。
>
> ⚠️ **本仓库不再做字节级匹配** —— 那部分已独立到
> [`th185-matching`](https://github.com/GreenSweet233/th185-matching)。

---

## 两个仓库的分工

| 仓库 | 追求 | 规模 | 验证方式 |
|---|---|---|---|
| **`th185`**（本仓库）| 可读、行为等价的 C++ 重写 | 37k 行 | 逐帧行为比对 |
| [`th185-matching`](https://github.com/GreenSweet233/th185-matching) | 编译产物与原版**逐字节相同** | 5k 行 | `diff = 0` |

**为什么分家**：字节匹配要求 `__fastcall` + 哑参数、宏折叠等技巧，
这些让功能还原的代码变得难读；而功能还原的可读性要求又让字节匹配无法达成。
两边约束互相拖累，所以拆开各自演进。

---

## 这个仓库里有什么

```
th185_web/
├─ docs/           26 篇逆向文档（ECL / ANM / PBG4 / RNG / 卡牌 / 关卡…）
├─ cpp/game/       C++ 游戏逻辑（Archive / Card / Difficulty / Ecl / Rng / Stage / Wave）
├─ cpp/sdl/        平台层
├─ scripts/        构建与验证脚本（build-cpp.mjs / verify-target.mjs）
├─ config/         final-baseline.json
├─ sdl-runtime/    web 运行时壳
├─ launcher/       启动器接入
├─ tests/          测试
└─ target.json     目标指纹

portable/          共享渲染器（SDL 等）
artifacts/         字体等产物
_references/       第三方参考资料（thtk 等，含 provenance）
cpp_world01/02/06.ecl.bin   ECL 解析产物
```

## 关键文档

| 文档 | 内容 |
|---|---|
| [`docs/RECONSTRUCTION.md`](th185_web/docs/RECONSTRUCTION.md) | 路线选择、技术方案、验收标准、工作量 |
| [`docs/BUILD.md`](th185_web/docs/BUILD.md) | 架构、环境、构建、验证 |
| [`docs/PROGRESS.md`](th185_web/docs/PROGRESS.md) | 进度 |
| [`docs/ECL.md`](th185_web/docs/ECL.md) | ECL 脚本格式（已完全解出）|
| [`docs/PBG4.md`](th185_web/docs/PBG4.md) | PBG4 归档格式（含解密 + LZSS）|
| [`docs/RNG.md`](th185_web/docs/RNG.md) | RNG 逐位一致的实测数据 |
| [`docs/CARD_CLASSES.md`](th185_web/docs/CARD_CLASSES.md) | 87 个卡牌类与 vtable |

## 原版文件

`th185.exe` 与 `th185.dat` **不入库**（版权 + 体积）。
本机路径写在 `th185_web/local.target.json`（已 gitignore）。

版本校验：`th185.exe` 的 SHA-256 必须是
`76bbf844e962c16004b64f1b2ef3551593056baa43ea53b9a5df45ad731345c7`。

用 `th185_web/scripts/verify-target.mjs` 校验。

## 三个必须先知道的事实

1. **本作取消了 Replay 系统**（游戏层），但引擎层的 `ReplayManager` 代码还在。
   这意味着 th11 那套「原版录像逐帧比对」的验证手段直接失效。
   替代方案见 `docs/RECONSTRUCTION.md` §3.3。
2. **自机只有魔理沙 1 组**（TH18 是 4 人 × 2 子机）。这是唯一让工作量变小的地方。
3. **87 个卡牌类**（TH18 只有 58），全部通过 RTTI → vtable 定位。

---

## License

见 [LICENSE](LICENSE)。原版游戏资源版权归原作者（上海アリス幻樂団 / 製作者）所有。