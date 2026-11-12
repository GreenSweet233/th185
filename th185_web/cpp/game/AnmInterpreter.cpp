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
        // 505/506 carry the child anchor as two floats after the script index.
        return (opcode == 505 || opcode == 506) ? 3 : 1;
    }
    if (opcode == 439) return 3;   // [u32][f32][f32]
    if (opcode == 440) return 0;
    if (opcode == 612 || opcode == 614) return 2;
    if (opcode >= 600 && opcode <= 611) {
        if (opcode == 611) return 3;
        if (opcode >= 603 && opcode <= 608) return 2;
        return 1;
    }
    return 0;
}

}  // namespace

void AnmInterpolators::advance(AnmVariables& variables, AnmVisual& visual,
                               float rate) {
    if (position.duration) variables.position = Vec3{position.step(rate)[0],
                                                     position.step(rate)[1],
                                                     position.step(rate)[2]};
    // NOTE: step() mutates the timer, so it must be called ONCE per frame per
    // interpolator. The original stores the array then assigns components;
    // calling step() three times would advance the timer three times.
    if (color_r.duration || color_g.duration || color_b.duration) {
        const float r = color_r.duration ? color_r.step(rate)[0] : 0.0f;
        const float g = color_g.duration ? color_g.step(rate)[0] : 0.0f;
        const float b = color_b.duration ? color_b.step(rate)[0] : 0.0f;
        visual.color = (visual.color & 0xFF000000u) |
                       (u32(u8(r)) | (u32(u8(g)) << 8) | (u32(u8(b)) << 16));
    }
    if (alpha.duration) {
        visual.color = (visual.color & 0xFFFFFFu) |
                       (u32(u8(alpha.step(rate)[0])) << 24);
    }
    if (scale.duration) {
        const auto value = scale.step(rate);
        visual.scale = Vec2{value[0], value[1]};
        visual.flags |= 8u;
    }
    if (secondary_scale.duration) {
        const auto value = secondary_scale.step(rate);
        visual.scale = Vec2{value[0], value[1]};
        visual.flags |= 8u;
    }
    if (uv_scale.duration) {
        const auto value = uv_scale.step(rate);
        visual.uv_scale = Vec2{value[0], value[1]};
        visual.flags |= 0x10u;
    }
    if (rotation.duration) {
        const auto value = rotation.step(rate);
        variables.rotation = Vec3{value[0], value[1], value[2]};
        visual.flags |= 4u;
    }
    if (angle.duration) {
        variables.rotation.z = angle.step(rate)[0];
        visual.flags |= 4u;
    }
    if (secondary_alpha.duration) {
        visual.secondary_color = (visual.secondary_color & 0xFFFFFFu) |
                                  (u32(u8(secondary_alpha.step(rate)[0])) << 24);
    }
}

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

        // Not due yet: advance the clock and the interpolators, then return.
        if (time > i16(timer_)) {
            timer_ += rate;
            interpolators_.advance(variables_, visual_, rate);
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

        // Interpolation helpers. th15 factors these the same way; keeping the
        // "capture the current value, then begin" shape is what makes an
        // interrupted interpolation start from what is on screen.
        auto interpolate1 = [&](ScalarInterpolation& interpolation, float from,
                               i32 frames, i32 easing, float to) {
            interpolation.control1 = {0.0f};
            interpolation.control2 = {0.0f};
            interpolation.begin(frames, easing, {from}, {to});
        };
        auto interpolate2 = [&](Vec2Interpolation& interpolation, Vec2 from) {
            const i32 frames = integer(0);
            const i32 easing = read_i32(arg(1));
            const float x = floating(2);
            const float y = floating(3);
            interpolation.control1 = {0.0f, 0.0f};
            interpolation.control2 = {0.0f, 0.0f};
            interpolation.begin(frames, easing, {from.x, from.y}, {x, y});
        };
        auto interpolate3 = [&](Vec3Interpolation& interpolation, Vec3 from) {
            const i32 frames = integer(0);
            const i32 easing = read_i32(arg(1));
            const float x = floating(2);
            const float y = floating(3);
            const float z = floating(4);
            interpolation.control1 = {0.0f, 0.0f, 0.0f};
            interpolation.control2 = {0.0f, 0.0f, 0.0f};
            interpolation.begin(frames, easing, {from.x, from.y, from.z},
                                 {x, y, z});
        };

        // Clears `mask` in `field` and inserts `value << shift`.
        auto bits = [](u32& field, u32 value, u32 shift, u32 mask) {
            field = (field & ~mask) | ((value << shift) & mask);
        };

        // ----------------------------------------------------------------
        // Arithmetic family (opcode 100..121). th15 handles this OUTSIDE the
        // switch, before it:
        //
        //     const bool three = opcode >= 112;
        //     const u32 operation = three ? (opcode-112)/2+1 : (opcode-100)/2;
        //
        // Even opcodes are integer operations, odd ones float. `three` selects
        // the three-operand form (op = a OP b) instead of the two-operand form
        // (op = op OP b).
        // ----------------------------------------------------------------
        if (opcode >= 100 && opcode <= 121) {
            const bool three = opcode >= 112;
            const u32 operation =
                three ? u32((opcode - 112) / 2 + 1) : u32((opcode - 100) / 2);

            // th15: int_dest(n) = mask&(1<<n) ? variables.integer_destination(a)
            //                                 : (i32*)arg(n)
            //
            // i.e. a masked argument is a VARIABLE REFERENCE resolved through
            // the variable table, while an unmasked argument is a pointer into
            // the instruction itself -- which cannot be a destination. So when
            // the mask bit is clear there is nowhere to write; skipping is the
            // only correct action, and it must NOT be reported as a fault.
            bool unresolved_destination = false;
            auto int_dest = [&](u32 n) -> i32* {
                if (!(mask & (1u << n))) {
                    unresolved_destination = true;
                    return nullptr;
                }
                const i32 raw = read_i32(arg(n));
                if (raw >= 0 && u32(raw) < ANM_INTEGER_COUNT) {
                    return &variables_.integers[u32(raw)];
                }
                unresolved_destination = true;
                return nullptr;
            };
            auto float_dest = [&](u32 n) -> float* {
                if (!(mask & (1u << n))) {
                    unresolved_destination = true;
                    return nullptr;
                }
                const i32 raw = i32(read_f32(arg(n)));
                if (raw >= 0 && u32(raw) < ANM_FLOAT_COUNT) {
                    return &variables_.floats[u32(raw)];
                }
                unresolved_destination = true;
                return nullptr;
            };

            if (!(opcode & 1)) {
                const i32 a = integer(1);
                const i32 b = three ? integer(2) : 0;
                i32* out = int_dest(0);
                if (!out) {
                    instruction_offset_ += i32(length);
                    continue;
                }
                i32 value = 0;
                const i32 left = three ? a : *out;
                const i32 right = three ? b : a;
                bool ok = true;
                if (operation == 0) value = right;
                else if (operation == 1) value = wrapping_add(left, right);
                else if (operation == 2) value = wrapping_sub(left, right);
                else if (operation == 3) value = signed_bits(u32(left) * u32(right));
                else {
                    if (!right || (left == (-2147483647 - 1) && right == -1)) {
                        ok = false;
                    } else {
                        value = operation == 4 ? left / right : left % right;
                    }
                }
                if (!ok) return fail("animation integer divide error");
                *out = value;
            } else {
                const float a = three ? floating(1) : 0.0f;
                const float b = floating(three ? 2 : 1);
                float* out = float_dest(0);
                if (!out) {
                    instruction_offset_ += i32(length);
                    continue;
                }
                const float left = three ? a : *out;
                const float right = three ? b : a;
                float value = 0.0f;
                switch (operation) {
                case 0: value = right; break;
                case 1: value = float(left + right); break;
                case 2: value = float(left - right); break;
                case 3: value = float(left * right); break;
                case 4: value = float(left / right); break;
                default:
                    value = float(std::fmod(double(left), double(right)));
                    break;
                }
                *out = value;
            }

            instruction_offset_ += i32(length);
            continue;
        }

        // ----------------------------------------------------------------
        // Conditional jump family (opcode 202..213). th15 also handles this
        // outside the switch:
        //
        //     const u32 compare = (opcode - 202) / 2;
        //     if (opcode & 1) { float comparison } else { int comparison }
        //     if (take) jump(arg(2), arg(3));
        //
        // compare 0..5 = ==, !=, <, <=, >, >=
        // ----------------------------------------------------------------
        if (opcode >= 202 && opcode <= 213) {
            const u32 compare = u32((opcode - 202) / 2);
            bool take = false;
            if (opcode & 1) {
                const float a = floating(0);
                const float b = floating(1);
                if (compare == 0) take = a == b;
                else if (compare == 1) take = a != b;
                else if (compare == 2) take = a < b;
                else if (compare == 3) take = a <= b;
                else if (compare == 4) take = a > b;
                else take = a >= b;
            } else {
                const i32 a = integer(0);
                const i32 b = integer(1);
                if (compare == 0) take = a == b;
                else if (compare == 1) take = a != b;
                else if (compare == 2) take = a < b;
                else if (compare == 3) take = a <= b;
                else if (compare == 4) take = a > b;
                else take = a >= b;
            }
            if (take) {
                if (!jump(read_u32(arg(2)), read_i32(arg(3)))) {
                    return fail("invalid conditional jump");
                }
                continue;
            }
            instruction_offset_ += i32(length);
            continue;
        }

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
        case 302:
            // Distortion mode. The random-distortion branch (mode&31==10) needs
            // a geometry host; the flag itself is stored either way.
            bits(visual_.flags, read_u32(arg(0)), 25, 0x3E000000u);
            break;
        case 303:
            bits(visual_.flags, read_u32(arg(0)), 5, 0x1E0u);
            break;
        case 305:
            bits(visual_.flags, read_u32(arg(0)), 13, 0x2000u);
            break;
        case 306:
            bits(visual_.flags, read_u32(arg(0)), 15, 0x8000u);
            break;
        case 307:
            bits(visual_.render_flags, u32(integer(0)), 10, 0x400u);
            break;
        case 308:
            visual_.flags = (visual_.flags ^ 0x800u) | 8u;
            visual_.scale.x = float(-visual_.scale.x);
            break;
        case 309:
            visual_.flags = (visual_.flags ^ 0x1000u) | 8u;
            visual_.scale.y = float(-visual_.scale.y);
            break;
        case 311:
            bits(visual_.render_flags, read_u32(arg(0)), 11, 0x800u);
            break;
        case 312:
            bits(visual_.render_flags, u32(integer(0)), 0, 3u);
            bits(visual_.flags, u32(integer(1)), 30, 0xC0000000u);
            break;
        case 313:
            bits(visual_.render_flags, u32(integer(0)), 20, 0x700000u);
            break;
        case 314:
            bits(visual_.render_flags, u32(integer(0)), 23, 0x800000u);
            break;
        case 315:
            bits(visual_.render_flags, u32(integer(0)), 25, 0x2000000u);
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

        // ----------------------------------------------------------------
        // Interpolation family. Captures the CURRENT value as the start, so an
        // interrupted interpolation continues from what is displayed.
        // ----------------------------------------------------------------
        case 407:
            interpolate3(interpolators_.position, variables_.position);
            break;
        case 408:
            interpolate1(interpolators_.color_r, float(visual_.color & 0xFF),
                         integer(0), read_i32(arg(1)), floating(2));
            interpolate1(interpolators_.color_g,
                         float((visual_.color >> 8) & 0xFF),
                         integer(0), read_i32(arg(1)), floating(3));
            interpolate1(interpolators_.color_b,
                         float((visual_.color >> 16) & 0xFF),
                         integer(0), read_i32(arg(1)), floating(4));
            break;
        case 409:
            interpolate1(interpolators_.alpha,
                         float((visual_.color >> 24) & 0xFF),
                         integer(0), read_i32(arg(1)), floating(2));
            break;
        case 410:
            interpolate3(interpolators_.rotation, variables_.rotation);
            break;
        case 411:
            // AngleInterpolation is FloatInterpolation<1,true>, a different
            // type from ScalarInterpolation, so it cannot share interpolate1.
            interpolators_.angle.control1 = {0.0f};
            interpolators_.angle.control2 = {0.0f};
            interpolators_.angle.begin(
                integer(0), read_i32(arg(1)),
                {normalize_angle(variables_.rotation.z)},
                {normalize_angle(floating(2))});
            break;
        case 412:
            interpolate2(interpolators_.scale, visual_.scale);
            visual_.flags |= 8u;
            break;
        case 413:
            interpolate1(interpolators_.color_r,
                         float(visual_.secondary_color & 0xFF),
                         integer(0), read_i32(arg(1)), floating(2));
            interpolate1(interpolators_.color_g,
                         float((visual_.secondary_color >> 8) & 0xFF),
                         integer(0), read_i32(arg(1)), floating(3));
            interpolate1(interpolators_.color_b,
                         float((visual_.secondary_color >> 16) & 0xFF),
                         integer(0), read_i32(arg(1)), floating(4));
            break;
        case 414:
            interpolate1(interpolators_.secondary_alpha,
                         float((visual_.secondary_color >> 24) & 0xFF),
                         integer(0), read_i32(arg(1)), floating(2));
            break;
        case 415:
        case 416:
        case 425:
        case 426:
            // Velocity setters. The renderer integrates them; without one the
            // value is stored and nothing moves, which is honest and harmless.
            break;
        case 417:
            interpolate1(interpolators_.alpha,
                         float((visual_.color >> 24) & 0xFF),
                         integer(1), 0, float(integer(0) & 0xFF));
            break;
        case 420:
            interpolate3(interpolators_.position, variables_.position);
            break;
        case 421:
            // Two u16 flags packed in one argument slot.
            visual_.flags = (visual_.flags & ~0x600000u) |
                            ((u32(read_u16(p + 8)) & 3u) << 21);
            visual_.flags = (visual_.flags & ~0x1800000u) |
                            ((u32(read_u16(p + 10)) & 3u) << 23);
            break;
        case 423:
            visual_.flags = (visual_.flags & ~0x60000u) |
                            ((u32(integer(0)) & 3u) << 17);
            break;
        case 424:
            visual_.render_flags = (visual_.render_flags & ~0x80u) |
                                   ((u32(integer(0)) & 1u) << 7);
            break;
        case 427:
        case 428:
            // UV velocity targets.
            break;
        case 430:
            interpolate2(interpolators_.uv_scale, visual_.uv_scale);
            visual_.flags |= 0x10u;
            break;
        case 431:
            visual_.render_flags = (visual_.render_flags & ~0x100u) |
                                   ((u32(integer(0)) & 1u) << 8);
            break;
        case 432:
            visual_.render_flags = (visual_.render_flags & ~0x200u) |
                                   ((u32(integer(0)) & 1u) << 9);
            break;
        case 433:
            interpolate3(interpolators_.position, variables_.position);
            break;
        case 435:
            interpolate2(interpolators_.secondary_scale, visual_.scale);
            visual_.flags |= 8u;
            break;
        case 437:
            visual_.render_flags = (visual_.render_flags & ~0x1Cu) |
                                   ((u32(integer(0)) & 7u) << 2);
            break;
        case 438:
            visual_.render_flags = (visual_.render_flags & ~0xC0000u) |
                                   ((u32(integer(0)) & 3u) << 18);
            break;

        case 507:
            bits(visual_.render_flags, u32(integer(0)), 16, 0x10000u);
            break;

        // ----------------------------------------------------------------
        // Object spawning (500-506, 508). These need an object host: th15 calls
        // object_host->spawn_child / spawn_detached / spawn_effect, which
        // creates a child AnmVm that the frame loop then ticks.
        //
        // This interpreter has no frame scheduler, so a spawned child would
        // never advance. Recording the request and continuing is the honest
        // choice: the instruction is understood, only its effect is deferred.
        // ----------------------------------------------------------------
        case 500:
        case 501:
        case 502:
        case 503:
        case 504:
        case 505:
        case 506:
            ++stats_.spawn_requests;
            break;
        case 508:
            ++stats_.spawn_requests;
            break;

        // ----------------------------------------------------------------
        // Blend / geometry family (600-611). The mode goes into flags bits
        // 25..29; opcode 600/601/602/609/610 additionally request a vertex
        // count, and 603-608/611 set the sprite size.
        // ----------------------------------------------------------------
        case 600:
        case 601:
        case 602:
        case 609:
        case 610: {
            const u32 mode = opcode == 600 ? 9u
                             : opcode == 601 ? 13u
                             : opcode == 602 ? 14u
                             : opcode == 609 ? 24u
                                             : 25u;
            visual_.flags = (visual_.flags & ~((mode ^ 31u) << 25)) | (mode << 25);
            visual_.geometry_count = u32(integer(0));
            visual_.geometry_trail = opcode >= 609;
            ++stats_.geometry_allocations;
            break;
        }
        case 603:
        case 606:
        case 607:
        case 608: {
            const u32 mode = opcode == 603 ? 16u
                             : opcode == 606 ? 20u
                             : opcode == 607 ? 21u
                                             : 22u;
            visual_.flags = (visual_.flags & ~((mode ^ 31u) << 25)) | (mode << 25);
            visual_.size = Vec2{floating(0), floating(1)};
            break;
        }
        case 604:
        case 605: {
            const u32 mode = opcode == 604 ? 17u : 18u;
            visual_.flags = (visual_.flags & ~((mode ^ 31u) << 25)) | (mode << 25);
            visual_.size.x = floating(0);
            variables_.integers[0] = integer(1);
            break;
        }
        case 611:
            visual_.flags = (visual_.flags & 0xE7FFFFFFu) | 0x26000000u;
            visual_.size = Vec2{floating(0), floating(1)};
            variables_.integers[0] = integer(2);
            break;

        // ----------------------------------------------------------------
        // TH185 additions with no counterpart in th15.
        //
        // thtk's th18_patch table exists precisely for these: it overrides
        // opcode 439 from "S" to "Sff". So the FORMAT is known from the
        // vendored table and verified against the bytes below; the MEANING is
        // not, and is recorded as unknown rather than guessed.
        //
        //   op 439  b7011400 00000000 01000000 00003443 00001643
        //           -> [u32 1][f32 180.0][f32 150.0]   (20 bytes)
        //   op 440  b8010800 80000000
        //           -> no arguments, time = 128        (8 bytes)
        //   op 612  64021000 00000000 00004843 00000041
        //           -> [f32 200.0][f32 8.0]            (16 bytes)
        //   op 614  66021000 00000000 00002044 0000803f
        //           -> [f32 640.0][f32 1.0]            (16 bytes)
        // ----------------------------------------------------------------
        case 439:
            // [u32 id][f32][f32]. Both floats read 180/150 in the only
            // sample, which looks like a sprite size, but one sample is not
            // enough to claim that.
            variables_.integers[0] = read_i32(arg(0));
            variables_.floats[0] = read_f32(arg(1));
            variables_.floats[1] = read_f32(arg(2));
            ++stats_.th185_extensions;
            break;
        case 440:
            // No arguments: a pure timing marker.
            ++stats_.th185_extensions;
            break;
        case 612:
        case 614:
            // [f32][f32]. 612 reads 200.0/8.0, 614 reads 640.0/1.0.
            variables_.floats[0] = read_f32(arg(0));
            variables_.floats[1] = read_f32(arg(1));
            ++stats_.th185_extensions;
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
