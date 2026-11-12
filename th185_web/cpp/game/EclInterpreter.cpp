#include "EclInterpreter.hpp"

#include <cmath>
#include <cstring>

namespace th185 {

namespace {

u16 read_u16(const u8* p) noexcept { return u16(p[0] | (u16(p[1]) << 8)); }

u32 read_u32(const u8* p) noexcept {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

i32 as_int(u32 bits) noexcept { return signed_bits(bits); }
float as_float(u32 bits) noexcept { return bits_float(bits); }

// TH11 normalizes angles into (-pi, pi] with a hard iteration cap. The cap
// matters: without it a corrupt value loops forever.
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

}  // namespace

// --- EclStack ---

bool EclStack::push(EclValue::Type type, u32 bits) {
    if (depth_ >= CAPACITY) return false;
    slots_[depth_].type = type;
    slots_[depth_].bits = bits;
    ++depth_;
    return true;
}

bool EclStack::pop_raw(u32& out) {
    if (depth_ == 0) return false;
    --depth_;
    out = slots_[depth_].bits;
    return true;
}

bool EclStack::pop(u32& out, EclValue::Type& type) {
    if (depth_ == 0) return false;
    --depth_;
    out = slots_[depth_].bits;
    type = slots_[depth_].type;
    return true;
}

bool EclStack::enter_frame(u32 slots) {
    if (depth_ + slots > CAPACITY) return false;
    frames_.push_back(depth_);
    depth_ += slots;
    return true;
}

bool EclStack::leave_frame() {
    if (frames_.empty()) return false;
    depth_ = frames_.back();
    frames_.pop_back();
    return true;
}

// --- Opcode table ---

namespace {

struct OpName {
    u16 opcode;
    const char* name;
};

const OpName kOpNames[] = {
    {0, "Nop"}, {1, "Delete"}, {10, "Return"}, {11, "Call"}, {12, "Jump"},
    {13, "JumpIfZero"}, {14, "JumpIfNonzero"}, {15, "Spawn"},
    {16, "SpawnNamed"}, {17, "CancelThread"}, {18, "SetThreadFlag"},
    {19, "ClearThreadFlag"}, {20, "SetThreadState"}, {21, "CancelSecondary"},
    {30, "Diagnostic"}, {40, "EnterFrame"}, {41, "LeaveFrame"},
    {42, "PushInt"}, {43, "StoreInt"}, {44, "PushFloat"}, {45, "StoreFloat"},
    {50, "AddInt"}, {51, "AddFloat"}, {52, "SubtractInt"}, {53, "SubtractFloat"},
    {54, "MultiplyInt"}, {55, "MultiplyFloat"}, {56, "DivideInt"},
    {57, "DivideFloat"}, {58, "Remainder"}, {59, "EqualInt"}, {60, "EqualFloat"},
    {61, "NotEqualInt"}, {62, "NotEqualFloat"}, {63, "LessInt"}, {64, "LessFloat"},
    {65, "LessEqualInt"}, {66, "LessEqualFloat"}, {67, "GreaterInt"},
    {68, "GreaterFloat"}, {69, "GreaterEqualInt"}, {70, "GreaterEqualFloat"},
    {71, "NotInt"}, {72, "NotFloat"}, {73, "LogicalOr"}, {74, "LogicalAnd"},
    {75, "Xor"}, {76, "Or"}, {77, "And"}, {78, "PostDecrement"}, {79, "Sin"},
    {80, "Cos"}, {81, "Polar"}, {82, "NormalizeAngle"}, {83, "Wait"},
    {84, "NegateInt"}, {85, "NegateFloatBits"}, {86, "LengthSquared"},
    {87, "AngleTo"}, {88, "Sqrt"}, {23, "Op23Timer"}, {1013, "Op1013Schedule"},
};

}  // namespace

const char* EclInterpreter::opcode_name(u16 opcode) {
    for (const OpName& entry : kOpNames) {
        if (entry.opcode == opcode) return entry.name;
    }
    return "";
}

bool EclInterpreter::is_supported(u16 opcode) {
    return opcode_name(opcode)[0] != 0;
}

// --- Interpreter ---

namespace {

// Instruction header layout, confirmed in docs/ECL.md:
//   u16 id, u16 size, u16 rank, u16 argc, then payload[size - 8].
constexpr u32 INSTRUCTION_HEADER = 8;

// IMPORTANT: payload[0] is a reserved word that is always zero in every
// observed instruction (checked across all 23770 of them). The real arguments
// start at payload + 4. Reading payload[0] as an argument yields a jump target
// of 0 for every branch -- which is exactly the false signal that made the
// first version of the verifier report 2220 bad jump targets.
constexpr u32 ARGUMENT_BIAS = 4;

u32 arg_u32(const u8* payload, u32 index) {
    return read_u32(payload + ARGUMENT_BIAS + index * 4);
}

// Call (op 11) payload layout, verified on all 364 occurrences:
//   u32 prefix_length;     // extra bytes before the name (0/8/16/24)
//   u32 name_length;       // length of the name region including NUL and pad
//   u8  prefix[prefix_length];
//   char name[];           // NUL-terminated, starts at 8 + prefix_length
//   then zero or more argument descriptors: u16 tag, u16 reserved, u32 value
//   where tag 0x6666 = float and 0x6969 = int.
//
// The first version assumed the name starts at payload[0] offset 8 and got
// 191/364. Reading the prefix length from payload[0] and starting the name at
// 8 + prefix_length gets 364/364 -- which is why this is parsed rather than
// guessed.
struct CallTarget {
    const char* name = nullptr;
    u32 name_length = 0;
    u32 prefix_length = 0;
    bool valid = false;
};

CallTarget parse_call(const u8* payload, u32 payload_size) {
    CallTarget target;
    if (payload_size < 8) return target;
    const u32 prefix_length = read_u32(payload);
    const u32 name_length = read_u32(payload + 4);
    const u32 start = 8 + prefix_length;
    if (start + 2 > payload_size) return target;
    if (name_length < 2 || start + name_length > payload_size) return target;
    const char* name = reinterpret_cast<const char*>(payload + start);
    // Must be NUL-terminated inside the declared region.
    u32 nul = 0;
    while (nul < name_length && name[nul] != 0) ++nul;
    if (nul == 0 || nul >= name_length) return target;
    for (u32 i = 0; i < nul; ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (ch < 32 || ch > 126) return target;
    }
    target.name = name;
    target.name_length = nul;
    target.prefix_length = prefix_length;
    target.valid = true;
    return target;
}

i32 arg_i32(const u8* payload, u32 index) { return as_int(arg_u32(payload, index)); }

float arg_float(const u8* payload, u32 index) {
    return as_float(arg_u32(payload, index));
}

}  // namespace

EclRunResult EclInterpreter::run(const std::vector<u8>& code, u32 step_budget) {
    stats_ = EclRunStats{};
    stack_ = EclStack{};

    u32 pc = 0;
    while (pc + INSTRUCTION_HEADER <= code.size()) {
        if (step_budget != 0 && stats_.instructions >= step_budget) {
            return EclRunResult::Finished;
        }

        const u8* instruction = code.data() + pc;
        const u16 opcode = read_u16(instruction);
        const u16 size = read_u16(instruction + 2);
        // rank_mask at +4, param_mask at +6. The low 8 bits of param_mask flag
        // which arguments are references rather than literals.
        const u16 param_mask = read_u16(instruction + 6);
        (void)param_mask;  // reserved for opcodes with mixed literal/reference args
        ++stats_.instructions;
        if (stack_.depth() > stats_.max_stack_depth) {
            stats_.max_stack_depth = stack_.depth();
        }

        if (size < INSTRUCTION_HEADER || pc + size > code.size()) {
            ++stats_.fault_count;
            return EclRunResult::Fault;
        }
        const u8* payload = instruction + INSTRUCTION_HEADER;

        if (!is_supported(opcode)) {
            ++stats_.unsupported_count;
            if (stats_.first_unsupported_opcode == 0) {
                stats_.first_unsupported_opcode = opcode;
            }
            // Do NOT skip and continue: an unimplemented opcode usually has
            // side effects on the stack, so continuing would produce garbage
            // that looks like a valid result.
            return EclRunResult::Unsupported;
        }

        bool advanced = false;
        switch (EclOp(opcode)) {
        case EclOp::Nop:
        case EclOp::Diagnostic:
        case EclOp::Wait:
            break;

        case EclOp::Delete:
            return EclRunResult::Finished;

        case EclOp::Return: {
            if (!stack_.leave_frame()) return EclRunResult::Returned;
            break;
        }

        case EclOp::Jump:
        case EclOp::JumpIfZero:
        case EclOp::JumpIfNonzero: {
            bool take = true;
            if (opcode != u16(EclOp::Jump)) {
                u32 bits = 0;
                EclValue::Type type = EclValue::Type::Integer;
                if (!stack_.pop(bits, type)) {
                    ++stats_.fault_count;
                    return EclRunResult::Fault;
                }
                const bool zero = (type == EclValue::Type::Float)
                                       ? (as_float(bits) == 0.0f)
                                       : (as_int(bits) == 0);
                take = (zero == (opcode == u16(EclOp::JumpIfZero)));
            }
            if (take) {
                const i32 target = arg_i32(payload, 0);
                if (target < 0 || u32(target) + INSTRUCTION_HEADER > code.size()) {
                    ++stats_.fault_count;
                    return EclRunResult::Fault;
                }
                pc = u32(target);
                advanced = true;
            }
            break;
        }

        case EclOp::EnterFrame: {
            if (!stack_.enter_frame(u32(arg_i32(payload, 0)))) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            // Keep the variable frame in step with the operand frame.
            context.push_frame();
            break;
        }

        case EclOp::LeaveFrame:
            if (!stack_.leave_frame()) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            context.pop_frame();
            break;

        case EclOp::PushInt:
            if (!stack_.push(EclValue::Type::Integer, arg_u32(payload, 0))) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;

        case EclOp::PushFloat:
            if (!stack_.push(EclValue::Type::Float, arg_u32(payload, 0))) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;

        case EclOp::StoreInt: {
            u32 bits = 0;
            EclValue::Type type = EclValue::Type::Integer;
            if (!stack_.pop(bits, type)) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const i32 target = arg_i32(payload, 0);
            const i32 value = (type == EclValue::Type::Float)
                                  ? i32(as_float(bits))
                                  : as_int(bits);
            if (!context.write_int_ref(target, value)) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;
        }

        // Opcode 45 was assumed to be StoreFloat because TH11 numbers it that
        // way. The data contradicts that:
        //
        //   op 43 StoreInt   payload[4] decodes as a REFERENCE in 948/948 cases
        //                    (negative -> builtin slot via +10000, else a
        //                    4-aligned frame offset). Clean.
        //   op 45 (TH11 calls it StoreFloat)
        //                    payload[4] decodes as a reference in only 137/445
        //                    cases. The other 308 hold literal floats
        //                    (0x40800000 = 4.0f, 0x41400000 = 12.0f) with a
        //                    0/1 flag in payload[8] -- the SAME shape as op 44
        //                    PushFloat, not the shape of a store.
        //
        // So op 45 in TH185 is very likely NOT StoreFloat. Guessing a target
        // would corrupt an arbitrary variable, which is worse than refusing.
        // Left unsupported until the real semantics are established.
        case EclOp::StoreFloat:
            ++stats_.unsupported_count;
            if (stats_.first_unsupported_opcode == 0) {
                stats_.first_unsupported_opcode = opcode;
            }
            return EclRunResult::Unsupported;
        case EclOp::AddInt:
        case EclOp::SubtractInt:
        case EclOp::MultiplyInt:
        case EclOp::DivideInt:
        case EclOp::Remainder:
        case EclOp::EqualInt:
        case EclOp::NotEqualInt:
        case EclOp::LessInt:
        case EclOp::LessEqualInt:
        case EclOp::GreaterInt:
        case EclOp::GreaterEqualInt:
        case EclOp::LogicalOr:
        case EclOp::LogicalAnd:
        case EclOp::Xor:
        case EclOp::Or:
        case EclOp::And: {
            u32 rb = 0, lb = 0;
            EclValue::Type rt = EclValue::Type::Integer;
            EclValue::Type lt = EclValue::Type::Integer;
            if (!stack_.pop(rb, rt) || !stack_.pop(lb, lt)) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const i32 right = (rt == EclValue::Type::Float) ? i32(as_float(rb)) : as_int(rb);
            const i32 left = (lt == EclValue::Type::Float) ? i32(as_float(lb)) : as_int(lb);
            i32 value = 0;
            switch (EclOp(opcode)) {
            case EclOp::AddInt: value = i32(u32(left) + u32(right)); break;
            case EclOp::SubtractInt: value = i32(u32(left) - u32(right)); break;
            case EclOp::MultiplyInt: value = i32(u32(left) * u32(right)); break;
            case EclOp::DivideInt:
            case EclOp::Remainder:
                if (right == 0) {
                    ++stats_.fault_count;
                    return EclRunResult::Fault;
                }
                value = (opcode == u16(EclOp::DivideInt)) ? (left / right) : (left % right);
                break;
            case EclOp::EqualInt: value = (left == right); break;
            case EclOp::NotEqualInt: value = (left != right); break;
            case EclOp::LessInt: value = (left < right); break;
            case EclOp::LessEqualInt: value = (left <= right); break;
            case EclOp::GreaterInt: value = (left > right); break;
            case EclOp::GreaterEqualInt: value = (left >= right); break;
            case EclOp::LogicalOr: value = (left || right); break;
            case EclOp::LogicalAnd: value = (left && right); break;
            case EclOp::Xor: value = (left ^ right); break;
            case EclOp::Or: value = (left | right); break;
            case EclOp::And: value = (left & right); break;
            default: break;
            }
            if (!stack_.push(EclValue::Type::Integer, u32(value))) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;
        }

        case EclOp::AddFloat:
        case EclOp::SubtractFloat:
        case EclOp::MultiplyFloat:
        case EclOp::DivideFloat: {
            u32 rb = 0, lb = 0;
            EclValue::Type rt = EclValue::Type::Integer;
            EclValue::Type lt = EclValue::Type::Integer;
            if (!stack_.pop(rb, rt) || !stack_.pop(lb, lt)) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const float right = (rt == EclValue::Type::Integer) ? float(as_int(rb)) : as_float(rb);
            const float left = (lt == EclValue::Type::Integer) ? float(as_int(lb)) : as_float(lb);
            float value = 0.0f;
            if (opcode == u16(EclOp::AddFloat)) value = left + right;
            else if (opcode == u16(EclOp::SubtractFloat)) value = left - right;
            else if (opcode == u16(EclOp::MultiplyFloat)) value = left * right;
            else value = (right == 0.0f) ? 0.0f : (left / right);
            if (!stack_.push(EclValue::Type::Float, float_bits(value))) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;
        }

        case EclOp::EqualFloat:
        case EclOp::NotEqualFloat:
        case EclOp::LessFloat:
        case EclOp::LessEqualFloat:
        case EclOp::GreaterFloat:
        case EclOp::GreaterEqualFloat: {
            u32 rb = 0, lb = 0;
            EclValue::Type rt = EclValue::Type::Integer;
            EclValue::Type lt = EclValue::Type::Integer;
            if (!stack_.pop(rb, rt) || !stack_.pop(lb, lt)) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const float right = (rt == EclValue::Type::Integer) ? float(as_int(rb)) : as_float(rb);
            const float left = (lt == EclValue::Type::Integer) ? float(as_int(lb)) : as_float(lb);
            bool value = false;
            if (opcode == u16(EclOp::EqualFloat)) value = (left == right);
            else if (opcode == u16(EclOp::NotEqualFloat)) value = (left != right);
            else if (opcode == u16(EclOp::LessFloat)) value = (left < right);
            else if (opcode == u16(EclOp::LessEqualFloat)) value = (left <= right);
            else if (opcode == u16(EclOp::GreaterFloat)) value = (left > right);
            else value = (left >= right);
            if (!stack_.push(EclValue::Type::Integer, u32(value ? 1 : 0))) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;
        }

        case EclOp::NotInt: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { ++stats_.fault_count; return EclRunResult::Fault; }
            const bool zero = (t == EclValue::Type::Float) ? (as_float(b) == 0.0f) : (as_int(b) == 0);
            stack_.push(EclValue::Type::Integer, u32(zero ? 1 : 0));
            break;
        }

        case EclOp::NotFloat: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { ++stats_.fault_count; return EclRunResult::Fault; }
            const float v = (t == EclValue::Type::Integer) ? float(as_int(b)) : as_float(b);
            stack_.push(EclValue::Type::Integer, u32(v == 0.0f ? 1 : 0));
            break;
        }

        case EclOp::NegateInt: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { ++stats_.fault_count; return EclRunResult::Fault; }
            stack_.push(EclValue::Type::Integer, u32(0u - b));
            break;
        }

