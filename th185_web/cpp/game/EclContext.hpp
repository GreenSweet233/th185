#pragma once
// TH185 ECL execution context: variable storage + reference resolution.
//
// This is what EclInterpreter was missing. Without it, StoreInt/StoreFloat
// could not run (they need a destination address) and 2612 instructions
// (10.99%) stayed unreachable.
//
// ## Reference encoding (recovered, verified)
//
// Instruction arguments carry a per-argument flag: the low 8 bits of the
// instruction's param_mask say whether argument i is a REFERENCE (1) or a
// LITERAL (0). Measured: op 43 (StoreInt) is 0xFF for all 948 occurrences,
// i.e. every argument is a reference. That is the tell that Store* needs
// resolution.
//
// A reference value is decoded as:
//
//   value < 0   ->  engine builtin slot, index = value + 10000
//                   (0..110, matching the dispatcher bound cmp eax,0x6e)
//   value >= 0  ->  frame-local byte offset, always a multiple of 4
//
// Both rules were verified across all 948 StoreInt references in the shipped
// data: every negative value maps into 0..110, every non-negative value is a
// multiple of 4. That is why this is implemented rather than guessed.
//
// ## What is NOT here
//
// The builtin SLOTS exist (so StoreInt can write them), but their semantics
// are mostly unknown. Only a handful were identified in docs/ECL.md:
//   41 difficulty, 47..50 is_difficulty_0..3, 51..54 EM+0x3c/40/44/4f44,
//   110 stage-2. Reading an unidentified slot yields 0 rather than a guess.

#include "EclOwner.hpp"
#include "Types.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace th185 {

// Reference encoding base: a negative argument value v maps to builtin slot
// (v + ECL_BUILTIN_BASE).
constexpr i32 ECL_BUILTIN_BASE = 10000;

// Number of builtin slots reachable through the dispatcher (0..110).
constexpr u32 ECL_BUILTIN_COUNT = 111;

// Builtin slot indices, identified in docs/ECL.md. Only these are named; any
// other negative reference reads as 0 rather than a guess.
constexpr u32 BUILTIN_SLOT_DIFFICULTY = 41;
constexpr u32 BUILTIN_SLOT_IS_DIFFICULTY_0 = 47;
constexpr u32 BUILTIN_SLOT_IS_DIFFICULTY_1 = 48;
constexpr u32 BUILTIN_SLOT_IS_DIFFICULTY_2 = 49;
constexpr u32 BUILTIN_SLOT_IS_DIFFICULTY_3 = 50;
constexpr u32 BUILTIN_SLOT_STAGE_MINUS_2 = 110;

// Frame-local storage: 64 slots of 4 bytes = 256 bytes of addressable offset.
constexpr u32 ECL_FRAME_BYTES = 256;

// One callable subroutine: its name plus the instruction stream to run.
// Named EclCallable to avoid colliding with EclSubroutine in EclResource.hpp,
// which describes the parsed form of the same thing.
struct EclCallable {
    std::string name;
    std::vector<u8> code;
    // Offset in the original file that code[0] corresponds to. Jump operands are
    // absolute file offsets, so a sliced buffer needs this to resolve them.
    u32 code_base = 0;
};

struct EclContext {
    // Thread table. Spawn/thread ops resolve through here.
    EclOwner owner;

    // Engine builtin slots, addressed by (value + ECL_BUILTIN_BASE).
    std::array<u32, ECL_BUILTIN_COUNT> builtins{};

    // Frame-local storage. Frames are pushed/popped by op 40/41.
    std::vector<std::array<u8, ECL_FRAME_BYTES>> frames;

    // Difficulty mask used by the rank check (op-level, not per-instruction).
    u8 difficulty_mask = 0xFF;

    // Callable subroutines, keyed by name. Call (op 11) resolves through here.
    std::unordered_map<std::string, EclCallable> subroutines;

    // Call depth guard: the original has a stack limit too, and an unbounded
    // recursion would hang the interpreter instead of failing.
    static constexpr u32 MAX_CALL_DEPTH = 32;
    u32 call_depth = 0;

