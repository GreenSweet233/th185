#pragma once
// TH185 ANM container parser (C++ mirror of scripts/native/anm.py).
//
// Structure follows thtk's anm_types.h -- the vendored copy lives at
// th185_web/reference/thtk/anm_types.h. TH185 reports version 8, which is the
// TH13+ layout (anm_header11_t). Do NOT re-derive this from bytes; the previous
// attempt got three fields wrong and cost a full round.
//
//   header (anm_header11_t)
//     +0x00 u32 version        = 8
//     +0x04 u16 sprites        number of sprite_t records
//     +0x06 u16 scripts
//     +0x08 u16 zero1
//     +0x0A u16 width, +0x0C u16 height
//     +0x0E u16 format         1/3/5/7, see AnmTexture.hpp
//     +0x10 u32 nameoffset     atlas path, or "@" for an in-file atlas
//     +0x14 u16 x, y
//     +0x18 u32 memorypriority
//     +0x1C u32 thtxoffset     first THTX block
//     +0x20 u16 hasdata
//     +0x22 u16 lowresscale    TH14+
//     +0x24 u32 nextoffset
//
//   then, 4-aligned after the atlas name:
//     sprite_t[sprites]        { u32 id; f32 x, y, w, h; } = 20 bytes each
//
//   then script instructions, then THTX blocks.

#include "Types.hpp"

#include <string>
#include <vector>

namespace th185 {

// sizeof(anm_header11_t) = 0x40, NOT 0x28: the struct ends with
// `uint32_t zero2[6]` (24 bytes). Getting this wrong shifts the sprite and
// script offset tables by 24 bytes, so the tables read plausible-looking
// garbage. The Python side was fixed to 0x40 first; this C++ copy lagged.
constexpr u32 ANM_HEADER_SIZE = 0x40;
constexpr u32 ANM_SPRITE_SIZE = 20;
constexpr u32 ANM_VERSION = 8;

struct AnmSprite {
    u32 id = 0;
    float x = 0, y = 0, w = 0, h = 0;
};

struct AnmTexture {
    u32 offset = 0;      // offset of the THTX header inside the blob
    u16 format = 0;
    u16 width = 0;
    u16 height = 0;
    u32 size = 0;        // payload bytes after the 16-byte header
};

struct AnmInstruction {
    u16 opcode = 0;
    u16 length = 0;
    u32 offset = 0;
};

struct AnmScriptEntry {
    u32 id = 0;
    u32 offset = 0;
    std::vector<AnmInstruction> instructions;
    // Raw instruction bytes, so the interpreter can walk them directly.
    std::vector<u8> bytes;
};

struct AnmResource {
    // Kept so texture payload pointers stay valid.
    std::vector<u8> data;

    u32 version = 0;
    u32 block_count = 0;   // number of chained blocks in this file
    u16 sprite_count = 0;
    u16 script_count = 0;
    u16 width = 0;
    u16 height = 0;
    u16 format = 0;
    u32 name_offset_ = 0;
    u32 memory_priority = 0;
    u32 thtx_offset_ = 0;
    u16 has_data = 0;
    u16 low_res_scale = 0;
    u32 next_offset_ = 0;
    std::string atlas;
    u32 sprite_offset = 0;
    std::vector<AnmSprite> sprites;
    std::vector<AnmTexture> textures;
    std::vector<AnmScriptEntry> script_list;

    // Parses `blob`. Returns false and fills `error` on failure.
    bool parse(std::vector<u8> blob, std::string* error);
};

}  // namespace th185
