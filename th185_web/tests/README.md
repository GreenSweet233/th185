# tests

| 目录 | 内容 | 状态 |
|---|---|---|
| `cpp/` | 模块级原版对照测试（Unicorn 执行原版函数 vs C++ 实现） | ⬜ |
| `browser/` | 浏览器端集成测试（启动器、存档、录像、触控、音频） | ⬜ |

## 运行

```powershell
# 模块级（较慢）
.\th10_web\tools\node.exe --test --test-concurrency=1 'th185_web/tests/cpp/*.test.mjs'

# 浏览器端
.\th10_web\tools\node.exe th185_web/tests/browser/launcher-check.mjs
```

## 测试原则

1. **每个管理器都要有原版对照测试**，不能只测自己写的实现。
2. **录像比对是最高级验证**，模块测试通过不等于整体一致。
3. **固定样本不等于穷举**，报告里必须写明测试边界。