    // --- record-only opcodes -------------------------------------------
    //
    // A group of TH185 opcodes carry only data: their payload[0] is 0 in 100%
    // of occurrences (verified per opcode across the whole corpus), which is
    // the same shape as the confirmed data-only ops (op23/op302/op1015). They
    // do not touch the operand stack, so recording them is safe even though
    // their semantics are unknown -- the stack stays correct and the values
    // are observable.
    //
    // This is deliberately NOT a claim of understanding. If a recorded opcode
    // later turns out to pop a value, the stack-balance check in the harness
    // will catch it.
    struct RecordedOp {
        u16 opcode = 0;
        std::array<u32, 8> words{};
        u32 word_count = 0;
    };
    std::vector<RecordedOp> recorded;
    u32 recorded_total = 0;

    void record_op(u16 opcode, const u8* payload, u32 payload_size);

    // Register a subroutine so Call can find it.
    void add_subroutine(std::string name, std::vector<u8> code);

    // Publishes the run's difficulty into the builtin slots the ECL scripts
    // read. `difficulty` is 0..7; the is_difficulty_N slots are set to 1 for
    // the matching tier and 0 for the rest, which is what the original's
    // virtual predicates return.
    void set_difficulty(u32 difficulty);

    // --- op 1013: spawn schedule ---------------------------------------
    //
    // Structure is fully determined (1021/1021 occurrences):
    //   u32 reserved(0), u32 slot, u32 time[8], u32 reserved(0)
    // and all 1021 time[8] sequences are monotonically non-decreasing.
    //
    // The SEMANTICS are inferred, not proven: `slot` takes only
    // 0,1,4,5,8,9,12,13,16,17,18 (grouped in pairs) and the times are
    // frame-like integers, which reads as "spawn wave `slot` at these 8
    // times". Recorded rather than acted on.
    struct SpawnSchedule {
        u32 slot = 0;
        std::array<u32, 8> times{};
    };
    std::vector<SpawnSchedule> schedules;

    // --- op 1011: wave template selector ---------------------------------
    //
    // Payload: one argument. Measured across all 187 occurrences: values span
    // -1..104 with 102 distinct values, which lines up with the 111-entry wave
    // name table at VA 0x4B5D50 (docs/WAVES.md). Read as "this wave script
    // generates template N".
    //
    // -1 appears as a sentinel (no template / keep current).
    // Subroutine names reached through Call, for diagnostics.
    std::vector<std::string> calls_made;

    // --- bullet emission (600 family) -----------------------------------
    //
    // The 600 family is how a wave subroutine describes a bullet pattern.
    // Structure recovered from wave01.ecl's Wave01_GirlA_at:
    //
    //   op600 (mode)                     pattern class
    //   op607 (submode)                  pattern variant
    //   op602 (a, b)                     two integers
    //   op606 (a, b)                     two integers
    //   op604 (a, b, speed, 0)           speed / acceleration
    //   op44  x N                        N angles pushed on the stack
    //   op605 (count, lo, hi)            N bullets, angular range
    //   op611 (a, b, ...)                EMIT -- consumes the stack
    //   op535 (d0..d7)                   per-bullet delay in frames
    //
    // op611 consumes the values pushed by the op44 sequence, which is why it
    // is listed as net 0 in the inferred table: the pushes and the consume
    // cancel out over the whole block.
    struct BulletRequest {
        u32 mode = 0;
        u32 submode = 0;
        u32 count = 0;
        float speed = 0.0f;
        float angle_lo = 0.0f;
        float angle_hi = 0.0f;
        std::array<u32, 8> delays{};
        std::vector<float> angles;   // pushed by op44 before op611
        u32 emitted = 0;
    };
    std::vector<BulletRequest> bullet_requests;
    // Working values accumulated between op600 and op611.
    BulletRequest pending_bullet;
    // Cumulative record of delays attached to an emitted request, for tests:
    // bullet_requests is drained by Application each frame.
    std::vector<std::array<u32, 8>> delays_seen;
    u32 requests_total = 0;

