#include "FieldAccess.hpp"

#include <xmmintrin.h>

namespace th185 {

namespace {
// 取 +offset 处的 32 位有符号字段。
//
// 用函数而不是就地强转：让「按偏移访问」这件事只出现在一个地方，
// 将来 struct 布局确定后只需改这里。也满足 G4 的 no-bare-offset-cast 规则。
inline int* field_at(void* self, unsigned offset) {
    return reinterpret_cast<int*>(static_cast<char*>(self) + offset);
}

// 取 +offset 处的 float 地址（避免裸偏移强转）。
inline float* float_at(void* self, unsigned offset) {
    return reinterpret_cast<float*>(static_cast<char*>(self) + offset);
}

// 12 字节聚合类型。
//
// ★ 必须用 struct 整体赋值 —— 实测逐字段赋值生成 3 条普通 mov（18 字节），
//   而 struct 赋值生成 movq xmm0(8字节) + mov(4字节)，与原版一致。
struct Vec3Raw {
    int x;
    int y;
    int z;
};
}  // namespace

//@witness func=0x00404D10 kind=stdcall
int __stdcall first_of_three(int a, int b, int c) {
    (void)b;
    (void)c;
    return a;
}

//@witness func=0x00407E40 kind=thiscall
//@witness disp 0x2A0
int __fastcall is_field_2A0_set(void* self, void* unused_edx) {
    (void)unused_edx;
    return *field_at(self, kField2A0Offset) != 0;
}

//@witness func=0x0040A050 kind=thiscall
void* __fastcall clear_first_and_return_this(void* self, void* unused_edx) {
    (void)unused_edx;
    *reinterpret_cast<int*>(self) = 0;
    return self;
}

//@witness func=0x00409F60 kind=thiscall
//@witness disp 0x20
void __fastcall set_field_20(void* self, void* unused_edx, int value) {
    (void)unused_edx;
    *field_at(self, kField20Offset) = value;
}

//@witness func=0x00433BC0 kind=thiscall
void __fastcall set_first_field(void* self, void* unused_edx, int value) {
    (void)unused_edx;
    *reinterpret_cast<int*>(self) = value;
}

//@witness func=0x004063C0 kind=thiscall
//@witness disp 0x5F0
//@witness disp 0x5F8
void __fastcall copy_vec3_to_5F0(void* self, void* unused_edx, const void* src) {
    (void)unused_edx;
    Vec3Raw* dst = reinterpret_cast<Vec3Raw*>(field_at(self, kVec3At5F0Offset));
    *dst = *static_cast<const Vec3Raw*>(src);
}

// --- 0x00404B60 / 0x00405A60：SSE 标量读取 ---
//
// ★ 这两个函数返回 __m128 而不是 float。
//
//   返回 float       -> d9 01        fld dword [ecx]        （x87 栈）
//   返回 __m128+load -> f3 0f 10 01  movss xmm0,[ecx]       （SSE 寄存器）
//
// MSVC 在 x86 上用 x87 返回 float，只有返回 __m128 才走 XMM0。
// 字节: 0x00404B60 = f30f1001c3（5 字节）
//@witness func=0x00404B60 kind=thiscall
__m128 __fastcall read_first_as_m128(void* self, void* unused_edx) {
    (void)unused_edx;
    return _mm_load_ss(static_cast<float*>(self));
}

//@witness func=0x00405A60 kind=thiscall
//@witness disp 0x44
__m128 __fastcall read_44_as_m128(void* self, void* unused_edx) {
    (void)unused_edx;
    return _mm_load_ss(float_at(self, kSseField44Offset));
}

// --- 0x0041C710：SSE 标量乘法 ---
//
// ★ 关键是第二个操作数保持**内存形式**：
//   _mm_mul_ss(a, _mm_load_ss(&b))  -> 48 字节（先 load 到寄存器再 mul）
//   _mm_mul_ss(a, *(__m128*)&b)     -> 43 字节 MATCH（mulss 直接读内存）
// 字节: f30f104174 f30f594154 c3
//@witness func=0x0041C710 kind=thiscall
//@witness disp 0x74
//@witness disp 0x54
__m128 __fastcall mul_74_x_54(void* self, void* unused_edx) {
    (void)unused_edx;
    return _mm_mul_ss(_mm_load_ss(float_at(self, kSseField74Offset)),
                      *reinterpret_cast<__m128*>(float_at(self, kSseField54Offset)));
}

}  // namespace th185