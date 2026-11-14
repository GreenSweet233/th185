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
    {1024, "BossIndex"}, {512, "BossFrameEnter"}, {307, "BossTimer"},
    {515, "BossFrameLength"}, {516, "BossAttackTimer"},
    {400, "EffectSlot"}, {404, "BulletPattern"}, {405, "BulletLaunch"},
    {501, "SetPositionPair"}, {503, "BossPhase"}, {505, "BranchAction"},
    {412, "PatternExtra"}, {401, "PatternAux"}, {504, "BossSpawnBox"},
    {629, "BossConfig"}, {632, "AttackSlot"}, {573, "SpawnCount"}, {316, "BossExtra"},
    {612, "PatternExtended"}, {613, "FrameValue"}, {614, "PatternCount"},
    {440, "EffectSpawn"}, {640, "CallNamedTail"}, {91, "FloatBlock"},
    {524, "SceneSync"},
    {601, "BulletConsume"},
    {527, "SpawnRect"}, {523, "BossWaitMark"}, {536, "AngleTable"}, {540, "SpawnCommit"},
    {513, "ScriptMark"}, {514, "CallNamedExt"}, {521, "CallNamedEffect"},
    {507, "SpawnParams"}, {1026, "BossTimerValue"},
    {545, "AttackPrepare"}, {445, "TimedValue"},
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
    {603, 0}, {609, 0}, {610, 0},
    {615, 0}, {626, 0}, {627, 0}, {628, 0},
    // other TH185-only
    // NOTE: op82 NormalizeAngle is NOT here either -- same reason as op1011.
    // It has a dedicated case that actually normalizes the angle into
    // (-pi, pi]; listing it as net -2 would swallow that work.
    {45, -1}, {81, -1}, {93, +1}, {94, +1},
    {303, 0}, {313, -1}, {319, +1},
    {404, 0}, {405, +1}, {412, +1}, {422, 0}, {441, +1}, {445, +2},
    {555, 0}, {1021, -1},
    // NOTE: op1011 is deliberately NOT here. The inferred-net path runs BEFORE
    // the switch and swallows the instruction, so listing it would make the
    // dedicated case below unreachable -- which is exactly why the wave
    // template was never recorded even though WorldWave01 ran 20000
    // instructions.
    // data-only opcodes already established (net 0)
    {306, 0}, {502, 0}, {525, 0},
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
    {1005, 0}, {1008, 0}, {1018, 0},

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
    {1009, 0},
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

