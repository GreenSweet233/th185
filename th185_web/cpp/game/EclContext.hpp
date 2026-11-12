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

#include "Types.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace th185 {

// Reference encoding base: a negative argument value v maps to builtin slot
// (v + ECL_BUILTIN_BASE).
constexpr i32 ECL_BUILTIN_BASE = 10000;

// Number of builtin slots reachable through the dispatcher (0..110).
constexpr u32 ECL_BUILTIN_COUNT = 111;

// Frame-local storage: 64 slots of 4 bytes = 256 bytes of addressable offset.
constexpr u32 ECL_FRAME_BYTES = 256;

// One callable subroutine: its name plus the instruction stream to run.
// Named EclCallable to avoid colliding with EclSubroutine in EclResource.hpp,
// which describes the parsed form of the same thing.
struct EclCallable {
    std::string name;
    std::vector<u8> code;
};

struct EclContext {
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

    // Register a subroutine so Call can find it.
    void add_subroutine(std::string name, std::vector<u8> code);

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
