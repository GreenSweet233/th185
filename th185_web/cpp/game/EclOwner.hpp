#pragma once
// TH185 ECL thread owner: spawn/cancel/flag management.
//
// This is the last structural dependency of the interpreter. Spawn (15),
// SpawnNamed (16) and the thread ops (17..21) need somewhere to keep a list of
// running contexts; without an owner they could only report "unsupported".
//
// ## What the payloads look like (verified across the corpus)
//
//   op 15 Spawn        [u32 prefix][u32 name_len][name at 8+prefix][args]
//                      126 uses. The name is a subroutine to start.
//   op 16 SpawnNamed   [u32 prefix][u32 name_len][name at 8+prefix][id][args]
//                      128 uses. Same shape, plus a thread id argument.
//   op 17 CancelThread [u32 reserved][u32 thread_id]        118 uses
//   op 18..20          thread flag / state, same shape
//   op 21 CancelSecondary [u32 reserved][u32 reserved]       41 uses
//
// ## What this class does and does not do
//
// It maintains the thread table and implements the bookkeeping the opcodes
// need: spawn a context (running a subroutine to its first yield), find it by
// id, set/clear its flags, cancel it. It does NOT model frame timing -- the
// interpreter runs a subroutine to completion, so a spawned thread is recorded
// and executed synchronously. That is a real limitation, documented rather
// than hidden.

#include "Types.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace th185 {

// One running ECL thread.
struct EclThread {
    i32 id = -1;
    std::string subroutine;
    u32 flags = 0;
    u32 state = 0;
    bool alive = true;
    u32 steps_run = 0;
};

struct EclOwner {
    static constexpr u32 MAX_THREADS = 256;

    std::vector<EclThread> threads;
    i32 next_id = 0;

    // Spawns a thread for `subroutine`. Returns its id, or -1 when the table
    // is full. An explicit id >= 0 is used when the opcode supplies one.
    i32 spawn(const std::string& subroutine, i32 requested_id = -1);

    // Finds a live thread by id. Returns nullptr when absent or dead.
    EclThread* find(i32 id);

    // Cancel: marks dead. Returns false when not found.
    bool cancel(i32 id);

    // Set or clear the low flag bit (op 18 / 19).
    bool set_flag(i32 id, bool on);

    // Set the state word (op 20).
    bool set_state(i32 id, u32 state);

    // Cancel every thread whose id is not the primary one (op 21).
    u32 cancel_secondary();

    // Live thread count.
    u32 live_count() const;
};

}  // namespace th185
