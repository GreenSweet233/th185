#pragma once
// TH185 ECL bytecode interpreter (skeleton: the TH11-shared opcode set).
//
// ## Scope, stated honestly
//
// This executes the 44 opcodes whose numbering TH185 shares with TH11
// (th11/th11_web/cpp/game/EclInterpreter.cpp). Measured coverage:
// 13048 of 23770 instructions = 54.89%. Everything outside that set lands in
// the default branch and is reported as unsupported rather than silently
// skipped -- a silently-skipped opcode would corrupt the stack and make every
// later result meaningless.
//
// ## What is NOT here
//
//   * TH185-only opcodes (families 300..399, 400..499, 500..699, 1000..1026,
//     plus op 23 and op 1013). See docs/ECL.md 12 for the ordering plan.
//   * Thread spawning/cancelling (op 15..21) needs an owner object that holds a
//     thread list; this skeleton has no owner, so those ops report unsupported.
//   * The game-specific escape hatch TH11 calls services.command().
//
// ## Evidence level
//
// Opcode NUMBERS matching is strong evidence; SEMANTICS matching is not proven.
// Op::PushInt = 42 is safe (high frequency, self-describing). Op::Call and
// Op::Spawn carry variable argument layouts in TH185 (Call has 15 distinct
// instruction sizes), so those need runtime validation before being trusted.
// The interpreter therefore exposes unsupported_count so a run can state
// exactly how much of a script it actually understood.

#include "EclContext.hpp"
#include "EclResource.hpp"
#include "Types.hpp"

#include <string>
#include <utility>
#include <vector>

namespace th185 {

// Opcodes shared with TH11. Kept as an enum so the switch is exhaustive and
// the compiler warns when a case is missing.
enum class EclOp : u16 {
    Nop = 0, Delete = 1, Return = 10, Call = 11, Jump = 12,
    JumpIfZero = 13, JumpIfNonzero = 14,
    Spawn = 15, SpawnNamed = 16, CancelThread = 17, SetThreadFlag = 18,
    ClearThreadFlag = 19, SetThreadState = 20, CancelSecondary = 21,
    Diagnostic = 30,
    EnterFrame = 40, LeaveFrame = 41, PushInt = 42, StoreInt = 43,
    PushFloat = 44, StoreFloat = 45,
    AddInt = 50, AddFloat = 51, SubtractInt = 52, SubtractFloat = 53,
    MultiplyInt = 54, MultiplyFloat = 55, DivideInt = 56, DivideFloat = 57,
    Remainder = 58, EqualInt = 59, EqualFloat = 60, NotEqualInt = 61,
    NotEqualFloat = 62, LessInt = 63, LessFloat = 64, LessEqualInt = 65,
    LessEqualFloat = 66, GreaterInt = 67, GreaterFloat = 68,
    GreaterEqualInt = 69, GreaterEqualFloat = 70,
    NotInt = 71, NotFloat = 72, LogicalOr = 73, LogicalAnd = 74, Xor = 75,
    Or = 76, And = 77, PostDecrement = 78, Sin = 79, Cos = 80, Polar = 81,
    NormalizeAngle = 82, Wait = 83, NegateInt = 84, NegateFloatBits = 85,
    LengthSquared = 86, AngleTo = 87, Sqrt = 88,

    // TH185-only opcodes. Payload structure is determined; semantics inferred.
    // Declared here so the switch can be exhaustive over them.
    Op23Timer = 23,          // u32 prefix, u32 timer value
    // 600 family: bullet pattern description. The interpreter records these
    // instead of acting on them; Application turns a finished BulletRequest
    // into real bullets.
    Op600BulletPattern = 600,
    Op601BulletConsume = 601,
    Op602BulletPair = 602,
    Op604BulletSpeed = 604,
    Op605BulletFan = 605,
    Op606BulletCount = 606,
    Op607BulletVariant = 607,
    Op611BulletEmit = 611,
    Op535BulletDelays = 535,
    Op1012WaveBlockEnd = 1012,
    Op1024BossIndex = 1024,
    Op512BossFrameEnter = 512,
    Op400EffectSlot = 400,
    Op401PatternAux = 401,
    Op412PatternExtra = 412,
    Op523BossWaitMark = 523,
    Op527SpawnRect = 527,
    Op540SpawnCommit = 540,
    Op536AngleTable = 536,
    Op573SpawnCount = 573,
    Op524SceneSync = 524,
    Op316BossExtra = 316,
    Op440EffectSpawn = 440,
    Op640CallNamedTail = 640,
    Op91FloatBlock = 91,
    Op612PatternExtended = 612,
    Op613FrameValue = 613,
    Op614PatternCount = 614,
    Op629BossConfig = 629,
    Op632AttackSlot = 632,
    Op504BossSpawnBox = 504,
    Op507SpawnParams = 507,
    Op513ScriptMark = 513,
    Op514CallNamedExt = 514,
    Op521CallNamedEffect = 521,
    Op1026BossTimerValue = 1026,
    Op501SetPositionPair = 501,
    Op503BossPhase = 503,
    Op505BranchAction = 505,
    Op545AttackPrepare = 545,
    Op445TimedValue = 445,
    Op404BulletPattern = 404,
    Op405BulletLaunch = 405,
    Op515BossFrameLength = 515,
    Op516BossAttackTimer = 516,
    Op307BossTimer = 307,
    Op1017StageSignalSet = 1017,
    Op1019StageSignalWait = 1019,
    Op1011WaveTemplate = 1011, // one argument: wave template index (-1..104)
    Op1013Schedule = 1013,   // u32 reserved, u32 slot, u32 time[8], u32 reserved
    Op1015Query = 1015,      // u32 reserved, u32 ref, u32 field, u32 reserved
    Op300CallNamed = 300,    // u32 flag, u32 name_len, char name[], args
    Op304CallNamedVariant = 304,  // same shape as op300
    Op302Set = 302,               // u32 reserved, u32 a, u32 b
    Op500SetPosition = 500,       // u32 reserved, f32 x, f32 y, u32 reserved
    Op1006Comment = 1006,         // u32 reserved, u32 len, char text[]
    // Thread ops, now implementable because EclContext owns a thread table.
    // (15/16/17..21 already exist in EclOp as Spawn/SpawnNamed/CancelThread/...)
};

// A value on the ECL stack. TH185 tags each slot with its type, same as TH11.
struct EclValue {
    enum class Type : u8 { Integer, Float };
    Type type = Type::Integer;
    u32 bits = 0;
};

// Fixed-capacity stack with explicit frames (op 40/41).
class EclStack {
public:
    // The original ECL stack is much larger than any single subroutine needs.
    // 1024 was too small: Wave17_Girl_at pushes 20+ values after EnterFrame(4)
    // and overflowed. 16384 gives generous headroom while still bounding a
    // runaway loop.
    static constexpr u32 CAPACITY = 16384;

