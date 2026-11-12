#include "AnmInterpreter.hpp"

#include <cmath>
#include <cstring>

namespace th185 {

namespace {

i16 read_i16(const u8* p) noexcept { return i16(u16(p[0]) | (u16(p[1]) << 8)); }
u16 read_u16(const u8* p) noexcept { return u16(p[0]) | (u16(p[1]) << 8); }
u32 read_u32(const u8* p) noexcept {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}
i32 read_i32(const u8* p) noexcept { return signed_bits(read_u32(p)); }
float read_f32(const u8* p) noexcept { return bits_float(read_u32(p)); }

// Instruction header is 8 bytes; every argument is 4 bytes.
constexpr u32 ANM_HEADER = 8;
constexpr u32 ANM_ARG = 4;

bool span(u32 offset, u32 length, u32 size) {
    return offset <= size && length <= size - offset;
}

// th15 wraps angles into (-pi, pi].
float normalize_angle(float value) {
    constexpr double PI = 3.1415927410125732421875;
    constexpr double TAU = 6.283185482025146484375;
    u32 loops = 0;
    while (value > float(PI)) {
        value = float(double(value) - TAU);
        if (++loops > 32) break;
    }
    while (value < float(-PI)) {
        value = float(double(value) + TAU);
        if (++loops > 32) break;
    }
    return value;
}

// How many 4-byte arguments an opcode takes. Mirrors the `needed` table in
// th15's AnmInterpreter.cpp so a truncated instruction is caught before it is
// read.
u32 argument_count(i16 opcode) {
    if (opcode >= 100 && opcode <= 111) return 2;
    if (opcode >= 112 && opcode <= 121) return 3;
    if (opcode == 122 || opcode == 123) return 2;
    if (opcode == 200) return 2;
    if (opcode == 201) return 3;
    if (opcode >= 202 && opcode <= 213) return 4;
    if (opcode == 5 || opcode == 6) return 1;
    if (opcode >= 124 && opcode <= 128) return 2;
    if (opcode == 129) return 1;
    if (opcode == 130 || opcode == 131) return 4;
    if (opcode == 300) return 1;
    if (opcode == 301) return 2;
    if (opcode >= 302 && opcode <= 317) {
        if (opcode == 308 || opcode == 309 || opcode == 316 || opcode == 317) return 0;
        if (opcode == 312) return 2;
        return 1;
    }
    if (opcode == 400 || opcode == 401 || opcode == 404 || opcode == 406 ||
        opcode == 415) return 3;
    if (opcode == 402 || opcode == 416 || opcode == 429 || opcode == 434 ||
        opcode == 436) return 2;
    if (opcode == 403 || opcode == 405 || opcode == 423 || opcode == 424 ||
        opcode == 425 || opcode == 426 || opcode == 431 || opcode == 432 ||
        opcode == 437 || opcode == 438) return 1;
    if (opcode == 407 || opcode == 408 || opcode == 410 || opcode == 413) return 5;
    if (opcode == 409 || opcode == 411 || opcode == 414) return 3;
    if (opcode == 412 || opcode == 430 || opcode == 433 || opcode == 435) return 4;
    if (opcode == 417) return 2;
    if (opcode == 420) return 10;
    if (opcode >= 500 && opcode <= 508) {
        return (opcode == 505 || opcode == 506) ? 3 : 1;
    }
    if (opcode >= 600 && opcode <= 611) {
        if (opcode == 611) return 3;
        if (opcode >= 603 && opcode <= 608) return 2;
        return 1;
    }
    return 0;
}

}  // namespace

i32 AnmVariables::integer(i32 value, Rng& rng) {
    (void)rng;
    // A masked argument is a variable reference. th15 resolves several special
    // ids (10016..10032) that need camera/RNG state; those are not available
    // here, so they fall back to 0 rather than producing a wrong number.
    if (value >= 0 && u32(value) < ANM_INTEGER_COUNT) return integers[u32(value)];
    return 0;
}

float AnmVariables::floating(float value, Rng& rng) {
    (void)rng;
    const i32 id = i32(value);
    if (id >= 0 && u32(id) < ANM_FLOAT_COUNT) return floats[u32(id)];
    return 0.0f;
}

bool AnmInterpreter::bind(const AnmResource* resource, const std::vector<u8>* script,
                          u32 code_base) {
    resource_ = resource;
    script_ = script;
    code_base_ = code_base;
    instruction_offset_ = 0;
    timer_ = 0.0f;
    saved_timer_ = 0.0f;
    saved_offset_ = 0;
    variables_ = AnmVariables{};
    visual_ = AnmVisual{};
    stats_ = AnmRunStats{};
    error_.clear();
    return script != nullptr && script->size() >= ANM_HEADER;
}

bool AnmInterpreter::jump(u32 offset, i32 time) {
    if (!script_) return false;
    // MEASURED: the jump operand is a byte offset from the START OF THE SCRIPT,
    // not from the current instruction and not an absolute file offset.
    // Across 260 jumps in the shipped data, `script_offset + operand` lands on
    // an instruction boundary 92.7% of the time, while `instruction + operand`
    // manages only 3.5% and an absolute interpretation 0%.
    //
    // `code_base_` is the script's file offset, so `offset` here is already the
    // buffer index -- do NOT subtract again.
    const u32 index = offset;
    if (!span(index, ANM_HEADER, u32(script_->size()))) return false;
    instruction_offset_ = i32(index);
    timer_ = float(time);
    return true;
}

AnmRunResult AnmInterpreter::tick(Rng& rng, float rate) {
    if (!script_) {
        error_ = "not bound";
        return AnmRunResult::Fault;
    }
    const u32 size = u32(script_->size());
    const u8* base = script_->data();

    auto fail = [&](const char* reason) {
        error_ = reason;
        ++stats_.faults;
        return AnmRunResult::Fault;
    };

    // Bounded so a malformed loop cannot hang the caller.
    for (u32 steps = 0; steps < 100000; ++steps) {
        // Reaching the end of the sliced buffer is a normal termination: the
        // 0xFFFF marker is not part of `bytes`, so the offset lands exactly at
        // the end after the last real instruction.
        if (instruction_offset_ < 0) {
            return AnmRunResult::Finished;
        }
        if (u32(instruction_offset_) >= size) {
            visual_.flags &= ~1u;
            return AnmRunResult::Finished;
        }
        if (!span(u32(instruction_offset_), ANM_HEADER, size)) {
            return fail("instruction outside script");
        }

        const u8* p = base + instruction_offset_;
        const i16 opcode = read_i16(p);
        const u16 length = read_u16(p + 2);
        const i16 time = read_i16(p + 4);
        const u16 mask = read_u16(p + 6);

        // Not due yet: advance the clock and return.
        if (time > i16(timer_)) {
            timer_ += rate;
            return AnmRunResult::Running;
        }

        // 0xFFFF is the end-of-script marker. thtk treats it as a terminator
        // and the loader stops there, so the interpreter must too -- otherwise
        // the next read lands past the end of the sliced buffer and reports
        // "instruction outside script".
        if (opcode == -1 || opcode == i16(0xFFFF) || opcode == 1) {
            visual_.flags &= ~1u;
            instruction_offset_ = -1;
            return AnmRunResult::Finished;
        }
        if (opcode == 2) {
            instruction_offset_ = -1;
            return AnmRunResult::Stopped;
        }

        if (length < ANM_HEADER || (length % 4) != 0 ||
            !span(u32(instruction_offset_), length, size)) {
            return fail("invalid instruction length");
        }
        const u32 needed = argument_count(opcode);
        if (ANM_HEADER + needed * ANM_ARG > length) {
            return fail("truncated arguments");
        }

        ++stats_.instructions;
        auto arg = [&](u32 n) { return p + ANM_HEADER + n * ANM_ARG; };
        // Masked arguments are variable references.
        auto integer = [&](u32 n) -> i32 {
            const i32 raw = read_i32(arg(n));
            return (mask & (1u << n)) ? variables_.integer(raw, rng) : raw;
        };
        auto floating = [&](u32 n) -> float {
            const float raw = read_f32(arg(n));
            return (mask & (1u << n)) ? variables_.floating(raw, rng) : raw;
        };

        switch (opcode) {
        case 0:
        case 5:
            // Opcode 5 is a label; falling through it is a no-op.
            break;

        case 3:
        case 4:
            if (opcode == 4) {
                visual_.flags &= ~1u;
            }
            timer_ -= rate;
            break;

        case 6: {
            // Subtract a frame count from the timer.
            const float frames = float(integer(0));
            timer_ -= frames;
            break;
        }

        case 7:
            timer_ = saved_timer_;
            instruction_offset_ = saved_offset_;
            continue;

        case 122: {
            const u32 bound = u32(integer(1));
            variables_.integers[0] = bound ? signed_bits(rng.next32() % bound) : 0;
            break;
        }

        case 123: {
            const float maximum = floating(1);
            variables_.floats[0] = float(rng.unit() * maximum);
            break;
        }

        case 124:
        case 125:
        case 126:
        case 127:
        case 128: {
            const double angle = double(floating(1));
            float value = 0.0f;
            if (opcode == 124) value = float(std::sin(angle));
            else if (opcode == 125) value = float(std::cos(angle));
            else if (opcode == 126) value = float(std::tan(angle));
            else if (opcode == 127) value = float(std::asin(angle));
            else value = float(std::acos(angle));
            variables_.floats[0] = value;
            break;
        }

        case 129:
            variables_.floats[0] = normalize_angle(floating(0));
            break;

        case 130: {
            const float radius = floating(3);
            const float angle = floating(2);
            variables_.floats[0] = float(std::cos(double(angle)) * double(radius));
            variables_.floats[1] = float(std::sin(double(angle)) * double(radius));
            break;
        }

        case 131: {
            const float minimum = floating(2);
            const float maximum = floating(3);
            const float angle = float(rng.signed_unit() * 3.1415927410125732421875f);
            const float radius = float(rng.signed_unit() * float(maximum - minimum)) + minimum;
            variables_.floats[0] = float(std::cos(double(angle)) * double(radius));
            variables_.floats[1] = float(std::sin(double(angle)) * double(radius));
            break;
        }

        case 200:
            if (!jump(read_u32(arg(0)), read_i32(arg(1)))) {
                return fail("invalid jump");
            }
            continue;

        case 201: {
            i32& counter = variables_.integers[0];
            counter = i32(u32(counter) - 1u);
            if (integer(0) > 0) {
                if (!jump(read_u32(arg(1)), read_i32(arg(2)))) {
                    return fail("invalid loop jump");
                }
                continue;
            }
            break;
        }

        case 300: {
            visual_.flags |= 1u;
            i32 index = integer(0);
            // A NEGATIVE index is not an error: th15 maps it to the
            // environment's fallback sprite (index 258). We have no
            // environment, so record the marker and leave the size alone
            // instead of reporting a fault -- failing here would reject a
            // perfectly valid instruction.
            if (index < 0) {
                visual_.sprite = -1;
                break;
            }
            visual_.sprite = index;
            if (resource_) {
                if (u32(index) >= resource_->sprites.size()) {
                    // Sprite indices are GLOBAL across the archive, not
                    // per-file: abcard.anm declares 30 sprites yet its
                    // scripts reference index 141. Treating that as a fault
                    // rejects a valid instruction, so record it as an
                    // unresolved external reference instead.
                    ++stats_.external_sprites;
                    break;
                }
                const AnmSprite& sprite = resource_->sprites[u32(index)];
                visual_.size = Vec2{sprite.w, sprite.h};
            }
            break;
        }

        case 301: {
            visual_.flags |= 1u;
            const i32 first = integer(0);
            const u32 count = u32(integer(1));
            if (count == 0) return fail("sprite range is zero");
            const i32 index = first + signed_bits(rng.next32() % count);
            if (index < 0) {
                visual_.sprite = -1;
                break;
            }
            visual_.sprite = index;
            if (resource_) {
                if (u32(index) >= resource_->sprites.size()) {
                    ++stats_.external_sprites;
                    break;
                }
                const AnmSprite& sprite = resource_->sprites[u32(index)];
                visual_.size = Vec2{sprite.w, sprite.h};
            }
            break;
        }

        case 304:
            visual_.layer = integer(0);
            break;
        case 310:
            visual_.flags = (visual_.flags & ~1u) | (u32(integer(0)) & 1u);
            break;
        case 316:
            visual_.flags |= 2u;
            break;
        case 317:
            visual_.flags &= ~2u;
            break;

        case 400:
            variables_.position = Vec3{};
            break;
        case 401:
            variables_.rotation = Vec3{floating(0), floating(1), floating(2)};
            visual_.flags |= 4u;
            break;
        case 402:
            visual_.scale = Vec2{floating(0), floating(1)};
            visual_.flags |= 8u;
            break;
        case 403:
            visual_.color = (visual_.color & 0xFFFFFFu) |
                            (u32(u8(integer(0))) << 24);
            break;
        case 404:
            // Set RGB from three floats, alpha unchanged.
            visual_.color = (visual_.color & 0xFF000000u) |
                            ((u32(u8(floating(0) * 255.0f)) << 16) |
                             (u32(u8(floating(1) * 255.0f)) << 8) |
                             u32(u8(floating(2) * 255.0f)));
            break;
        case 405:
            visual_.secondary_color = (visual_.secondary_color & 0xFFFFFFu) |
                                      (u32(u8(integer(0))) << 24);
            break;
        case 406:
            visual_.secondary_color = (visual_.secondary_color & 0xFF000000u) |
                                      ((u32(u8(floating(0) * 255.0f)) << 16) |
                                       (u32(u8(floating(1) * 255.0f)) << 8) |
                                       u32(u8(floating(2) * 255.0f)));
            break;

        case 422:
            variables_.position = Vec3{};
            break;
        case 429:
            visual_.uv_scale = Vec2{floating(0), floating(1)};
            visual_.flags |= 0x10u;
            break;
        case 434:
            visual_.scale = Vec2{floating(0), floating(1)};
            visual_.flags |= 8u;
            break;
        case 436:
            visual_.size = Vec2{floating(0), floating(1)};
            break;

        case 509:
            // Copy variables from the creation parent. Without a parent this
            // is a no-op, which is the correct behaviour for a root script.
            break;

        default:
            ++stats_.unsupported;
            if (stats_.first_unsupported == 0) {
                stats_.first_unsupported = u16(opcode);
            }
            error_ = "unimplemented ANM opcode";
            return AnmRunResult::Unsupported;
        }

        instruction_offset_ += i32(length);
    }

    return fail("instruction budget exceeded");
}

}  // namespace th185
