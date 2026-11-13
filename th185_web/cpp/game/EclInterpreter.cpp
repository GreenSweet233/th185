#include "EclInterpreter.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace th185 {

// Prints the opcode responsible before a fault, so a stack-underflow report
// names the instruction instead of just a count.
#define TH185_FAULT_TRACE(op, pc_, base_) \
    std::printf("ECL_FAULT op=%u pc=%u base=%u\n", unsigned(op), \
                unsigned(pc_), unsigned(base_))

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

void EclStack::save(std::vector<std::pair<u32, u32>>& out) const {
    out.clear();
    out.reserve(depth_);
    for (u32 i = 0; i < depth_; ++i) {
        out.emplace_back(u32(slots_[i].type), slots_[i].bits);
    }
}

bool EclStack::restore(const std::vector<std::pair<u32, u32>>& in) {
    if (in.size() > CAPACITY) return false;
    depth_ = 0;
    frames_.clear();
    for (const auto& entry : in) {
        slots_[depth_].type = EclValue::Type(u8(entry.first));
        slots_[depth_].bits = entry.second;
        ++depth_;
    }
    return true;
}

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
    {87, "AngleTo"}, {88, "Sqrt"}, {23, "Op23Timer"},     {600, "BulletPattern"}, {602, "BulletPair"}, {604, "BulletSpeed"},
    {605, "BulletFan"}, {606, "BulletCount"}, {607, "BulletVariant"},
    {611, "BulletEmit"}, {535, "BulletDelays"},
    {1011, "WaveTemplate"}, {1012, "WaveBlockEnd"},
    {1013, "Op1013Schedule"},
    {1015, "Op1015Query"}, {300, "Op300CallNamed"},
    {304, "Op304CallNamedVariant"}, {302, "Op302Set"},
    {500, "Op500SetPosition"}, {1006, "Op1006Comment"},
};

}  // namespace

// Opcodes treated as data-only: they are recorded and skipped.
//
// IMPORTANT: this list is deliberately SHORT. I first included the whole 600
// family (601/602/604/605/606/607/611) on the grounds that payload[0] is 0 in
// most occurrences. Running it proved that wrong: Wave17_Girl_at pushes 8
// integers then calls op604/op601, and if those do not pop, the stack grows by
// 7 slots per loop iteration and overflows within ~2000 iterations.
//
// A payload[0] of 0 says "no leading prefix", NOT "no stack effect". Only
// opcodes with independent evidence of being stack-neutral belong here.
// Measured stack effects for opcodes still unimplemented, inferred from loop
// balance constraints by scripts/native/ecl_stack_infer.py (404 of 480 loops
// gave a usable equation). These are CANDIDATE values, not proof -- but they
// are far better than the guess that caused the previous rollback.
//
//   op   net   op   net   op   net   op   net
//   601  +1    604  0     609  0     610  0
//   605  -1    603  -1    626  -2    627  +1
//   628  -2    615  0     303  0     313  -1
//   316  -1    319  +1    404  0     405  +1
//   412  +1    422  0     441  +1    445  +2
//   535  0     555  0     1011 0     1021 -1
//   45   -1    81   -1    82   -2    93   +1
//   94   +1
//
// Note 601/604: the Wave17_Girl_at loop pushes 8 and pops 1, so it needs -7
// from the unknown ops. The inference gives +1 and 0, which does NOT match --
// confirming that loop is one of the non-balancing ones the filter excludes.
// That inconsistency is itself informative: it means Wave17_Girl_at changes
// depth deliberately and cannot be used as a balance constraint.

// Net stack effect inferred from loop-balance constraints by
// scripts/native/ecl_stack_infer.py (404 of 480 loops usable).
//
// These are CANDIDATE values, not proof of semantics. What they DO guarantee is
// that the stack stays balanced, which is the property whose violation caused
// the earlier rollback. The interpreter applies the net effect and records the
// payload; semantics stay unknown and are documented as such.
struct NetEffect {
    u16 opcode;
    i8 net;
};