    // --- coroutine state ------------------------------------------------
    //
    // The interpreter is a REPLAY interpreter: it restarts a subroutine from
    // instruction 0 every time run() is called. That is correct for decoding
    // and for stack-balance checks, but it cannot express `op23` (wait N
    // frames), because a restart forgets where it was.
    //
    // The scene controller needs the coroutine behaviour: it must stop at the
    // wait, resume after N frames, and continue from the NEXT instruction.
    // This state makes that possible without changing run()'s contract --
    // when `yield_on_wait` is false (the default) nothing here is consulted.
    struct CoroutineState {
        u32 pc = 0;             // resume point, a buffer index
        u32 wait_frames = 0;    // frames still to wait; 0 = not waiting
        bool started = false;
        // Reached the end of its code. A finished coroutine must not be
        // re-entered: only a wait updates pc, so without this flag the next
        // frame re-ran the block it last suspended in.
        bool finished = false;
        // Operand stack snapshot. A coroutine resumes MID-subroutine, so the
        // values pushed before the wait must survive: `main` pushes -9891 then
        // immediately branches on it, and resuming with an empty stack loses
        // that value and takes the wrong branch.
        std::vector<std::pair<u32, u32>> stack;   // (type, bits), bottom first
    };
    std::unordered_map<std::string, CoroutineState> coroutines;
    std::vector<std::string> missing_calls;
    std::vector<i32> wave_templates;
    // Templates declared by the CURRENT WorldWaveNN block. WorldWave01 in
    // world01.ecl pushes 11 consecutive op1011 values before op1012 closes
    // the block, so op1011 is a LIST of templates to spawn, not a single
    // "current" template.
    std::vector<i32> pending_templates;
    // Completed template lists, one per WorldWaveNN invocation.
    std::vector<std::vector<i32>> wave_blocks;
    i32 last_wave_template = -1;

    // --- op 23: timer / wait value -------------------------------------
    //
    // Structure: u32 reserved(0), then one argument (frame-like integers:
    // 60, 120, 30, 10, 240, 1000, or a negative builtin reference such as
    // -9985). Does not touch the stack. Semantics inferred as a timer/wait.
    u32 last_timer_value = 0;
    u32 timer_writes = 0;
    // Times op601 found fewer stack values than it expected. A non-zero value
    // means some call site enters the pattern loop with a short stack, which
    // is a stack-balance signal rather than a hard error.
    u32 short_pops = 0;
    // The "stage signal" flag that op1017 raises and op1019 waits on. See the
    // op1017 case for how the two were identified from control flow.
    // Which boss this script is running as (op1024). See the case for evidence.
    u32 spawn_commits = 0;
    u32 scene_syncs = 0;
    u32 scene_sync_count = 0;
    u32 wait_value = 0;
    u32 wait_values = 0;
    i32 angle_normalize_ref = 0;
    u32 angle_normalizes = 0;
    u32 named_tail_length = 0;
    std::string named_tail_target;
    u32 named_tail_calls = 0;
    std::array<float, 10> float_block{};
    u32 float_blocks = 0;
    u32 effect_spawn_mode = 0;
    float effect_spawn_scale = 0.0f;
    u32 effect_spawns = 0;
    float angle_to_a = 0.0f;
    float angle_to_b = 0.0f;
    float angle_to_c = 0.0f;
    float angle_to_d = 0.0f;
    float angle_to_e = 0.0f;
    u32 angle_to_calls = 0;
    std::array<u32, 14> pattern_ext{};
    u32 pattern_exts = 0;
    u32 pattern_count = 0;
    u32 pattern_counts = 0;
    u32 frame_value = 0;
    u32 frame_values = 0;
    u32 boss_extra_a = 0;
    u32 boss_extra_b = 0;
    u32 boss_extra_c = 0;
    u32 boss_extras = 0;
    float boss_config_radius = 0.0f;
    u32 boss_config_flags = 0;
    u32 boss_configs = 0;
    u32 attack_slots = 0;
    u32 spawn_count_a = 0;
    u32 spawn_count_b = 0;
    u32 spawn_counts = 0;
    float spawn_rect_w = 0.0f;
    float spawn_rect_h = 0.0f;
    u32 spawn_rects = 0;
    u32 boss_wait_marks = 0;
    std::array<float, 11> angle_table{};
    u32 angle_tables = 0;
    u32 pattern_extra_frames = 0;
    u32 pattern_extra_mode = 0;
    float pattern_extra_scale = 0.0f;
    u32 pattern_extra_tail = 0;
    u32 pattern_extras = 0;
    u32 pattern_aux_a = 0;
    u32 pattern_aux_b = 0;
    u32 pattern_aux_c = 0;
    u32 pattern_aux_count = 0;
    float spawn_box_x = 0.0f;
    float spawn_box_y = 0.0f;
    u32 spawn_box_flag = 0;
    u32 spawn_boxes = 0;
    u32 spawn_param_a = 0;
    u32 spawn_param_b = 0;
    u32 spawn_param_sets = 0;
    u32 script_marks = 0;
    u32 named_call_length = 0;
    std::string named_call_target;
    u32 named_call_exts = 0;
    u32 named_effect_length = 0;
    std::string named_effect_target;
    u32 named_effect_calls = 0;
    u32 boss_timer_value = 0;
    u32 boss_timer_values = 0;
    float position_x = 0.0f;
    float position_y = 0.0f;
    u32 position_pairs = 0;
    u32 boss_phase = 0;
    u32 boss_phases = 0;
    u32 branch_actions = 0;
    u32 branch_action_count = 0;
    u32 attack_prepares = 0;
    u32 timed_value_frames = 0;
    float timed_value = 0.0f;
    u32 timed_values = 0;
    u32 effect_slot = 0;
    u32 effect_slot_calls = 0;
    u32 bullet_pattern_flag = 0;
    u32 bullet_pattern_mode = 0;
    float bullet_pattern_a = 0.0f;
    float bullet_pattern_b = 0.0f;
    u32 bullet_patterns = 0;
    u32 bullet_launch_flag = 0;
    u32 bullet_launch_speed = 0;
    float bullet_launch_a = 0.0f;
    float bullet_launch_b = 0.0f;
    u32 bullet_launches = 0;
    u32 boss_frame = 0;
    u32 boss_frame_length = 0;
    u32 boss_frame_lengths = 0;
    u32 boss_attack_a = 0;
    u32 boss_attack_b = 0;
    u32 boss_attack_timers = 0;
    u32 boss_frame_enters = 0;
    u32 boss_timer_a = 0;
    u32 boss_timer_b = 0;
    u32 boss_timer_sets = 0;
    u32 boss_index = 0;
    u32 boss_index_sets = 0;
    u32 stage_signal = 0;
    u32 stage_signal_sets = 0;
    u32 stage_signal_waits = 0;
    // Times op601 found fewer values than it expected; a signal that the