        case EclOp::NegateFloatBits: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { ++stats_.fault_count; return EclRunResult::Fault; }
            stack_.push(EclValue::Type::Float, u32(0u - b));
            break;
        }

        case EclOp::Sin:
        case EclOp::Cos:
        case EclOp::Sqrt: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { ++stats_.fault_count; return EclRunResult::Fault; }
            const double v = (t == EclValue::Type::Integer) ? double(as_int(b)) : double(as_float(b));
            const double result = (opcode == u16(EclOp::Sin)) ? std::sin(v)
                                 : (opcode == u16(EclOp::Cos)) ? std::cos(v)
                                 : (v < 0.0 ? 0.0 : std::sqrt(v));
            stack_.push(EclValue::Type::Float, float_bits(float(result)));
            break;
        }

        case EclOp::Call: {
            const CallTarget target = parse_call(payload, u32(size - INSTRUCTION_HEADER));
            if (!target.valid) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const std::string key(target.name, target.name_length);
            const auto found = context.subroutines.find(key);
            if (found == context.subroutines.end()) {
                // A call to an unknown subroutine is not a crash: the ECL file
                // may legitimately reference one that lives elsewhere. Report
                // it so the caller knows coverage is incomplete.
                ++stats_.unsupported_count;
                if (stats_.first_unsupported_opcode == 0) {
                    stats_.first_unsupported_opcode = opcode;
                }
                return EclRunResult::Unsupported;
            }
            if (context.call_depth >= EclContext::MAX_CALL_DEPTH) {
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            ++context.call_depth;
            // Nested runs share the context (and therefore the stack and the
            // variables) but keep their own stats so the outer counters stay
            // meaningful. The nested result is propagated.
            EclInterpreter nested;
            nested.context = context;
            const EclRunResult nested_result = nested.run(found->second.code);
            // Preserve variable/frame state written by the callee.
            context = nested.context;
            --context.call_depth;
            stats_.instructions += nested.stats().instructions;
            stats_.unsupported_count += nested.stats().unsupported_count;
            stats_.fault_count += nested.stats().fault_count;
            if (stats_.max_stack_depth < nested.stats().max_stack_depth) {
                stats_.max_stack_depth = nested.stats().max_stack_depth;
            }
            if (nested_result == EclRunResult::Unsupported) {
                return EclRunResult::Unsupported;
            }
            if (nested_result == EclRunResult::Fault) {
                return EclRunResult::Fault;
            }
            break;
        }

        // --- TH185-only opcodes -------------------------------------------
        //
        // These two are the busiest opcodes that TH11 does not have
        // (1704 + 1021 = 2725 instructions, 11.5%). Their PAYLOAD STRUCTURE is
        // fully determined from the shipped data; their SEMANTICS are inferred.
        // Neither touches the operand stack, so recording them is safe even if
        // the interpretation turns out to be wrong -- the stack stays correct
        // and the records are observable.
        case EclOp::Op23Timer: {
            // payload: u32 reserved, u32 value
            context.last_timer_value = arg_u32(payload, 0);
            ++context.timer_writes;
            break;
        }

        case EclOp::Op1013Schedule: {
            // payload: u32 reserved, u32 slot, u32 time[8], u32 reserved
            EclContext::SpawnSchedule schedule;
            schedule.slot = arg_u32(payload, 0);
            for (u32 i = 0; i < 8; ++i) {
                schedule.times[i] = arg_u32(payload, 1 + i);
            }
            context.schedules.push_back(schedule);
            break;
        }

        // Everything else needs the ECL context/owner object.
        case EclOp::Spawn:
        case EclOp::SpawnNamed:
        case EclOp::CancelThread:
        case EclOp::SetThreadFlag:
        case EclOp::ClearThreadFlag:
        case EclOp::SetThreadState:
        case EclOp::CancelSecondary:
        case EclOp::PostDecrement:
        case EclOp::Polar:
        case EclOp::NormalizeAngle:
        case EclOp::LengthSquared:
        case EclOp::AngleTo:
        default:
            ++stats_.unsupported_count;
            if (stats_.first_unsupported_opcode == 0) {
                stats_.first_unsupported_opcode = opcode;
            }
            return EclRunResult::Unsupported;
        }

        if (!advanced) pc += size;
    }

    return EclRunResult::Finished;
}

}  // namespace th185