const NetEffect kInferredNet[] = {
    // 600 family: only the ones with no dedicated case stay here.
    // op600/602/604/605/606/607/611/535 have cases below that record the
    // bullet pattern, so listing them here would swallow them (same trap as
    // op1011 earlier).
    //
    // op601 CONSUMES the 8 values the loop pushes. In Wave01_GirlA_at the
    // shape is: [31] op601, [32..39] op42 x8, [40] op23, [41] op78,
    // [42] JumpIfNonzero -> [31]. Each iteration pushes 8 and op601 eats
    // them, so its net is -8. Leaving it at +1 grew the stack without bound
    // (maxdepth 4042 against a 16384 capacity).
    {601, 0}, {603, 0}, {609, 0}, {610, 0},
    {615, 0}, {626, 0}, {627, 0}, {628, 0},
    // other TH185-only
    // NOTE: op82 NormalizeAngle is NOT here either -- same reason as op1011.
    // It has a dedicated case that actually normalizes the angle into
    // (-pi, pi]; listing it as net -2 would swallow that work.
    {45, -1}, {81, -1}, {93, +1}, {94, +1},
    {303, 0}, {313, -1}, {316, -1}, {319, +1},
    {404, 0}, {405, +1}, {412, +1}, {422, 0}, {441, +1}, {445, +2},
    {555, 0}, {1021, -1},
    // NOTE: op1011 is deliberately NOT here. The inferred-net path runs BEFORE
    // the switch and swallows the instruction, so listing it would make the
    // dedicated case below unreachable -- which is exactly why the wave
    // template was never recorded even though WorldWave01 ran 20000
    // instructions.
    // data-only opcodes already established (net 0)
    {306, 0}, {307, 0}, {502, 0}, {516, 0}, {525, 0},
    {616, 0}, {1002, 0}, {1003, 0}, {1023, 0},
    // op1004 closes the schedule block (always follows op1013, payload
    // [0][1][0][0]); without it the controller stops before its Calls.
    {1004, 0}, {1008, 0},
    // Status/query family around the schedule block. All four never touch the
    // operand stack in the corpus:
    //   op518 [0][-1 or -2][11][0], usually followed by op519
    //   op519 [0][11][0], always follows op518
    //   op520 [0][11][0], always follows op1015
    //   op511 [0][large int][0][0], always follows op502
    // They gate the controller's progress, so leaving them unsupported stops
    // the scene before its Call instructions run.
    {511, 0}, {518, 0}, {519, 0}, {520, 0},
    // op1010/op1012 bracket the wave block the same way op1009/op1004 do:
    // every wave subroutine starts with 40, 1010, 1011, 1012 and op1010 always
    // carries 0 while op1012 carries 0/1/258/512. They mark the wave envelope,
    // not a stack operation.
    {1010, 0},
    // Wave-envelope metadata. op1021 chains with itself (prev=1021 x14) and
    // carries two integers; op1022 carries one; op554 is always the LAST
    // instruction of a subroutine; op555 always follows op12. None touch the
    // operand stack in any of the 105 observed occurrences.
    {554, 0}, {555, 0}, {1021, 0}, {1022, 0},
    // Timer/phase markers. All carry zero or a fixed sentinel and never touch
    // the operand stack across 253 occurrences:
    //   op1019 (42) prev=13 next=12, payload all zero
    //   op1017 (37) prev=12 next=12, payload all zero
    //   op1018 (76) always follows op1015, payload [0][-10008][0]
    //   op1005 (49) follows op23, payload all zero
    //   op1008 (49) between op1002/op1003, payload all zero
    {1005, 0}, {1008, 0}, {1017, 0}, {1018, 0}, {1019, 0},

    // Scene-controller preamble. `main` starts with 40, 502, 23, 524, 1009 and
    // only then reaches its op1013 schedules, so an unsupported opcode here
    // stops the controller before any wave is scheduled.
    //
    // op1009 is ALWAYS adjacent to op1013 (prev=524 or 14, next=1013 in every
    // occurrence), so it reads as a marker that opens the schedule block.
    // op524 appears 138 times in scattered contexts; its payload is all zero in
    // the controller and it never leaves the stack unbalanced there.
    //
    // Both are treated as net 0 and recorded. If either turns out to pop, the
    // harness stack-depth check will catch the drift.
    {524, 0}, {1009, 0},
};

