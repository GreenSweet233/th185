#include "Trivial.hpp"

#include <cmath>

namespace th185 {

// --- 形状 A：返回 this（`mov eax, ecx; ret`）---
//
// 2 个二进制函数共享此形状：0x00404AA0（6 个调用方）、0x00417AF0（15 个调用方）
//@witness func=0x00404AA0+0x00417AF0 kind=thiscall
void* return_this(void* self) {
    return self;
}

// --- 形状 B：空函数（`ret 0`）---
//
// 13 个二进制函数共享此形状。0x00404C20 有 11 个调用方。
// 这是「虚函数的空实现」最常见的形态。
//@witness func=0x00404C20+0x00441640+0x00448100+0x0045CA20+0x004717D0+0x0047A210+0x0048AFA0+0x0048C910 kind=thiscall
void do_nothing() {
}

// --- 形状 C：读首字段（`mov eax, [ecx]; ret`）---
//
// 2 个：0x0041FA20（7 个调用方）、0x00433B40（4 个调用方）
//@witness func=0x0041FA20+0x00433B40 kind=thiscall
unsigned read_first_field(void* self) {
    return *reinterpret_cast<unsigned*>(self);
}

// --- 形状 D：读首字段为 float（`movss xmm0, [ecx]; ret`）---
//
// 2 个：0x00404B60、0x00441610
//@witness func=0x00404B60+0x00441610 kind=thiscall
float read_first_float(void* self) {
    return *reinterpret_cast<float*>(self);
}

// --- 形状 E：极坐标 → Vec2 ---
//
// 20 个函数共享 `fsincos` 形状。语义见 Trivial.hpp 的注释。
// x = sin(angle)*length，y = cos(angle)*length（注意顺序）。
//@witness func=0x00404B70+0x00405A70+0x00406290+0x00407E10+0x00417750+0x00417B00+0x004299D0+0x004305B0+0x00430720+0x00441580+0x00441670+0x0044CDA0+0x004587B0+0x00463DF0+0x00467150+0x0047FD20+0x0047FFE0+0x00485B20+0x00486080+0x0048F370 kind=stdcall
void polar_to_vec2(void* self, float angle, float length) {
    float* out = reinterpret_cast<float*>(self);
    float s = 0.0f;
    float c = 0.0f;
    // 用 sincos 的语义：原版用 x87 的 fsincos 一次算出两者。
    // 这里分两次算，因为 C++ 标准库没有 sincos；Phase B 若需要逐字节
    // 一致，要换成编译器内建或内联 x87。
    s = std::sin(angle);
    c = std::cos(angle);
    out[0] = s * length;
    out[1] = c * length;
}

}  // namespace th185