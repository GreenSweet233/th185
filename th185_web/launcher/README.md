# launcher

启动器。**本仓库不自建启动器**，直接沿用工作区共用的 Eagler Touhou 启动器。

## 来源

- 上游权威：`../eagler-touhou`（`src/launcher/`、`src/contracts/`）
- 本工作区最终版：`../th11/th11_web/launcher/`（GPL-3.0-or-later，含 `LICENSE`）

## 接入时要做的事

1. 复制 `../th11/th11_web/launcher/` 到本目录（或作为 submodule 固定到某个 commit）。
2. 在 `../eagler-touhou/src/contracts/product-catalog.mts` 的 `PRODUCT_GAMES` 中注册 `th185` 条目。
3. 准备卡牌美术 `th185-card.png`（640×480 或同比例）。
4. 声明存储名：`saveRoot: "/savesth185"`、`scoreFile: "scoreth185.dat"`、`configFiles: ["th185.cfg"]`。
5. 声明 package 的 data/music 布局（TH185 的音乐是 `thbgm.dat`，需要转成 OGG 或运行时解码）。

## 不能做的事

**不要在启动器、Host、Package 或部署代码里加 `if (game === "th185")`。** 上游 `ADAPTING_A_GAME.md` 明确要求：产品差异必须**声明**，不能教共享编排认识新的游戏名。