    bool push(EclValue::Type type, u32 bits);
    bool pop_raw(u32& out);
    bool pop(u32& out, EclValue::Type& type);
    bool enter_frame(u32 slots);
    bool leave_frame();
    u32 depth() const { return depth_; }
    bool empty() const { return depth_ == 0; }

    // Snapshot/restore for coroutine suspension. Only the live slots are
    // copied, so the cost is proportional to the actual depth.
    void save(std::vector<std::pair<u32, u32>>& out) const;
    bool restore(const std::vector<std::pair<u32, u32>>& in);

private:

    EclValue slots_[CAPACITY]{};
    u32 depth_ = 0;
    std::vector<u32> frames_;
};

// Result of running a subroutine to completion or to a stop condition.
enum class EclRunResult : u8 {
    Finished,        // ran off the end or hit Delete
    Returned,        // hit Return with an empty frame stack
    Unsupported,     // hit an opcode this skeleton does not implement
    Fault,           // stack under/overflow, bad jump target, etc.
    Running,         // coroutine suspended on a wait; call again next frame
};

struct EclRunStats {
    u32 instructions = 0;
    u32 unsupported_count = 0;
    u32 fault_count = 0;
    u16 first_unsupported_opcode = 0;
    u32 max_stack_depth = 0;
};

// Interpreter over one subroutine.
//
// code is the subroutine instruction stream. Jump targets are byte offsets
// into that stream, matching how TH11 resolves them.
class EclInterpreter {
public:
    // Variable storage and reference resolution. StoreInt/StoreFloat need it;
    // a run without a context reports those ops as unsupported instead of
    // writing somewhere arbitrary.
    EclContext context;

    // Run at most step_budget instructions (0 = unlimited).
    //
    // `code_base` is the offset in the ORIGINAL file that code[0] corresponds
    // to. Jump operands are absolute file offsets, so a run over a sliced
    // buffer must pass the slice start or every jump lands out of range.
    // Passing 0 means code already starts at file offset 0.
    // `start_pc` begins execution at that buffer index instead of 0.
    // When `out_pc` is non-null it receives the pc to resume at, and when
    // `out_wait` is non-null an op23 wait is reported there instead of being
    // ignored.
    EclRunResult run(const std::vector<u8>& code, u32 step_budget = 0,
                     u32 code_base = 0, u32 start_pc = 0,
                     u32* out_pc = nullptr, u32* out_wait = nullptr);

    const EclRunStats& stats() const { return stats_; }

    // Call nesting depth. Lives here rather than in EclContext because the
    // context is copied back from the callee, which double-counted it: a
    // 3-deep chain (Boss23 -> Boss23Boss -> Boss23Boss1) measured as 1257.
    static constexpr u32 MAX_NESTING = 32;
    u32 nesting_ = 0;

    // Runs `code` as a COROUTINE: resumes at the saved pc, stops at op23
    // (wait N frames) by recording the wait, and returns Running. Call it once
    // per frame; it advances `wait_frames` internally.
    //
    // `key` identifies the coroutine in context.coroutines.
    EclRunResult run_coroutine(const std::string& key,
                               const std::vector<u8>& code, u32 code_base);
    const EclStack& stack() const { return stack_; }

    // Human-readable name for an opcode, or empty when not shared with TH11.
    static const char* opcode_name(u16 opcode);

    // True when this opcode is implemented by the skeleton.
    static bool is_supported(u16 opcode);

private:
    EclStack stack_;
    EclRunStats stats_;
};

}  // namespace th185
