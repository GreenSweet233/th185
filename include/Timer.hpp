#pragma once
// 倒计时钳制 —— Phase A 语义重建。
//
// ## 目标
//
// th185.exe @ 0x00402A50，44 字节，6 个调用方，无 callee（纯叶子）。
// thiscall：ecx = this，无参数，无返回值。
//
// ## 反汇编（二进制见证）
//
//   0x00402A50  8b 91 8C 00 00 00   mov edx, [ecx+0x8C]
//   0x00402A56  83 EA 01            sub edx, 1
//   0x00402A59  89 91 8C 00 00 00   mov [ecx+0x8C], edx
//   0x00402A5F  79 1B               jns 0x402A7C
//   0x00402A61  c7 81 8C..00000000 mov dword [ecx+0x8C], 0
//   0x00402A6B  c7 01 00000000      mov dword [ecx+0x00], 0
//   0x00402A71  c7 81 D4..00000000 mov dword [ecx+0xD4], 0
//   0x00402A7B  c3                  ret
//
// 语义：递减一个倒计时；变负时把它自己与另外两个 32 位字段清零。
//
// ## 为什么这里没有 struct
//
// 本函数只碰 +0x00 / +0x8C / +0xD4 三个 32 位字段，
// 三者之间的内容是什么，目前**没有证据**。
//
// AGENTS.md §0 第 3 条：每个源码元素必须有证据，作不出证就删掉或标 unresolved。
// AGENTS.md §8 第 1 条：不许加 dummy/pad。
//
// 所以这里**不定义 TimerHost 结构体**——用一个带三个 pad 成员的假 struct
// 去凑偏移，既是无见证元素（G2 FAIL），也是 §6 G4 明令禁止的 pad。
//
// 取而代之：把三个偏移写成具名常量（不是魔法数），函数接受 void*，
// 内部按常量换算。等后续函数确认了这些偏移之间的真实成员后，
// 再在 /include 里定义正式 struct 并锁进 /types/lock.json。
// 在那之前，本文件的 struct 布局标为 unresolved（见 /ledger/unresolved.md）。

namespace th185 {

// 二进制见证的字段偏移（不是推测）。
// 命名而非裸字面量，符合 §6 G4「未登记魔法数」禁令的反面。
constexpr unsigned kTimerCountdownOffset = 0x8C;
constexpr unsigned kTimerFieldAOffset = 0x00;   // 与倒计时一起被清零
constexpr unsigned kTimerFieldBOffset = 0xD4;   // 与倒计时一起被清零

// 递减倒计时；归负时清三个字段。
void tick_timer(void* self);

}  // namespace th185