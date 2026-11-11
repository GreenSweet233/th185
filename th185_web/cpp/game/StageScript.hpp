#pragma once
// TH185 stage script (.std) — per-scene layout.
//
// There are 9 files (world01t + world01..08), matching the 9 scenes. A `.std`
// is NOT the wave template pool: the 111-entry name table at VA 0x4B5D50 is a
// separate global table (see docs/WAVES.md). `.std` holds one scene is layout.
//
// Reverse-engineered and verified on all 9 files (see docs/STAGE.md):
//
//   +0x00  uint16  wave_count
//   +0x02  uint16  group_count
//   +0x04  uint32  entity_offset      // absolute
//   +0x08  uint32  command_offset     // absolute; instruction stream starts +4
//   +0x0C  uint32  reserved (0)
//   +0x10  char[]  anm_name           // NUL-terminated, e.g. "world01t.anm"
//   +0x90  uint32  wave_offsets[wave_count]   // stride 60
//   ...    wave entries (60 bytes each)
//   ...    entity group table (28 bytes each) <- VARIABLE COUNT, may be 0
//   ...    entity table (16 bytes each)
//   ...    16-byte gap (always present)
//   ...    command_offset: instruction stream ([u16 id][u16 size])
//   ...    16 bytes of 0xFF at EOF
//
// IMPORTANT: the 28-byte group table between the wave entries and the entity
// table has a VARIABLE number of entries (0 for world01t..03, 15 for world04,
// 5 for world05, 7 for world06..08). The wave offsets do NOT lead straight to
// the entity table — do not assume they are adjacent.
//
// Verification: `python th185_web/scripts/native/std.py --selftest` (9 checks)
// and `stage_cpp_check.py --selftest` for the C++ mirror.

#include "Types.hpp"

#include <string>
#include <vector>

namespace th185 {

constexpr u32 STD_WAVE_TABLE_OFFSET = 0x90;
constexpr u32 STD_WAVE_ENTRY_SIZE = 60;
constexpr u32 STD_GROUP_ENTRY_SIZE = 28;
constexpr u32 STD_ENTITY_ENTRY_SIZE = 16;
constexpr u32 STD_ENTITY_GAP = 16;   // always present between entities and commands
constexpr u32 STD_COMMAND_HEADER = 4;  // the stream starts 4 bytes into command_offset
constexpr u32 STD_EOF_MARKER = 16;   // 16 bytes of 0xFF at EOF

// One wave slot (60 bytes).
struct StageWave {
    u32 offset = 0;
    u32 marker = 0;        // high 16 bits = wave type, low 16 bits = ordinal
    float f1 = 0, f2 = 0, f3 = 0;
    u32 flags = 0;
    u32 slot_id = 0;       // NOT a sequential index (world03: 0,1,2,2,2,9,10)
    u32 marker2 = 0;
};

// One 28-byte entity-group entry. Used for ring / arc formations.
struct StageGroup {
    u32 type = 0;          // observed 0..9
    float x = 0, y = 0;
    u32 reserved = 0;
    float size_a = 0;      // observed 16.0
    float size_b = 0;      // observed 512.0
    u32 marker = 0;        // 0x001C0000, last entry 0x0004FFFF
};

// One 16-byte entity placement.
struct StageEntity {
    u32 kind = 0;          // high byte 0x01; bits 16..23 type, bits 0..15 index
    float x = 0, y = 0, z = 0;
};

// One instruction in the scene command stream.
struct StageCommand {
    u16 id = 0;
    u16 size = 0;
    u32 offset = 0;
    const u8* payload = nullptr;
    u32 payload_size = 0;
};

// Parsed `.std` file. Keeps the bytes so payload pointers stay valid.
struct StageScript {
    std::vector<u8> data;
    u16 wave_count = 0;
    u16 group_count = 0;
    u32 entity_offset = 0;
    u32 command_offset = 0;
    std::string anm_name;
    std::vector<StageWave> waves;
    std::vector<StageGroup> groups;
    std::vector<StageEntity> entities;
    std::vector<StageCommand> commands;
    u32 command_end = 0;

    // Parse `blob` into this script. Returns false and fills `error`.
    bool parse(std::vector<u8> blob, std::string* error);

    // Bytes left after the last command (16 for every scene).
    u32 tail_size() const;
};

}  // namespace th185