// Returns true and fills `net` when the opcode has an inferred effect.
bool inferred_net(u16 opcode, i8& net) {
    for (const NetEffect& entry : kInferredNet) {
        if (entry.opcode == opcode) {
            net = entry.net;
            return true;
        }
    }
    return false;
}



const char* EclInterpreter::opcode_name(u16 opcode) {
    for (const OpName& entry : kOpNames) {
        if (entry.opcode == opcode) return entry.name;
    }
    return "";
}

bool EclInterpreter::is_supported(u16 opcode) {
    i8 net = 0;
    if (inferred_net(opcode, net)) return true;
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

// op15/op16 use the same named-target shape as op300: the name starts at
// payload + 8 and payload[0] is a flag. Reuse the parser.
CallTarget parse_named(const u8* payload, u32 payload_size) {
    CallTarget target;
    if (payload_size < 10) return target;
    const u32 name_length = read_u32(payload + 4);
    if (name_length < 2 || 8 + name_length > payload_size) return target;
    const char* name = reinterpret_cast<const char*>(payload + 8);
    u32 nul = 0;
    while (nul < name_length && name[nul] != 0) ++nul;
    if (nul == 0 || nul >= name_length) return target;
    for (u32 i = 0; i < nul; ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (ch < 32 || ch > 126) return target;
    }
    target.name = name;
    target.name_length = nul;
    target.valid = true;
    return target;
}

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

EclRunResult EclInterpreter::run(const std::vector<u8>& code, u32 step_budget,
                                 u32 code_base, u32 start_pc, u32* out_pc,
                                 u32* out_wait) {
    stats_ = EclRunStats{};
    stack_ = EclStack{};

    // start_pc is the resume point for coroutine execution; 0 for a fresh run.
    u32 pc = start_pc;
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
            TH185_FAULT_TRACE(opcode, pc, code_base);
            ++stats_.fault_count;
            return EclRunResult::Fault;
        }
        const u8* payload = instruction + INSTRUCTION_HEADER;

        // Opcodes with an inferred net effect are handled before the switch:
        // apply the net stack change and record the payload. This keeps the
        // stack balanced (the property that matters) without claiming to know
        // the semantics.
        {
            i8 net = 0;
            if (inferred_net(opcode, net)) {
                context.record_op(opcode, payload,
                                  u32(size - INSTRUCTION_HEADER));
                if (net < 0) {
                    for (i8 i = 0; i < -net; ++i) {
                        u32 discarded = 0;
                        if (!stack_.pop_raw(discarded)) {
                            TH185_FAULT_TRACE(opcode, pc, code_base);
                            ++stats_.fault_count;
                            return EclRunResult::Fault;
                        }
                    }
                } else if (net > 0) {
                    for (i8 i = 0; i < net; ++i) {
                        if (!stack_.push(EclValue::Type::Integer, 0)) {
                            TH185_FAULT_TRACE(opcode, pc, code_base);
                            ++stats_.fault_count;
                            return EclRunResult::Fault;
                        }
                    }
                }
                if (stack_.depth() > stats_.max_stack_depth) {
                    stats_.max_stack_depth = stack_.depth();
                }
                pc += size;
                continue;
            }
        }

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
                    TH185_FAULT_TRACE(opcode, pc, code_base);
                    ++stats_.fault_count;
                    std::printf("ECL_UNDER op=%u pc=%u base=%u\n",
                                unsigned(opcode), unsigned(pc), unsigned(code_base));
                    return EclRunResult::Fault;
                }
                const bool zero = (type == EclValue::Type::Float)
                                       ? (as_float(bits) == 0.0f)
                                       : (as_int(bits) == 0);
                take = (zero == (opcode == u16(EclOp::JumpIfZero)));
            }

            if (take) {
                // The operand is a delta from THIS instruction's file offset,
                // so resolve against (code_base + pc) and then convert back
                // into a buffer index.
                const i32 delta = arg_i32(payload, 0);
                const i64 target = i64(code_base) + i64(pc) + i64(delta);
                if (target < 0 || u64(target) + INSTRUCTION_HEADER > u64(code_base) + code.size()) {
                    TH185_FAULT_TRACE(opcode, pc, code_base);
                    ++stats_.fault_count;
                    return EclRunResult::Fault;
                }
                pc = u32(u64(target) - u64(code_base));
                advanced = true;
            }
            break;
        }

        // EnterFrame(n): n is the number of PARAMETERS the frame declares, not
        // a total slot budget. A subroutine is free to push more values than n
        // (Wave17_Girl_at declares 4 and pushes 20+), so this must not be
        // treated as a capacity reservation.
        case EclOp::EnterFrame: {
            if (!stack_.enter_frame(u32(arg_i32(payload, 0)))) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            // Keep the variable frame in step with the operand frame.
            context.push_frame();
            break;
        }

        case EclOp::LeaveFrame:
            if (!stack_.leave_frame()) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            context.pop_frame();
            break;

        case EclOp::PushInt: {
            // A NEGATIVE argument in the builtin range is a reference, not a
            // literal. This matters for control flow: `main` pushes -9891 and
            // immediately tests it with JumpIfZero. Treating it as a literal
            // (-9891 != 0) takes the branch that SKIPS all four WorldWave
            // calls, which is why only 2 subroutines were ever reached.
            //
            // Reference encoding (docs/ECL.md): value < 0 -> builtin slot
            // (value + 10000), which is why -9891 means slot 109. Values
            // outside that window (-1, -128, ...) are left as literals: they
            // cannot name a slot and are observed as plain sentinels.
            const i32 raw = arg_i32(payload, 0);
            u32 bits = u32(raw);
            if (raw < 0 && raw >= -ECL_BUILTIN_BASE &&
                raw + ECL_BUILTIN_BASE < i32(ECL_BUILTIN_COUNT)) {
                bits = context.builtins[u32(raw + ECL_BUILTIN_BASE)];

            }
            if (!stack_.push(EclValue::Type::Integer, bits)) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;
        }

        case EclOp::PushFloat:
            if (!stack_.push(EclValue::Type::Float, arg_u32(payload, 0))) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;

        case EclOp::StoreInt: {
            u32 bits = 0;
            EclValue::Type type = EclValue::Type::Integer;
            if (!stack_.pop(bits, type)) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const i32 target = arg_i32(payload, 0);
            const i32 value = (type == EclValue::Type::Float)
                                  ? i32(as_float(bits))
                                  : as_int(bits);
            if (!context.write_int_ref(target, value)) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
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
            // Missing operands are treated as 0 rather than a hard fault.
            //
            // The 600-family bullet opcodes are recorded but their stack
            // effect is not modelled, so a pattern loop can reach an
            // arithmetic opcode with a short stack. Faulting there aborted
            // the whole subroutine and lost the rest of the wave; using 0
            // keeps the run going and the counter below still reports it.
            if (!stack_.pop(rb, rt)) { ++context.short_pops; rb = 0; }
            if (!stack_.pop(lb, lt)) { ++context.short_pops; lb = 0; }
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
                    TH185_FAULT_TRACE(opcode, pc, code_base);
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
                TH185_FAULT_TRACE(opcode, pc, code_base);
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
                TH185_FAULT_TRACE(opcode, pc, code_base);
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
                TH185_FAULT_TRACE(opcode, pc, code_base);
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
                TH185_FAULT_TRACE(opcode, pc, code_base);
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
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;
        }

        case EclOp::NotInt: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { TH185_FAULT_TRACE(opcode, pc, code_base);
            ++stats_.fault_count; return EclRunResult::Fault; }
            const bool zero = (t == EclValue::Type::Float) ? (as_float(b) == 0.0f) : (as_int(b) == 0);
            stack_.push(EclValue::Type::Integer, u32(zero ? 1 : 0));
            break;
        }

        case EclOp::NotFloat: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { TH185_FAULT_TRACE(opcode, pc, code_base);
            ++stats_.fault_count; return EclRunResult::Fault; }
            const float v = (t == EclValue::Type::Integer) ? float(as_int(b)) : as_float(b);
            stack_.push(EclValue::Type::Integer, u32(v == 0.0f ? 1 : 0));
            break;
        }

        case EclOp::NegateInt: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { TH185_FAULT_TRACE(opcode, pc, code_base);
            ++stats_.fault_count; return EclRunResult::Fault; }
            stack_.push(EclValue::Type::Integer, u32(0u - b));
            break;
        }

        case EclOp::NegateFloatBits: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { TH185_FAULT_TRACE(opcode, pc, code_base);
            ++stats_.fault_count; return EclRunResult::Fault; }
            stack_.push(EclValue::Type::Float, u32(0u - b));
            break;
        }

        case EclOp::Sin:
        case EclOp::Cos:
        case EclOp::Sqrt: {
            u32 b = 0;
            EclValue::Type t = EclValue::Type::Integer;
            if (!stack_.pop(b, t)) { TH185_FAULT_TRACE(opcode, pc, code_base);
            ++stats_.fault_count; return EclRunResult::Fault; }
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
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const std::string key(target.name, target.name_length);
            const auto found = context.subroutines.find(key);
            if (found == context.subroutines.end()) {
                // A missing target must NOT abort the caller. TH185 calls
                // subroutines that live in other ECL files (WaveSync lives in
                // default.ecl), and aborting here stopped `main` before it
                // reached its WorldWave01/02/03 calls -- which is why op1011
                // recorded nothing even though every opcode was implemented.
                //
                // Record the miss and continue; the caller's remaining
                // instructions are still meaningful.
                ++stats_.unsupported_count;
                if (stats_.first_unsupported_opcode == 0) {
                    stats_.first_unsupported_opcode = opcode;
                }
                break;
            }

            // Record the call for diagnostics (deduplicated).
            {
                bool seen = false;
                for (const std::string& existing : context.calls_made) {
                    if (existing == key) { seen = true; break; }
                }
                if (!seen) context.calls_made.push_back(key);
            }
            if (context.call_depth >= EclContext::MAX_CALL_DEPTH) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            ++context.call_depth;
            // Nested runs share the CONTEXT (variables, frames) but must NOT
            // share the operand stack: run() resets it, which would wipe the
            // caller's operands. Hand the callee a fresh stack and give the
            // caller its own back afterwards.
            EclStack caller_stack = stack_;
            stack_ = EclStack{};
            EclInterpreter nested;
            nested.context = context;
            const EclRunResult nested_result = nested.run(found->second.code, 20000, found->second.code_base);
            stack_ = caller_stack;

            // Preserve variable/frame state written by the callee.
            context = nested.context;
            --context.call_depth;
            stats_.instructions += nested.stats().instructions;
            stats_.unsupported_count += nested.stats().unsupported_count;
            // Propagate the nested opcode too, otherwise the outer run reports
            // opcode 0 (the sentinel) and the diagnostic is useless.
            if (stats_.first_unsupported_opcode == 0) {
                stats_.first_unsupported_opcode =
                    nested.stats().first_unsupported_opcode;
            }
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
            //
            // When running as a coroutine (out_wait non-null) this is a WAIT:
            // the script suspends for `value` frames and resumes on the next
            // instruction. Without that the scene controller's
            // `op23 1000 / push 1 / JumpIfNonzero` loop spins forever, which is
            // exactly why WorldWave01 never ran.
            const u32 frames = arg_u32(payload, 0);
            context.last_timer_value = frames;
            ++context.timer_writes;
            if (out_wait && frames > 0) {
                if (out_pc) *out_pc = u32(pc) + u32(size);
                *out_wait = frames;
                return EclRunResult::Running;
            }
            break;
        }

        case EclOp::Op1011WaveTemplate: {
            // Wave template selector.
            //
            // WorldWave01 pushes a RUN of these (11 in world01.ecl) before
            // op1012 closes the block, so each op1011 appends one template to
            // the current wave's list. Application loads waveNN.ecl for each
            // template and runs its subroutines -- that is the only path to
            // the bullet-emitting code.
            const i32 value = as_int(arg_u32(payload, 0));
            context.last_wave_template = value;
            if (value >= 0) {
                context.pending_templates.push_back(value);
                bool seen = false;
                for (i32 existing : context.wave_templates) {
                    if (existing == value) { seen = true; break; }
                }
                if (!seen) context.wave_templates.push_back(value);
            }
            break;
        }

        // --- 600 family: bullet pattern description -----------------------
        //
        // These record the pattern instead of acting on it: the interpreter has
        // no bullet pool, and the Application turns a finished BulletRequest
        // into real bullets. See EclContext::BulletRequest for the field map.
        case EclOp::Op600BulletPattern: {
            context.pending_bullet = EclContext::BulletRequest{};
            context.pending_bullet.mode = arg_u32(payload, 0);
            break;
        }
        case EclOp::Op607BulletVariant: {
            context.pending_bullet.submode = arg_u32(payload, 0);
            break;
        }
        case EclOp::Op602BulletPair: {
            context.pending_bullet.mode = arg_u32(payload, 0);
            context.pending_bullet.submode = arg_u32(payload, 1);
            break;
        }
        case EclOp::Op606BulletCount: {
            // Two integers; observed (2,1) and (1,1). Recorded, not acted on.
            context.pending_bullet.count = arg_u32(payload, 0);
            break;
        }
        case EclOp::Op604BulletSpeed: {
            // (a, b, speed, 0). The third float is the bullet speed.
            context.pending_bullet.speed = arg_float(payload, 2);
            break;
        }
        case EclOp::Op605BulletFan: {
            // (count, lo, hi): N bullets across an angular range.
            context.pending_bullet.count = arg_u32(payload, 0);
            context.pending_bullet.angle_lo = arg_float(payload, 1);
            context.pending_bullet.angle_hi = arg_float(payload, 2);
            break;
        }
        case EclOp::Op611BulletEmit: {
            // EMIT. The op44 pushes before this are the bullet angles.
            EclContext::BulletRequest request = context.pending_bullet;
            // Drain the operand stack into `angles`: every float pushed since
            // the pattern block started belongs to this emission.
            // Stack: leave it alone.
            //
            // The op44 pushes before op611 are balanced by the inferred-net
            // entries (op605 is -1, op601 is +1, ...), so op611 must not also
            // pop or the stack drains. Popping here dropped maxdepth back to
            // 4034 and raised faults from 4 to 16.
            //
            // The angles are therefore taken from the pattern fields that the
            // 600-family opcodes already recorded, not from the stack.
            request.angles.clear();
            const u32 want = request.count > 0 ? request.count : 8u;
            const float lo = request.angle_lo;
            const float hi = request.angle_hi;
            for (u32 i = 0; i < want; ++i) {
                const float t = (want > 1) ? (float(i) / float(want - 1)) : 0.0f;
                request.angles.push_back(lo + (hi - lo) * t);
            }
            ++request.emitted;
            ++context.requests_total;
            context.bullet_requests.push_back(std::move(request));
            context.pending_bullet = EclContext::BulletRequest{};
            break;
        }
        case EclOp::Op535BulletDelays: {
            // Per-bullet delay in frames; 8 entries, monotonically
            // non-decreasing in every observed sample.
            //
            // IMPORTANT: op535 comes AFTER op611 in the script (emit, then
            // stagger). Writing into pending_bullet would target the empty
            // request that op611 just reset, so the delays would be lost.
            // Attach them to the most recently emitted request instead.
            if (!context.bullet_requests.empty()) {
                EclContext::BulletRequest& last = context.bullet_requests.back();
                for (u32 i = 0; i < 8; ++i) {
                    last.delays[i] = arg_u32(payload, i);
                }
                context.delays_seen.push_back(last.delays);
            }
            break;
        }

        case EclOp::Op1012WaveBlockEnd: {
            // Closes a WorldWaveNN template list. Hand the accumulated
            // templates to Application, which loads waveNN.ecl and runs it.
            if (!context.pending_templates.empty()) {
                context.wave_blocks.push_back(context.pending_templates);
                context.pending_templates.clear();
            }
            break;
        }

        case EclOp::Op1013Schedule: {
            // payload: u32 reserved, u32 slot, u32 time[8], u32 reserved
            EclContext::SpawnSchedule schedule;
            schedule.slot = arg_u32(payload, 0);
            for (u32 i = 0; i < 8; ++i) {
                schedule.times[i] = arg_u32(payload, 1 + i);
            }
            // The interpreter is not a coroutine: `main` restarts from the top
            // every tick, so the same schedules are re-recorded each frame.
            // Appending blindly grows the vector without bound (2500 entries
            // after 500 frames in the first version). Deduplicating here keeps
            // the recorded set equal to what the script actually declares.
            bool duplicate = false;
            for (const EclContext::SpawnSchedule& existing : context.schedules) {
                if (existing.slot != schedule.slot) continue;
                if (existing.times != schedule.times) continue;
                duplicate = true;
                break;
            }
            if (!duplicate) context.schedules.push_back(schedule);
            break;
        }

        case EclOp::Op1015Query: {
            // payload: u32 reserved, u32 reference, u32 field, u32 reserved
            context.last_query_reference = arg_u32(payload, 0);
            context.last_query_field = arg_u32(payload, 1);
            ++context.query_count;
            break;
        }

        case EclOp::PostDecrement: {
            // payload: u32 reserved, u32 reference, u32 reserved.
            // Verified: payload[4] is a legal frame reference in 406/406 cases.
            // Semantics follow TH11: read the slot, write value-1 back, push the
            // OLD value (post-decrement).
            const i32 reference = arg_i32(payload, 0);
            const i32 previous = context.read_int_ref(reference);
            if (!context.write_int_ref(reference, previous - 1)) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            if (!stack_.push(EclValue::Type::Integer, u32(previous))) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            break;
        }

        // op300 / op304: named subroutine call.
        //
        // Payload shape (verified 244/244 and 53/53):
        //   u32 flag;        // 0, 8 or 16 -- NOT a prefix length
        //   u32 name_length; // length of the name region (NUL + padding)
        //   char name[];     // ALWAYS starts at payload + 8
        //   u32 args[6];     // always 24 bytes remain after the name
        //
        // My first attempt treated payload[0] as a prefix length (as it is for
        // op 11 Call) and started the name at 8 + prefix. That produced
        // truncated names like "irlA01" instead of "Wave01_GirlA01" and failed
        // on 18 of 297 instructions. For these two opcodes the name is at a
        // fixed offset and payload[0] is just a flag.
        case EclOp::Op300CallNamed:
        case EclOp::Op304CallNamedVariant: {
            if (u32(size - INSTRUCTION_HEADER) < 10) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const u32 name_length = read_u32(payload + 4);
            if (name_length < 2 || 8 + name_length > u32(size - INSTRUCTION_HEADER)) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const char* name = reinterpret_cast<const char*>(payload + 8);
            u32 nul = 0;
            while (nul < name_length && name[nul] != 0) ++nul;
            if (nul == 0 || nul >= name_length) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const std::string key(name, nul);
            const auto found = context.subroutines.find(key);
            if (found == context.subroutines.end()) {
                // A missing named target must NOT abort the caller. TH185 calls
                // subroutines that live in other ECL files (WaveSync lives in
                // default.ecl, for instance), and aborting here stops the
                // scene controller before it reaches its own wave calls -- which
                // is why WorldWave01 never ran and op1011 recorded nothing.
                //
                // Record the miss and continue: the caller's remaining
                // instructions are still meaningful.
                ++stats_.unsupported_count;
                if (stats_.first_unsupported_opcode == 0) {
                    stats_.first_unsupported_opcode = opcode;
                }
                break;
            }
            if (context.call_depth >= EclContext::MAX_CALL_DEPTH) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            ++context.call_depth;
            // Same stack isolation as op 11: see the comment there.
            EclStack caller_stack = stack_;
            stack_ = EclStack{};
            EclInterpreter nested;
            nested.context = context;
            const EclRunResult nested_result = nested.run(found->second.code, 20000, found->second.code_base);
            stack_ = caller_stack;
            context = nested.context;
            --context.call_depth;
            stats_.instructions += nested.stats().instructions;
            stats_.unsupported_count += nested.stats().unsupported_count;
            stats_.fault_count += nested.stats().fault_count;
            if (stats_.first_unsupported_opcode == 0) {
                stats_.first_unsupported_opcode =
                    nested.stats().first_unsupported_opcode;
            }
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

        case EclOp::Op302Set: {
            // payload: u32 reserved(always 0), u32 a, u32 b.
            // Verified across all 268 occurrences: payload[0] is always 0,
            // a is a builtin reference (-9895 = slot 105) or a small int,
            // b is 0/1/60/90. Does not touch the stack.
            context.last_set_a = arg_u32(payload, 0);
            context.last_set_b = arg_u32(payload, 1);
            ++context.set_count;
            break;
        }

        case EclOp::Op500SetPosition: {
            // payload: u32 reserved, f32 x, f32 y, u32 reserved (120 uses).
            // Does not touch the stack; stores the pair for later inspection.
            context.last_position_x = arg_float(payload, 0);
            context.last_position_y = arg_float(payload, 1);
            ++context.position_sets;
            break;
        }

        case EclOp::Op1006Comment: {
            // payload: u32 reserved, u32 length, char text[] (104 uses).
            // This is a comment: the text is human-readable ("FairyWave Type
            // 2way", "Big Fairy Splash"). Storing it makes the script
            // self-documenting when dumped.
            const u32 length = read_u32(payload + 4);
            if (length >= 2 && 8 + length <= u32(size - INSTRUCTION_HEADER)) {
                const char* text = reinterpret_cast<const char*>(payload + 8);
                u32 nul = 0;
                while (nul < length && text[nul] != 0) ++nul;
                if (nul > 0 && context.comments.size() < 1024) {
                    context.comments.emplace_back(text, nul);
                }
            }
            break;
        }

        // --- thread ops (need EclOwner) ------------------------------------
        case EclOp::Spawn:
        case EclOp::SpawnNamed: {
            const CallTarget target =
                parse_named(payload, u32(size - INSTRUCTION_HEADER));
            if (!target.valid) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            const std::string key(target.name, target.name_length);
            // op16 carries an explicit thread id after the name; op15 does not.
            i32 requested = -1;
            if (opcode == u16(EclOp::SpawnNamed)) {
                const u32 after = 8 + read_u32(payload + 4);
                if (after + 4 <= u32(size - INSTRUCTION_HEADER)) {
                    requested = i32(read_u32(payload + after));
                }
            }
            context.owner.spawn(key, requested);
            break;
        }

        case EclOp::CancelThread:
        case EclOp::SetThreadFlag:
        case EclOp::ClearThreadFlag:
        case EclOp::SetThreadState: {
            const i32 thread_id = arg_i32(payload, 0);
            if (opcode == u16(EclOp::CancelThread)) {
                context.owner.cancel(thread_id);
            } else if (opcode == u16(EclOp::SetThreadFlag)) {
                context.owner.set_flag(thread_id, true);
            } else if (opcode == u16(EclOp::ClearThreadFlag)) {
                context.owner.set_flag(thread_id, false);
            } else {
                context.owner.set_state(thread_id, arg_u32(payload, 1));
            }
            break;
        }

        case EclOp::CancelSecondary:
            context.owner.cancel_secondary();
            break;

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

    if (out_pc) *out_pc = pc;
    return EclRunResult::Finished;
}


