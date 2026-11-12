# TH185 协程的栈语义

> 结论：**协程不只是保存 pc，还必须保存操作数栈**

---

## 1. 问题

`run()` 每次调用都重置：

```cpp
stats_ = EclRunStats{};
stack_ = EclStack{};   // ← 栈被清空
```

这对「每帧从头重放」是对的。
但协程从 `pc` 恢复时，**栈是空的**——等待之前压入的值全部丢失。

### 1.1 为什么这会让分支走错

`main` 的 `[15]-[17]`：

```
[15] op42 push -9891     ← 压入一个值
[16] op13 JumpIfZero     ← 立即基于它分支
[17] op12 Jump -> [57]   ← 非零走这里，跳过全部 4 个 CALL
```

如果 `[15]` 与 `[16]` 之间发生挂起，恢复后栈为空，
`JumpIfZero` 弹出的是垃圾——分支方向随机。

**这个 bug 的表现是「调用数恒为 2」，看起来像分支条件判断错了，
实际是栈在恢复时被清空了。**

---

## 2. 修法

```cpp
struct CoroutineState {
    u32 pc = 0;
    u32 wait_frames = 0;
    bool started = false;
    // 操作数栈快照，(type, bits)，栈底在前
    std::vector<std::pair<u32, u32>> stack;
};
```

```cpp
void EclStack::save(std::vector<std::pair<u32, u32>>& out) const {
    out.clear();
    out.reserve(depth_);
    for (u32 i = 0; i < depth_; ++i)
        out.emplace_back(u32(slots_[i].type), slots_[i].bits);
}
```

**只复制活槽**（`depth_` 个），不是整个 16384 项数组。

### 2.1 在 run_coroutine 里

```cpp
if (!state.stack.empty()) stack_.restore(state.stack);   // 恢复
const auto result = run(code, 20000, code_base, state.pc,
                        &state.pc, &state.wait_frames);
if (state.wait_frames > 0) stack_.save(state.stack);     // 再挂起时保存
else state.stack.clear();
```

---

## 3. 顺带修的两处

### 3.1 `main` 必须最先运行

其余子程序都是它按名字调用的助手。
单独跑它们会先耗尽每帧预算于各自的等待循环，`main` 永远轮不到。

```cpp
if (is_controller) active_scripts_.insert(active_scripts_.begin(), sub.name);
else active_scripts_.push_back(sub.name);
```

### 3.2 `PushInt` 的负值要解引用

`-9891 + 10000 = 109` 是 builtin 槽，不是字面量。

```cpp
const i32 raw = arg_i32(payload, 0);
u32 bits = u32(raw);
if (raw < 0 && raw >= -ECL_BUILTIN_BASE &&
    raw + ECL_BUILTIN_BASE < i32(ECL_BUILTIN_COUNT)) {
    bits = context.builtins[u32(raw + ECL_BUILTIN_BASE)];
}
```

**但 `builtins` 全量初始化为 0 且从未被赋值**，所以解引用后仍是 0。

---

## 4. 仍未解决

三处修正后 `calls` 仍恒为 2（只到 `WaveSync` 与 `WorldWaveB00`）：

| 已排除 | 证据 |
|---|---|
| 栈恢复 | 已实现 save/restore，行为不变 |
| builtin 解引用 | 已实现，`builtins` 全为 0 |
| `main` 顺序 | 已排最前，行为不变 |

**剩余疑点：`op42` 在 `[15]` 处的真实语义。**

可能它不是「压入」而是「压入并消耗某个帧变量」，
或 `-9891` 对应的 builtin 槽在原版里有非零初值（如关卡编号、难度）。

### 4.1 为什么不能继续猜

`builtins` 的语义只解出 9 个（难度、is_difficulty_0..3、EM+0x3c/40/44/4f44、stage-2）。
槽 109 不在已知集合里。

**再猜下去就是在没有证据的情况下改行为，这比留下缺口更危险。**

---

## 5. 当前状态

| 项 | 状态 |
|---|---|
| 全部 opcode 已实现 | ✅ |
| 协程 pc 推进 | ✅ |
| 协程栈保存 | ✅ 本轮 |
| `main` 优先 | ✅ 本轮 |
| builtin 解引用 | ✅ 本轮 |
| 4 个波次调用被执行 | ❌ 分支未走通 |
