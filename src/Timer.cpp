#include "Timer.hpp"

namespace th185 {

namespace {

// 取 +offset 处的 32 位有符号字段。
//
// 用函数而不是就地强转，是为了让「按偏移访问」这件事只出现在一个地方：
// 将来 struct 布局确定后，只需要改这一个函数，不必逐处改调用点。
//
// 注意：offset 是具名常量传入，不是裸字面量，符合 §6 G4。
inline int* field_at(void* self, unsigned offset) {
    return reinterpret_cast<int*>(static_cast<char*>(self) + offset);
}

}  // namespace

//@witness func=0x00402A50 kind=thiscall
//@witness disp 0x8C
//@witness disp 0xD4
void tick_timer(void* self) {
    int* countdown = field_at(self, kTimerCountdownOffset);
    *countdown -= 1;
    if (*countdown >= 0) {
        return;
    }
    *countdown = 0;
    *field_at(self, kTimerFieldAOffset) = 0;
    *field_at(self, kTimerFieldBOffset) = 0;
}

}  // namespace th185