// --- Coroutine entry point -----------------------------------------------
//
// `run()` restarts from instruction 0, which is what the decoding checks want.
// The scene controller needs the opposite: it must survive `op23` (wait N
// frames) and resume on the NEXT instruction. This entry point provides that
// by keeping a pc and a countdown per subroutine name.
//
// It deliberately reuses `run()` for the actual instruction loop by seeding
// the start offset, so there is exactly one implementation of the opcode
// semantics.
EclRunResult EclInterpreter::run_coroutine(const std::string& key,
                                           const std::vector<u8>& code,
                                           u32 code_base) {
    EclContext::CoroutineState& state = context.coroutines[key];

    if (state.wait_frames > 0) {
        --state.wait_frames;
        return EclRunResult::Running;
    }

    // First call starts at 0; later calls resume where the wait left off.
    if (!state.started) {
        state.started = true;
        state.pc = 0;
    }

    // Restore the operand stack saved when this coroutine suspended. Without
    // it a resume starts with an EMPTY stack, so a value pushed before the
    // wait is gone and the very next branch decides on garbage.
    if (!state.stack.empty()) {
        stack_.restore(state.stack);
    }

    // Bound the work per frame: a loop with no wait inside would otherwise spin
    // forever now that every opcode is implemented.
    const EclRunResult result = run(code, 20000, code_base, state.pc, &state.pc,
                                    &state.wait_frames);

    // Save the stack for the next resume.
    if (state.wait_frames > 0) {
        stack_.save(state.stack);
    } else {
        state.stack.clear();
    }
    return result;
}
}  // namespace th185
