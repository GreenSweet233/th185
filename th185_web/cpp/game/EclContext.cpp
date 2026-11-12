#include "EclContext.hpp"

namespace th185 {

void EclContext::record_op(u16 opcode, const u8* payload, u32 payload_size) {
    ++recorded_total;
    // Keep memory bounded: a long-running script must not grow without limit.
    if (recorded.size() >= 4096) return;

    RecordedOp entry;
    entry.opcode = opcode;
    const u32 words = payload_size / 4;
    entry.word_count = (words > 8) ? 8 : words;
    for (u32 i = 0; i < entry.word_count; ++i) {
        const u8* at = payload + i * 4;
        entry.words[i] = u32(at[0]) | (u32(at[1]) << 8) | (u32(at[2]) << 16) |
                         (u32(at[3]) << 24);
    }
    recorded.push_back(entry);
}

void EclContext::add_subroutine(std::string name, std::vector<u8> code) {
    EclCallable entry;
    entry.name = std::move(name);
    entry.code = std::move(code);
    subroutines[entry.name] = std::move(entry);
}

u32 EclContext::push_frame() {
    frames.emplace_back();
    frames.back().fill(0);
    return u32(frames.size() - 1);
}

bool EclContext::pop_frame() {
    if (frames.empty()) return false;
    frames.pop_back();
    return true;
}

EclContext::RefKind EclContext::classify(i32 value) {
    if (value < 0) {
        const i32 index = value + ECL_BUILTIN_BASE;
        if (index >= 0 && u32(index) < ECL_BUILTIN_COUNT) return RefKind::Builtin;
        return RefKind::Invalid;
    }
    // Frame-local offsets are byte offsets into a 256-byte frame, so they must
    // be 4-aligned and inside it. Measured: every observed value is a multiple
    // of 4 in 0..60, but the check stays general.
    if ((value % 4) != 0) return RefKind::Invalid;
    if (u32(value) + 4 > ECL_FRAME_BYTES) return RefKind::Invalid;
    return RefKind::FrameLocal;
}

u32* EclContext::resolve_int_ref(i32 value) {
    if (value < 0) {
        const i32 index = value + ECL_BUILTIN_BASE;
        if (index < 0 || u32(index) >= ECL_BUILTIN_COUNT) return nullptr;
        return &builtins[u32(index)];
    }
    if (frames.empty()) return nullptr;
    if ((value % 4) != 0) return nullptr;
    const u32 offset = u32(value);
    if (offset + 4 > ECL_FRAME_BYTES) return nullptr;
    u8* base = frames.back().data() + offset;
    return reinterpret_cast<u32*>(base);
}

i32 EclContext::read_int_ref(i32 value) const {
    if (value < 0) {
        const i32 index = value + ECL_BUILTIN_BASE;
        if (index < 0 || u32(index) >= ECL_BUILTIN_COUNT) return 0;
        return signed_bits(builtins[u32(index)]);
    }
    if (frames.empty()) return 0;
    if ((value % 4) != 0) return 0;
    const u32 offset = u32(value);
    if (offset + 4 > ECL_FRAME_BYTES) return 0;
    const u8* base = frames.back().data() + offset;
    u32 bits = u32(base[0]) | (u32(base[1]) << 8) | (u32(base[2]) << 16) |
               (u32(base[3]) << 24);
    return signed_bits(bits);
}

bool EclContext::write_int_ref(i32 value, i32 stored) {
    u32* slot = resolve_int_ref(value);
    if (!slot) return false;
    *slot = u32(stored);
    return true;
}

bool EclContext::write_float_ref(i32 value, float stored) {
    u32* slot = resolve_int_ref(value);
    if (!slot) return false;
    *slot = float_bits(stored);
    return true;
}

}  // namespace th185
