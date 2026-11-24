#pragma once
// 极简叶子函数组 —— Phase A 语义重建。
//
// 这些函数的反汇编形状完全相同，是「同一段源码在多个类里实例化」的典型。
// 按 AGENTS.md §5「叶子/简单函数先行」的原则，它们是第一批匹配对象。
//
// ## 形状 A：`mov eax, ecx; ret`（3 字节）
//
// ```
// mov eax, ecx     ; eax = this
// ret
// ```
//
// thiscall 返回 this 指针。C++ 里对应返回 `*this` 或自身指针的访问器。
// 例：0x00417AF0（15 个调用方）、0x00404AA0（6 个调用方）
//
// ## 形状 B：`ret 0`（3 字节）
//
// ```
// ret 0            ; stdcall/thiscall 的零参数空函数返回
// ```
//
// 空函数体。13 个函数共享此形状（0x00404C20 有 11 个调用方）。
// 典型用途：虚函数的空实现（纯虚之外的默认空体）。
//
// ## 形状 C：`mov eax, [ecx]; ret`（3 字节）
//
// ```
// mov eax, [ecx]   ; 读首字段
// ret
// ```
//
// 返回 this 的第一个 32 位字段。例：0x0041FA20（7 个调用方）

namespace th185 {

// --- 形状 A：返回 this ---
//
// 用 void* 而不是具体类型：本函数不碰任何字段，
// 具体类型目前无证据（AGENTS §0 第 3 条），不编造。
void* return_this(void* self);

// --- 形状 B：空函数 ---
//
// 无参数、无返回值、无副作用。
void do_nothing();

// --- 形状 C：读首字段 ---
//
// 返回 *reinterpret_cast<unsigned*>(self)。
unsigned read_first_field(void* self);

// --- 形状 D：读首字段为 float ---
//
// `movss xmm0, [ecx]; ret` —— 与形状 C 只差数据宽度。
float read_first_float(void* self);

// --- 形状 E：极坐标 → Vec2（`fsincos` 形状，20 个函数共享）---
//
// 反汇编（0x00404B70，20 个之一）：
//
//   mov eax, [ebp-4]        ; eax = this
//   fld  dword [ebp+8]      ; st0 = angle
//   fsincos                 ; st0 = sin(angle), st1 = cos(angle)
//   fmul dword [ebp+0xC]    ; * length
//   fstp dword [eax]        ; this->x = sin(angle) * length
//   fmul dword [ebp+0xC]    ; * length
//   fstp dword [eax+4]      ; this->y = cos(angle) * length
//   ret 8                   ; stdcall/thiscall，2 个 float 参数
//
// 语义：给定角度与长度，算出 (sin*len, cos*len) 写进 this 的前两个 float。
//
// ★ 注意 x 用 sin、y 用 cos —— 不是常见的 (cos, sin)。
//   这是从 fsincos 的栈序推出来的：fsincos 之后 st0=sin、st1=cos，
//   第一次 fstp 弹出的是 st0=sin，写到 [eax]（即 x）。
void polar_to_vec2(void* self, float angle, float length);

}  // namespace th185