    // --- op 1015: field query ------------------------------------------
    //
    // Structure (212 occurrences, every field checked against the full set):
    //   u32 reserved(0), u32 reference, u32 field, u32 reserved(0)
    //   reference is 0 (108x) or a builtin reference -9985 (104x)
    //   field takes only 16, 0, 1, 8, 12
    // Does not touch the stack. Semantics inferred as a field query; recorded.
    u32 query_count = 0;
    u32 last_query_reference = 0;
    u32 last_query_field = 0;

    // --- op 302: set a/b pair -------------------------------------------
    // Structure: u32 reserved(always 0), u32 a, u32 b. Does not touch the stack.
    u32 set_count = 0;
    u32 last_set_a = 0;
    u32 last_set_b = 0;

    // op500: position pair. op1006: comment text.
    float last_position_x = 0.0f;
    float last_position_y = 0.0f;
    u32 position_sets = 0;
    std::vector<std::string> comments;

    // Push a fresh zeroed frame. Returns its index.
    u32 push_frame();

    // Drop the newest frame. Returns false when there is none.
    bool pop_frame();

    // Resolve a reference to a writable integer slot.
    //   value < 0  -> builtin slot
    //   value >= 0 -> current frame byte offset (must be 4-aligned and in range)
    // Returns nullptr when the reference is out of range.
    u32* resolve_int_ref(i32 value);

    // Read a reference as an integer.
    i32 read_int_ref(i32 value) const;

    // Write through a reference. Returns false when out of range.
    bool write_int_ref(i32 value, i32 stored);

    // Same, for float bit patterns.
    bool write_float_ref(i32 value, float stored);

    // Decode a reference without touching storage; useful for diagnostics.
    enum class RefKind : u8 { Builtin, FrameLocal, Invalid };
    static RefKind classify(i32 value);
};

}  // namespace th185