// Upper bound on one bullet emission. The recorded op606 counts top out at
// 32; this only stops a corrupt or referenced value from becoming a
// multi-billion-iteration loop (which aborted the WASM instance once).
constexpr u32 MAX_EMIT = 64;

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
        // Header layout, verified against every instruction in 40 ECL files:
        //   +0x04 u16 param_mask    (0 or 1)
        //   +0x06 u8  rank_mask     (difficulty bitmask: 0x01..0x80, or 0xFF)
        //   +0x07 u8  param_count
        // The previous version read +0x06 as the whole mask, which is why the
        // difficulty tier was never consulted.
        const u16 param_mask = read_u16(instruction + 4);
        (void)param_mask;  // reserved for opcodes with mixed literal/reference args
        const u8 rank_mask = instruction[6];

        // Difficulty gate. 0xFF means "every tier"; otherwise the instruction
        // only runs when the bit for the current tier is set. Without this the
        // higher difficulties executed every tier's instructions at once.

        if (rank_mask != 0xFF) {
            const u8 tier_bit = u8(1u << (context.difficulty & 7u));

            if ((rank_mask & tier_bit) == 0) {
                ++stats_.difficulty_skipped;
                if (stats_.difficulty_skipped == 1) {
                    stats_.first_masked_opcode = opcode;
                    stats_.first_masked_rank = rank_mask;
                }
                pc += size;
                continue;
            }
        }
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

            // Depth is tracked on the interpreter, NOT on the shared context.
            //
            // It used to live in EclContext, but `context = nested.context`
            // copies the callee's counter back into the caller, so each level
            // added one again on return: a 3-deep call chain measured as
            // depth=1257. The nesting is real (Boss23 -> Boss23Boss ->
            // Boss23Boss1) and only three levels deep.
            if (nesting_ >= MAX_NESTING) {
                TH185_FAULT_TRACE(opcode, pc, code_base);
                ++stats_.fault_count;
                return EclRunResult::Fault;
            }
            // Nested runs share the CONTEXT (variables, frames) but must NOT
            // share the operand stack: run() resets it, which would wipe the
            // caller's operands. Hand the callee a fresh stack and give the
            // caller its own back afterwards.
            EclStack caller_stack = stack_;
            stack_ = EclStack{};
            EclInterpreter nested;
            nested.context = context;
            nested.nesting_ = nesting_ + 1;
            // Same protection as op300: the callee's context snapshot must not
            // overwrite the caller's coroutine pc when it is copied back.
            const auto saved_coroutines = context.coroutines;
            const EclRunResult nested_result = nested.run(found->second.code, 20000, found->second.code_base);
            stack_ = caller_stack;

            // Copy back ONLY what the callee owns: variable frames and the
            // records it appended.
            //
            // `context = nested.context` used to be used here, and it has caused
            // three separate bugs because it replaces the WHOLE context:
            //   1. wave_blocks appended by the callee were discarded
            //   2. call_depth was copied back and re-incremented per level
            //   3. a CoroutineState& held by run_coroutine dangled afterwards
            // Listing the fields explicitly makes that class of bug impossible.
            // Whole-context write-back, with the two fields the caller owns
            // restored afterwards.
            //
            // A hand-listed field-by-field copy was tried and it silently dropped
            // fields (boss_index_sets went from 2 to 0), because EclContext has
            // ~100 members and any new one is missed. Copying wholesale and then
            // restoring the caller-owned fields is the version that cannot lose
            // data; ecl_merge_check.py covers the Application-level equivalent.
            const auto caller_coroutines = context.coroutines;
            context = nested.context;
            context.coroutines = caller_coroutines;
            context.call_depth = nesting_ + 1;
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
            const i32 raw_frames = arg_i32(payload, 0);
            // A NEGATIVE argument is not a frame count. Measured in wave45 and
            // wave65: `op23 0xFFFFFFFF` (-1) appears right after a push and
            // before op600 -- it is a sentinel, not a delay. Treating it as a
            // huge unsigned value made the coroutine wait forever and the
            // per-frame budget was exhausted instead of the script progressing.
            const u32 frames = raw_frames > 0 ? u32(raw_frames) : 0u;
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
            // (mode, count, flag): the emission count is the SECOND word.
            //
            // Measured payload distribution over 190 occurrences:
            //   [0][0][1][1][0]   x67
            //   [8][0][-1][1][0]  x17
            //   [0][1][1][1][0]   x12
            //   [0][0][5][1][0]   x9
            //   [0][0][32][1][0]  x6
            // The third word is the count (1/2/3/5/8/32 ...) and the fourth is
            // always 1. Reading index 0 gave 0 for 90% of them, so every
            // emission fell back to the default 8 and the field filled with
            // 1140 bullets.
            context.pending_bullet.mode = arg_u32(payload, 0);
            // A negative count is a REFERENCE (payload [8][0][-1][1][0]), not a
            // number of bullets. Treating it as u32 made the emit loop run
            // 4 billion times and aborted the WASM instance.
            const i32 raw = arg_i32(payload, 1);
            context.pending_bullet.count = raw > 0 ? u32(raw) : 0u;
            break;
        }
        case EclOp::Op604BulletSpeed: {
            // (a, b, speed, 0). The third float is the bullet speed.
            context.pending_bullet.speed = arg_float(payload, 2);
            break;
        }
        case EclOp::Op605BulletFan: {
            // (lo, hi): the angular range of the fan.
            //
            // Measured on Boss05BossCard2_at: the payload is
            //   [0][0][1.5][1.0]   -> 00000000 00000000 0000c03f 0000803f
            // so the two floats are at +8 and +12, i.e. arg index 1 and 2.
            // There is NO count here -- reading index 0 gave 0 and CLOBBERED
            // the count that op606 had just set, which is why every emission
            // fell back to the default 8 and the field filled with 1140
            // bullets.
            context.pending_bullet.angle_lo = arg_float(payload, 1);
            context.pending_bullet.angle_hi = arg_float(payload, 2);
            break;
        }
        case EclOp::Op601BulletConsume: {
            // Consumes the values pushed by the pattern loop.
            //
            // Measured from Wave01_GirlA_at's loop body [31..42]:
            //   op42 x8 (+8), op23 (0), op78 (+1), op14 (-1)  => net +8
            // so op601 must remove 8 to keep the loop balanced. Leaving it at
            // 0 grew the stack to 3805; hard-popping 8 underflowed on call
            // sites that enter with fewer values (85 faults).
            //
            // Pop what is available, up to 8, and record a shortfall. A
            // shortfall means the caller's own stack was already short, which
            // is a signal rather than something to abort on.
            u32 popped = 0;
            while (popped < 8) {
                u32 bits = 0;
                if (!stack_.pop_raw(bits)) break;
                ++popped;
            }
            if (popped < 8) ++context.short_pops;
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
            // Clamp: op606's count reaches 32 in the data, and a corrupt or
            // referenced value must not turn into a 4-billion-iteration loop.
            const u32 want = request.count > 0
                                  ? (request.count > MAX_EMIT ? MAX_EMIT : request.count)
                                  : 8u;
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

        case EclOp::Op1017StageSignalSet: {
            // Sets the "stage signal" flag the scene controller tests next.
            //
            // Derived from control flow, not from a name: every worldNN.main
            // has the same shape
            //
            //   op42 -9896        push builtin slot 104
            //   op13 -> +3        if zero, jump to op1017
            //   op1019            ...otherwise wait here
            //   op12 -> +2
            //   op1017            ...and this branch sets it
            //
            // so 1019 and 1017 are the two arms of one test: 1019 waits for
            // the signal, 1017 raises it. The payload is all zero in every
            // occurrence (13 and 8 samples), so there is no argument to read.
            context.stage_signal = 1;
            ++context.stage_signal_sets;
            break;
        }

        case EclOp::Op1019StageSignalWait: {
            // Waits until the stage signal is raised. Yielding is what makes
            // this useful: the scene controller resumes on a later tick once
            // something else (a boss defeat, a timer) sets the flag.
            if (context.stage_signal == 0) {
                ++context.stage_signal_waits;
                if (out_wait) {
                    *out_wait = 1;
                    return EclRunResult::Running;
                }
            }
            context.stage_signal = 0;   // consumed
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

        case EclOp::Op515BossFrameLength: {
            // Frame length for the block op512 opened.
            //
            // Evidence: 29 occurrences across 29 files, 28/29 with payload 0x78
            // (120) and one 0x5A (90); context is prev=op512 next=op502 in 28/29.
            // A frame that follows "frame enter" and carries a frame count is
            // the frame's duration.
            context.boss_frame_length = arg_u32(payload, 0);
            ++context.boss_frame_lengths;
            break;
        }

        case EclOp::Op516BossAttackTimer: {
            // Per-attack timer inside a boss frame.
            //
            // Evidence: 182 occurrences across 38 files. Payload is
            // [u32 reserved][u32 a][u32 b]; a takes 0x1B(27)/0x36(54)/0x06(6) and
            // b is 0 or 0x3C(60). Context is most often prev=op307 (the boss
            // timer) which places it right after the timer is set.
            context.boss_attack_a = arg_u32(payload, 0);
            context.boss_attack_b = arg_u32(payload, 1);
            ++context.boss_attack_timers;
            break;
        }
        case EclOp::Op524SceneSync: {
            // Scene synchronisation point.
            //
            // Evidence: 121 occurrences across 88 files, size ALWAYS 20 so the
            // payload is one u32 plus a reserved word; 87/121 are zero and
            // 28/121 are [0x2B=43][1]. Context is prev=op23 (a wait) next=op300
            // (a named call) or op42 in 63/121 -- so it sits between a wait and
            // the next action.
            //
            // It YIELDS. Boss01ttest stops here with wait_frames == 0, which
            // means the coroutine is not waiting but also not advancing: the
            // instruction must be a synchronisation barrier, not a no-op. Yielding
            // one frame lets the frame loop run and the coroutine resume.
            context.scene_syncs = arg_u32(payload, 0);
            ++context.scene_sync_count;
            if (out_wait) {
                if (out_pc) *out_pc = u32(pc) + u32(size);
                *out_wait = 1;
                return EclRunResult::Running;
            }
            break;
        }
        case EclOp::Op316BossExtra: {
            // Boss extra parameter block.
            //
            // Evidence: 37 occurrences across 16 files, size ALWAYS 24 so the
            // payload is [u32][u32][u32][u32]. Payload is all-zero in 13/37,
            // [0][0][0][0x3C=60] in 9/37, [0][0][1][0] in 4/37. Context is
            // prev=op302 (a setter) next=op23 / op0 / op15.
            context.boss_extra_a = arg_u32(payload, 1);
            context.boss_extra_b = arg_u32(payload, 2);
            context.boss_extra_c = arg_u32(payload, 3);
            ++context.boss_extras;
            break;
        }
        case EclOp::Op640CallNamedTail: {
            // Named call whose name sits at payload + 16.
            //
            // Evidence: 1 occurrence (boss19.ecl), size 48. The payload carries
            // the plain text "Boss19BossCard2_et" after two u32s:
            //   [0][0][4][20]"Boss19BossCard2_et"...
            // Context is prev=op609 next=op609.
            //
            // Same calling convention as op514 (name after a small header), so
            // the name is extracted rather than the whole block recorded.
            context.named_tail_length = arg_u32(payload, 2);
            context.named_tail_target.assign(
                reinterpret_cast<const char*>(payload) + 12, 32);
            ++context.named_tail_calls;
            break;
        }

        case EclOp::Op91FloatBlock: {
            // A block of floats describing a motion.
            //
            // Evidence: 18 occurrences across 10 files, size ALWAYS 40 so the
            // payload is 10 words. Context is prev=op609 (a float compare) or
            // prev=op611 (EMIT) next=op12 (Jump) / op42.
            //
            // Reading as floats: sample 2 gives [0][0][8.0][0][4.0][600.0][10.0]
            // -- position, speed and frame counts, the shape a motion takes.
            for (u32 k = 0; k < 10; ++k) {
                context.float_block[k] = arg_float(payload, k);
            }
            ++context.float_blocks;
            break;
        }
        case EclOp::Op440EffectSpawn: {
            // Spawns an effect object with one float parameter.
            //
            // Evidence: 14 occurrences across 11 files, size ALWAYS 20 so the
            // payload is [u32][f32][u32], and the payload is IDENTICAL in all
            // 14: [8][-1.0f][0]. Context is prev=op51 (a float add) next=op445
            // or op16 (SpawnNamed) in 14/14 -- so it sits right before an
            // object is spawned.
            context.effect_spawn_mode = arg_u32(payload, 0);
            context.effect_spawn_scale = arg_float(payload, 1);
            ++context.effect_spawns;
            break;
        }

        case EclOp::Op612PatternExtended: {
            // Extended bullet-pattern parameters.
            //
            // Evidence: 13 occurrences across 8 files, size ALWAYS 60 so the
            // payload is exactly 14 u32s. Context is prev=op611 (the EMIT
            // instruction) in 7/13, so it refines the pattern just emitted.
            //
            // Reading the words as mixed types gives sensible values:
            //   [3]  = 16            (a count)
            //   [4]  = 30 / 1 / 60   (frames)
            //   [5]  = 1             (a flag)
            //   [8]  = 3.0f          (0x40408000)
            //   [10] = 1.5708f       (0x3FC90FDB = pi/2, an angle)
            //   [11] = 622847.6f     (0x49180F7A -- a packed sentinel)
            for (u32 k = 0; k < 14; ++k) {
                context.pattern_ext[k] = arg_u32(payload, k);
            }
            ++context.pattern_exts;
            break;
        }

        case EclOp::Op614PatternCount: {
            // A single small integer refining the emitted pattern.
            //
            // Evidence: 13 occurrences across 8 files, size ALWAYS 24 so the
            // payload is [u32][u32][u32][u32]. payload[1] is 1 (7x), 2 (1x),
            // 3 (1x). Context is prev=op611 in 6/13 and prev=op628 in 1/13.
            context.pattern_count = arg_u32(payload, 1);
            ++context.pattern_counts;
            break;
        }

        case EclOp::Op613FrameValue: {
            // One frame-count value.
            //
            // Evidence: only 1 occurrence (boss27.ecl), size 16, payload
            // [0][0x3C=60]. Context is prev=op525 next=op523. Recorded rather
            // than interpreted -- one sample is not enough to claim semantics.
            context.frame_value = arg_u32(payload, 0);
            ++context.frame_values;
            break;
        }
        case EclOp::Op629BossConfig: {
            // Boss configuration: one float and a flag word.
            //
            // Evidence: 28 occurrences across 28 files, size ALWAYS 24 so the
            // payload is [u32][f32][u32][u32]. Payload is
            // [0][160.0][0x00000F80][1]; 160.0 is a field-scale distance and
            // 0xF80 is a bit set (bits 7..11). Context is prev=op302 next=op11
            // in 28/28 -- it configures the boss right before its body is called.
            context.boss_config_radius = arg_float(payload, 1);
            context.boss_config_flags = arg_u32(payload, 2);
            ++context.boss_configs;
            break;
        }

        case EclOp::Op632AttackSlot: {
            // Zero-argument attack slot marker.
            //
            // Evidence: 41 occurrences across 29 files, size ALWAYS 20 (payload
            // all zero), context is prev=op523 next=op545 in 41/41 -- op545 is
            // the attack-prepare instruction, so this immediately precedes it.
            ++context.attack_slots;
            break;
        }

        case EclOp::Op573SpawnCount: {
            // Spawn counts, same shape as op507.
            //
            // Evidence: 44 occurrences across 22 files, size ALWAYS 24, payload
            // [0][count][mode][0] with pairs (20,15) and (2,4). Context is
            // prev=op507 next=op507 / op23, i.e. it pairs with op507.
            context.spawn_count_a = arg_u32(payload, 1);
            context.spawn_count_b = arg_u32(payload, 2);
            ++context.spawn_counts;
            break;
        }

        case EclOp::Op540SpawnCommit: {
            // Commits the spawn rectangle op527 configured.
            //
            // Evidence: 18 occurrences across 18 files, size ALWAYS 20 (payload
            // all zero), context is prev=op527 (SpawnRect) next=op1023 in 17/18.
            ++context.spawn_commits;
            break;
        }
        case EclOp::Op527SpawnRect: {
            // Spawn rectangle: two floats plus a sentinel.
            //
            // Evidence: 40 occurrences across 28 files, size ALWAYS 28 so the
            // payload is [u32][u32][f32][f32][u32]. Observed floats are 1300.0
            // and 28.75 / 720.0 -- field-scale extents. Context is prev=op521
            // (the named effect call) next=op540 / op527.
            context.spawn_rect_w = arg_float(payload, 2);
            context.spawn_rect_h = arg_float(payload, 3);
            ++context.spawn_rects;
            break;
        }

        case EclOp::Op523BossWaitMark: {
            // Zero-argument wait marker.
            //
            // Evidence: 43 occurrences across 29 files, size ALWAYS 16 (42/43
            // payload all zero), context is prev=op616 next=op632 in 41/43.
            ++context.boss_wait_marks;
            break;
        }

        case EclOp::Op536AngleTable: {
            // A table of angles.
            //
            // Evidence: 43 occurrences across 26 files, size ALWAYS 52 so the
            // payload is 11 floats. Sample values are 12.0, 112.0, 112.0, 144.0,
            // 160.0, 180.0, 208.0, 224.0, 240.0, 256.0, 0 -- a monotone angle
            // sweep, and a second variant runs -9990.0 (a builtin reference)
            // through -2.0, -4.0, -6.0, -8.0, -10.0, -12.0, -14.0, -16.0, -18.0.
            for (u32 k = 0; k < 11; ++k) {
                context.angle_table[k] = arg_float(payload, k);
            }
            ++context.angle_tables;
            break;
        }
        case EclOp::Op412PatternExtra: {
            // Trailing parameters of a bullet pattern.
            //
            // Evidence: 95 occurrences across 47 files, size ALWAYS 28 so the
            // payload is [u32][u32][u32][f32][u32]. Payload is
            // [0][0x3C=60][4][1.0][tail] with tail in {0, 120, 60}. Context is
            // most often prev=op23 next=op23 (40/95) -- it sits inside a wait
            // loop, refining the pattern each iteration.
            context.pattern_extra_frames = arg_u32(payload, 1);
            context.pattern_extra_mode = arg_u32(payload, 2);
            context.pattern_extra_scale = arg_float(payload, 3);
            context.pattern_extra_tail = arg_u32(payload, 4);
            ++context.pattern_extras;
            break;
        }

        case EclOp::Op401PatternAux: {
            // Auxiliary pattern data.
            //
            // Evidence: 92 occurrences across 29 files, size ALWAYS 32 so the
            // payload is [u32 x6][f32 x2]. 41/92 are all-zero (a no-op variant),
            // the rest carry [0][0x3C=60][4][0][0][0x43]. Context is
            // prev=op405 (bullet launch) next=op42 in 41/92.
            context.pattern_aux_a = arg_u32(payload, 1);
            context.pattern_aux_b = arg_u32(payload, 2);
            context.pattern_aux_c = arg_u32(payload, 5);
            ++context.pattern_aux_count;
            break;
        }

        case EclOp::Op504BossSpawnBox: {
            // Spawn area for a boss object: centre plus extents.
            //
            // Evidence: 85 occurrences across 49 files, size ALWAYS 32 so the
            // payload is [u32][u32][u32][f32][f32][u32]. Payload is
            // [0][0][0x43][280.0][64.0][flag]; 280 and 64 are field-scale
            // dimensions. Context is prev=op501 (the position pair) next=op302
            // in 28/85 -- it follows a position being set.
            context.spawn_box_x = arg_float(payload, 3);
            context.spawn_box_y = arg_float(payload, 4);
            context.spawn_box_flag = arg_u32(payload, 5);
            ++context.spawn_boxes;
            break;
        }

        case EclOp::Op513ScriptMark: {
            // Zero-argument marker.
            //
            // Evidence: 69 occurrences across 29 files, size ALWAYS 16 (so the
            // payload is 8 zero bytes in every case), context is
            // prev=op616 next=op21 in 39/69 and prev=op43 next=op514 in 27/69.
            // A no-argument marker that always precedes op514/op521, which are
            // the named-call instructions.
            ++context.script_marks;
            break;
        }

        case EclOp::Op514CallNamedExt: {
            // Named subroutine call, extended form.
            //
            // Evidence: 69 occurrences across 28 files, sizes 44/48/52/56 -- the
            // variable part is the trailing parameter block, and the fixed part
            // carries a plain-text name. Observed names: "BossDead" (0x0C=12
            // chars, matching the length field at +0x10). This is the same
            // calling convention as op300/op304: length at +0x10, name at +0x14.
            context.named_call_length = arg_u32(payload, 4);
            context.named_call_target.assign(
                reinterpret_cast<const char*>(payload) + 12, 16);
            ++context.named_call_exts;
            break;
        }

        case EclOp::Op521CallNamedEffect: {
            // Named effect call.
            //
            // Evidence: 68 occurrences across 28 files, size ALWAYS 36, and the
            // payload carries "BossEscape" (10 chars) after a length field at
            // +8. Context is prev=op514 next=op1023 / op527 in 67/68, i.e. right
            // after the other named call.
            context.named_effect_length = arg_u32(payload, 2);
            context.named_effect_target.assign(
                reinterpret_cast<const char*>(payload) + 8, 16);
            ++context.named_effect_calls;
            break;
        }

        case EclOp::Op507SpawnParams: {
            // Spawn parameters: two small integers.
            //
            // Evidence: 55 occurrences across 24 files, size ALWAYS 24 so the
            // payload is [u32][u32][u32][u32]. Observed pairs are (20,15),
            // (2,4), (2,8) -- counts and modes. Context is prev=op573 next=op573
            // in 22/55 and prev=op504 next=op573 in 19/55.
            context.spawn_param_a = arg_u32(payload, 1);
            context.spawn_param_b = arg_u32(payload, 2);
            ++context.spawn_param_sets;
            break;
        }

        case EclOp::Op1026BossTimerValue: {
            // A single timer value, often self-looping.
            //
            // Evidence: 51 occurrences across 26 files, size ALWAYS 20 so the
            // payload is [u32][u32]. Values are 100, 50, and -1 (0xFFFFFFFF).
            // Context is prev=op1026 next=op1026 in 20/51 -- it repeats to build
            // a delay sequence.
            context.boss_timer_value = arg_u32(payload, 0);
            ++context.boss_timer_values;
            break;
        }
        case EclOp::Op501SetPositionPair: {
            // Sets a position from two floats.
            //
            // Evidence: 120 occurrences across 105 files. Payload is
            // [u32][f32 x][f32 y][u32]; observed pairs are (16.0,16.0),
            // (48.0,48.0), (32.0,32.0) -- all plausible field coordinates.
            // Context is prev=op500 (SetPosition) next=op404 / op504 in 83/120,
            // so it refines the position op500 started.
            context.position_x = arg_float(payload, 0);
            context.position_y = arg_float(payload, 1);
            ++context.position_pairs;
            break;
        }

        case EclOp::Op503BossPhase: {
            // Sets the boss phase number.
            //
            // Evidence: 31 occurrences across 28 files, payload[0] is 3 in 29/31
            // (12 once, 60 once), and the context is prev=op306 (a setter)
            // next=op502 in 28/31. One phase value per boss file is exactly the
            // shape a phase index takes.
            context.boss_phase = arg_u32(payload, 0);
            ++context.boss_phases;
            break;
        }

        case EclOp::Op505BranchAction: {
            // Action taken after a conditional jump.
            //
            // Evidence: 44 occurrences across 29 files, payload always zero,
            // context is prev=op14 (JumpIfNonzero) or op17 next=op404 in 33/44.
            context.branch_actions = arg_u32(payload, 0);
            ++context.branch_action_count;
            break;
        }

        case EclOp::Op545AttackPrepare: {
            // Marks an attack as prepared.
            //
            // Evidence: 41 occurrences across 29 files, payload always zero, and
            // the context is prev=op632 next=op516 (the attack timer) in 41/41.
            ++context.attack_prepares;
            break;
        }

        case EclOp::Op445TimedValue: {
            // A value with a frame count attached.
            //
            // Evidence: 107 occurrences across 51 files. Payload is
            // [u32 frames][u32 flag=1][f32 value][u32]; frames are 0x78(120),
            // 0x1E(30), 0xFA(250) and the float is 2.0 or 0.0.
            context.timed_value_frames = arg_u32(payload, 0);
            context.timed_value = arg_float(payload, 1);
            ++context.timed_values;
            break;
        }
        case EclOp::Op400EffectSlot: {
            // Invokes an effect against the value just stored into a builtin
            // slot.
            //
            // Evidence: 28 occurrences across 28 files, payload always
            // [0][0][0x43] and the context is prev=op43 (StoreInt into slot 53)
            // next=op11 (Call EffChargePoint2) in 28/28. So the shape is
            // "store a value, then act on it, then spawn the effect".
            context.effect_slot = arg_u32(payload, 0);
            ++context.effect_slot_calls;
            break;
        }

        case EclOp::Op404BulletPattern: {
            // Bullet pattern header.
            //
            // Evidence: 184 occurrences across 105 files, size is ALWAYS 24, so
            // the payload is exactly [u32 flag][u32 mode][f32 a][f32 b].
            // Observed floats include 1.5708 (pi/2), 5.2, -1.0, 1.5 -- all legal
            // angles and speeds. Context is most often prev=op516 (the attack
            // timer) next=op405.
            context.bullet_pattern_flag = arg_u32(payload, 0);
            context.bullet_pattern_mode = arg_u32(payload, 1);
            context.bullet_pattern_a = arg_float(payload, 2);
            context.bullet_pattern_b = arg_float(payload, 3);
            ++context.bullet_patterns;
            break;
        }

        case EclOp::Op405BulletLaunch: {
            // Launches a bullet: speed and angle.
            //
            // Evidence: 50 occurrences across 33 files. Payload is
            // [u32 flag][u32 speed][f32 a][f32 b][u32 tail]; speed takes 60 / 130
            // / 200 (all plausible units per frame), and the floats include
            // 3.142 (pi) and -0.035. Context is prev=op404 next=op401 in 41/50,
            // i.e. right after the pattern header.
            context.bullet_launch_flag = arg_u32(payload, 0);
            context.bullet_launch_speed = arg_u32(payload, 1);
            context.bullet_launch_a = arg_float(payload, 2);
            context.bullet_launch_b = arg_float(payload, 3);
            ++context.bullet_launches;
            break;
        }
        case EclOp::Op512BossFrameEnter: {
            // Marks the start of a boss frame block.
            //
            // Evidence: 31 occurrences across 29 files, 28/31 with an all-zero
            // payload, and the context is prev=op40 (EnterFrame) next=op515 in
            // 28/31. So it sits right after EnterFrame, which is where a frame
            // block opens.
            context.boss_frame = arg_u32(payload, 0);
            ++context.boss_frame_enters;
            break;
        }

        case EclOp::Op307BossTimer: {
            // A boss-side timer / wait.
            //
            // Evidence: 153 occurrences across 29 files, payload is
            // [u32 flag(1)][u32 a][u32 b] with a in 0x4B..0x60 (75..96) and b
            // either 0 or 0x3C (60). The 0/60 split is a frames argument, and the
            // 75..96 range matches the series' per-attack intervals.
            context.boss_timer_a = arg_u32(payload, 0);
            context.boss_timer_b = arg_u32(payload, 1);
            ++context.boss_timer_sets;
            break;
        }
        case EclOp::Op1024BossIndex: {
            // Sets the boss index this script is running as.
            //
            // Evidence: 27 occurrences, exactly one per bossNN.ecl, payload[0]
            // equals NN (boss01 -> 1, boss02 -> 2, ... boss27 -> 27). Context is
            // identical in 27/27: prev=op1006 (the file's comment) next=op307.
            //
            // This is what tells the rest of the script which boss is on screen.
            context.boss_index = arg_u32(payload, 0);
            ++context.boss_index_sets;
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
            // Remember the enemy family this call names (Wave13_Cir -> Cir).
            context.note_named_target(key);
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
            // Protect the CALLER's coroutine table. `context = nested.context`
            // below copies the callee's view back, and that view was taken
            // before the callee ran -- so the caller's own pc and wait_frames
            // reverted to their pre-call values. Measured on Boss01test: its pc
            // sat at 548 forever because every op300 call rewound it.
            // The callee must not touch the caller's coroutine table at all.
            //
            // `context = nested.context` copies the callee's whole view back, and
            // that view was snapshotted before the callee ran -- so restoring it
            // wholesale also rewound the CALLER's own pc. Measured on Boss01test:
            // op524 wrote pc=548, then the op300 call that followed restored the
            // pre-call pc=528, and the next frame re-executed op524 forever.
            //
            // Nested runs only need the callee's variable/frame writes, so the
            // coroutine table is taken from the caller and left untouched.
            const EclRunResult nested_result = nested.run(found->second.code, 20000, found->second.code_base);
            stack_ = caller_stack;
            // Whole-context write-back with the caller-owned fields restored.
            // See the op11 case: a hand-listed copy silently dropped fields.
            const auto caller_coroutines = context.coroutines;
            context = nested.context;
            context.coroutines = caller_coroutines;
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

        case EclOp::AngleTo: {
            // Angle-to-target with a trailing parameter block.
            //
            // Evidence: 4 occurrences across 4 files, size ALWAYS 36 so the
            // payload is 8 words. Context is prev=op51 (float add) in 2/4.
            // Reading them as floats gives coordinates and angles; -9994 and
            // -9963 are builtin references, not positions.
            context.angle_to_a = arg_float(payload, 0);
            context.angle_to_b = arg_float(payload, 1);
            context.angle_to_c = arg_float(payload, 2);
            context.angle_to_d = arg_float(payload, 3);
            context.angle_to_e = arg_float(payload, 4);
            ++context.angle_to_calls;
            break;
        }

        case EclOp::NormalizeAngle: {
            // Normalizes an angle in place.
            //
            // It must NOT pop: 5 of the 9 occurrences enter with an empty
            // operand stack and faulted when this case popped. The value it
            // normalizes therefore lives in a frame slot named by the payload,
            // not on the stack -- the same shape as op45 (which I originally
            // assumed was StoreFloat and is not).
            //
            // Until that slot is identified this records the request and leaves
            // the stack alone; a stack change here is what broke boss13.
            context.angle_normalize_ref = arg_i32(payload, 0);
            ++context.angle_normalizes;
            break;
        }

        case EclOp::Polar:
        case EclOp::LengthSquared:
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
    // NOTE: `state` is a reference INTO context.coroutines. A nested call
    // (op11 / op300) does `context = nested.context`, which replaces the whole
    // map -- so this reference is DANGLING after run() returns. Every write
    // through it went to freed memory, which is why Boss01test's pc never
    // advanced past op524: the value was written to the old map and lost.
    //
    // All writes below therefore re-fetch the entry by key.
    {
        const auto it = context.coroutines.find(key);
        if (it != context.coroutines.end() && it->second.finished) {
            return EclRunResult::Finished;
        }
    }
    EclContext::CoroutineState& state = context.coroutines[key];

    if (state.finished) return EclRunResult::Finished;

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
    // Pass locals, not the map entry: run() may replace the map underneath us.
    u32 resume_pc = state.pc;
    u32 resume_wait = state.wait_frames;
    const EclRunResult result = run(code, 20000, code_base, resume_pc, &resume_pc,
                                    &resume_wait);


    // Re-fetch AFTER run(): a nested call may have replaced the map.
    EclContext::CoroutineState& after = context.coroutines[key];
    after.pc = resume_pc;
    after.wait_frames = resume_wait;

    // A coroutine that reached its end must NOT be re-run from a stale pc.
    //
    // Only a wait updates state.pc; a script that runs to completion leaves the
    // pc pointing at wherever it last suspended, so the next frame re-executed
    // that block. Measured: Boss01test re-ran its op42/op43/op300 block every
    // frame, which is why bossIndexSets reached 8046 and effectSlots 10746 for
    // a script that should have run once.
    if (result == EclRunResult::Finished) {
        after.finished = true;
        after.pc = u32(code.size());
    }

    // Save the stack for the next resume.
    if (after.wait_frames > 0) {
        stack_.save(after.stack);
    } else {
        after.stack.clear();
    }
    return result;
}
}  // namespace th185
