#pragma once
// TH185 save file (scoreth185.dat).
//
// The file is a 24-byte header plus ONE encrypted, LZSS-compressed blob. Both
// stages reuse code already reversed for th185.dat, which is why this was cheap
// to add:
//
//   decryption   — same routine as PBG4 (RVA 0x1E90),
//                  key 0xAC, step 0x35, block 0x10, limit = compressed size
//   decompression— same LZSS (RVA 0x71E70)
//
// Confirmed by disassembly of the loader at RVA 0x69440:
//   069452  cmp dword ptr [eax], 0x54313835   ; "581T"
//   06945E  cmp word  ptr [eax + 8], 6        ; version
//   069469  mov edx, [eax + 0x10]             ; compressed size
//   06946C  lea ecx, [eax + 0x18]             ; payload
//   069471  push edx / push 0x10 / push 0x35 / push 0xac
//   06947B  call 0x1E90                       ; decrypt
//   0694A0  call 0x71E70                      ; LZSS
//
// Decompressed layout: a sequence of self-describing records.
//
//   struct SaveRecord {
//       uint16 magic;    // "CR" 0x5243 or "ST" 0x5453
//       uint16 version;  // 3 for CR, 4 for ST
//       uint32 value;    // CR: score-like value; ST: unused
//       uint32 length;   // total length including this header
//   };
//
// A real save has two CR records (78176 bytes each) then one ST record
// (1888 bytes); 78176 + 78176 + 1888 = 158240, exactly the declared
// decompressed size, so the walk validates itself.
//
// Inside a CR record, from +0x10, are 32-byte slots in groups of 10:
//
//   struct SaveSlot {
//       uint32 score;    // decreases within a group
//       uint8  flag_a;   // 1 in every observed slot
//       uint8  flag_b;   // 0 or 1 depending on the record
//       char   name[8];  // "--------" means empty
//       uint8  extra[20];// zero in every observed slot
//   };
//
// Verification: `python th185_web/scripts/native/save.py --selftest` (14 checks).

#include "Types.hpp"

#include <string>
#include <vector>

namespace th185 {

constexpr u32 SAVE_MAGIC = 0x54313835u;   // "581T"
constexpr u16 SAVE_VERSION = 6;
constexpr u32 SAVE_HEADER_SIZE = 24;

constexpr u32 SAVE_KEY = 0xAC;
constexpr u32 SAVE_STEP = 0x35;
constexpr u32 SAVE_BLOCK = 0x10;

constexpr u16 SAVE_RECORD_CR = 0x5243;    // "CR"
constexpr u16 SAVE_RECORD_ST = 0x5453;    // "ST"

constexpr u32 SAVE_SLOT_SIZE = 32;
constexpr u32 SAVE_SLOT_GROUP = 10;
constexpr u32 SAVE_CR_HEADER = 0x10;

struct SaveHeader {
    u32 magic = 0;
    u32 file_size = 0;
    u16 version = 0;
    u16 reserved = 0;
    u32 constant = 0;        // 0x100
    u32 compressed_size = 0;
    u32 decompressed_size = 0;
};

struct SaveSlot {
    u32 score = 0;
    // +0x04 and +0x05 are two independent u8 fields, not one u16. Observed
    // bytes: "01 00" in one CR record, "01 01" in another.
    u8 flag_a = 0;
    u8 flag_b = 0;
    std::string name;        // 8 characters, "--------" when empty
};

struct SaveRecord {
    u16 magic = 0;
    u16 version = 0;
    u32 value = 0;
    u32 length = 0;
    std::size_t offset = 0;
    std::vector<SaveSlot> slots;   // CR records only
};

struct SaveFile {
    SaveHeader header;
    std::vector<u8> raw;           // decompressed payload
    std::vector<SaveRecord> records;
    u32 consumed = 0;

    // Decode `blob`. Returns false and fills `error` on failure.
    bool parse(std::vector<u8> blob, std::string* error);

    // True when the records cover the whole decompressed buffer.
    bool is_self_consistent() const;

    // Number of CR records (one per playthrough entry).
    u32 cr_count() const;

    // Number of ST records (settings).
    u32 st_count() const;
};

}  // namespace th185
