#pragma once
// TH185 ECL script container (SCPT / ECLH).
//
// The 114 `.ecl` files use the same "SCPT" container as TH18 (confirmed by
// thcrap is ThModern_ECLGetSub). Reverse-engineered and verified on all 114
// files: 486 subroutines, 12480 instructions, 0 parse errors, and every single
// subroutine ends with exactly 12 bytes left (see docs/ECL.md).
//
// Layout:
//
//   "SCPT" u32 version u32 name_count u32 reserved u32 sub_count ...
//   "ANIM" u32 count [names]        <- VARIABLE LENGTH section
//   "ECLI" u32 count [names]        <- ALSO VARIABLE LENGTH; offset table after
//   u32 sub_offsets[sub_count]
//   names[]                         <- NUL-terminated, sub_count of them
//   "ECLH" blocks at each offset
//
// The ANIM section is variable length: it holds `count` NUL-terminated
// animation names. That is why the offset table is NOT at a fixed position —
// locate "ECLI" first, then the table starts 8 bytes after it.
//
// ECLH block:
//
//   "ECLH" u32 header_size(16) u32 x3
//   instructions[]                  <- start 20 bytes after the tag
//
// Instruction:
//
//   u16 id u16 size u16 rank u16 argc  payload[size - 8]
//
// Verification: `python th185_web/scripts/native/ecl.py --selftest` (12 checks)
// and `ecl_cpp_check.py --selftest` for the C++ mirror.

#include "Types.hpp"

#include <string>
#include <vector>

namespace th185 {

constexpr u32 ECL_MAGIC_SCPT = 0x54504353u;  // "SCPT"
constexpr u32 ECL_MAGIC_ECLI = 0x494C4345u;  // "ECLI"
constexpr u32 ECL_MAGIC_ECLH = 0x484C4345u;  // "ECLH"
constexpr u32 ECL_MAGIC_ANIM = 0x4D494E41u;  // "ANIM"

constexpr u32 ECLH_INSTRUCTION_START = 20;  // tag + 16 header bytes
constexpr u32 ECL_TRAILER = 12;             // fixed tail after the last instruction

// One ECL instruction.
struct EclInstruction {
    u16 id = 0;
    u16 size = 0;
    u16 rank = 0;
    u16 argc = 0;
    u32 offset = 0;                 // offset inside the ECLH block
    const u8* payload = nullptr;    // points into the resource buffer
    u32 payload_size = 0;
};

// One ECLH subroutine.
struct EclSubroutine {
    u32 index = 0;
    u32 offset = 0;                 // absolute offset inside the file
    std::string name;
    u32 end = 0;
    u32 tail = 0;                   // bytes left after the last instruction
    std::vector<EclInstruction> instructions;
};

// Parsed SCPT container. Keeps a copy of the file bytes so instruction payload
// pointers stay valid.
struct EclResource {
    std::vector<u8> data;
    u32 version = 0;
    u32 name_count = 0;
    u32 sub_count = 0;
    u32 ecli_offset = 0;
    std::vector<std::string> anim_names;   // ANIM section entries
    std::vector<std::string> ecli_names;   // ECLI section: dependent .ecl files
    std::vector<std::string> names;
    std::vector<EclSubroutine> subs;

    // Parse `blob` into this resource. Returns false and fills `error`.
    bool parse(std::vector<u8> blob, std::string* error);

    // Find a subroutine by name (linear scan; only 34-40 per file).
    const EclSubroutine* find(const std::string& name) const;

    // Total instruction count across all subroutines.
    std::size_t instruction_count() const;
};

}  // namespace th185
