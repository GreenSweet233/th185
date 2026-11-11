# _references

外部参考资料。**不是本仓库的源码，不参与构建。**

## thprac

| 项 | 值 |
|---|---|
| 上游 | https://github.com/touhouworldcup/thprac |
| 许可证 | MIT（见同目录 `LICENCE`，Copyright (c) 2022 Ack） |
| 取用日期 | 2026-11-11 |
| 内容 | `thprac_th185.cpp` + `thprac_th185_warps0..8.cpp` |

### 为什么保留这份副本

thprac 是**目前唯一对 TH185 做过实战逆向**的项目。它的 `thprac_th185.cpp` 里有：

1. **29 个全局变量地址**（`enum addrs`）—— 已在 thprac 中实战验证，可信度最高
2. **2 个函数地址**（`enum funcs`：`SAVE_REPLAY` / `SET_MENU`）
3. **9 个关卡的波次结构**（`warps0..8`）—— 这是反推 TH185「9 场景 × 波次」设计的一手材料
4. **波次随机系统的数据结构**（`EnemyManager + 0x324` 权重数组、`0x4B5D50` 名字表、110 条模板池上限）
5. **作弊/练习功能实现**，反推出机制细节（如 `AddCard` 的 `cardId < 85` 边界、F6 全卡牌批发的 85 指针表）

### 使用约束

- **只读参考，不得修改后提交回上游。**
- 引用其中的地址/结论时，必须标注来源文件与行号，并区分「thprac 已验证」与「本仓库推断」。
- 不得把 thprac 的代码直接复制进 `th185_web/cpp/`——本仓库是**独立重实现**，不是 thprac 的分支。

### 关键地址速查（源自 `thprac_th185.cpp`）

```cpp
enum addrs {
    BLACK_MARKET_PTR = 0x4d7ac4,   // 黑市对象
    CARD_DESC_LIST   = 0x4ca370,   // 卡牌描述表
    ENEMY_MANAGER    = 0x4d7af4,   // 敌机管理器（+0x324 波次权重）
    GAME_THREAD_PTR  = 0x4d7b0c,   // 游戏线程
    SCALE_ADDR       = 0x573DC0,   // 渲染缩放
};
// 另有 funds=0x4d106c, bulletMoney=0x4d1070/0x4d1074, life=0x4d10bc,
// difficulty=0x4d1038, phase=0x4d1024, stage=0x4d7c68, 波次名表=0x4B5D50
```

> 完整 29 个全局变量已整理进 `th185_web/config/` 的符号台账（见后续工作流文档）。
