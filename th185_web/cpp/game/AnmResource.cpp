#include "AnmResource.hpp"

#include <cstring>

namespace th185 {

namespace {

u16 read_u16(const u8* p) noexcept { return u16(p[0] | (u16(p[1]) << 8)); }

u32 read_u32(const u8* p) noexcept {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

float read_f32(const u8* p) noexcept { return bits_float(read_u32(p)); }

}  // namespace

bool AnmResource::parse(std::vector<u8> blob, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };

    if (blob.size() < ANM_HEADER_SIZE) return fail("anm blob too small");

    data = std::move(blob);

    version = read_u32(data.data() + 0x00);
    sprite_count = read_u16(data.data() + 0x04);
    script_count = read_u16(data.data() + 0x06);
    width = read_u16(data.data() + 0x0A);
    height = read_u16(data.data() + 0x0C);
    format = read_u16(data.data() + 0x0E);
    name_offset = read_u32(data.data() + 0x10);
    memory_priority = read_u32(data.data() + 0x18);
    thtx_offset = read_u32(data.data() + 0x1C);
    has_data = read_u16(data.data() + 0x20);
    low_res_scale = read_u16(data.data() + 0x22);
    next_offset = read_u32(data.data() + 0x24);

    if (version != ANM_VERSION) return fail("unexpected anm version");
    if (name_offset >= data.size()) return fail("atlas name offset out of range");

    // Atlas name, NUL terminated.
    {
        std::size_t end = name_offset;
        while (end < data.size() && data[end] != 0) ++end;
        if (end >= data.size()) return fail("atlas name is not terminated");
        atlas.assign(reinterpret_cast<const char*>(data.data() + name_offset),
                     end - name_offset);
        // sprite_t array starts 4-aligned after the NUL.
        sprite_offset = u32((end + 1 + 3) & ~std::size_t(3));
    }

    // sprite_t records, addressed THROUGH the offset table that follows the
    // header. thtk's thanm.c:
    //   uint32_t* sprite_offsets = (uint32_t*)(map + sizeof(*header));
    //   sprite19_t* sprite = (sprite19_t*)(map + sprite_offsets[s]);
    // The records are NOT stored in order, so reading them sequentially works
    // only for files whose table happens to be ordered.
    sprites.clear();
    sprites.reserve(sprite_count);
    for (u32 i = 0; i < sprite_count; ++i) {
        const std::size_t table_at = std::size_t(ANM_HEADER_SIZE) + std::size_t(i) * 4;
        if (table_at + 4 > data.size()) break;
        const std::size_t at = read_u32(data.data() + table_at);
        if (at + ANM_SPRITE_SIZE > data.size()) break;
        AnmSprite sprite;
        sprite.id = read_u32(data.data() + at);
        sprite.x = read_f32(data.data() + at + 4);
        sprite.y = read_f32(data.data() + at + 8);
        sprite.w = read_f32(data.data() + at + 12);
        sprite.h = read_f32(data.data() + at + 16);
        sprites.push_back(sprite);
    }

    // Script table: anm_offset_t { u32 id; u32 offset; } entries, right after
    // the sprite offset array.
    script_list.clear();
    const std::size_t script_table =
        ANM_HEADER_SIZE + std::size_t(sprite_count) * 4;
    for (u32 i = 0; i < script_count; ++i) {
        const std::size_t at = script_table + std::size_t(i) * 8;
        if (at + 8 > data.size()) break;
        AnmScriptEntry script;
        script.id = read_u32(data.data() + at);
        script.offset = read_u32(data.data() + at + 4);
        if (script.offset >= data.size()) break;

        // Walk the instruction stream to find its extent, then keep a copy of
        // the bytes so the interpreter does not have to re-slice.
        // NOTE: an ANM *instruction* header is 8 bytes, while ANM_HEADER_SIZE
        // is the 0x40-byte container header. Comparing `length` against
        // ANM_HEADER_SIZE silently rejected every instruction (all are 8..64
        // bytes), which is why script.bytes came out empty.
        constexpr u32 INSTRUCTION_HEADER = 8;
        std::size_t pos = script.offset;
        u32 guard = 0;
        while (pos + INSTRUCTION_HEADER <= data.size() && guard++ < 100000) {
            const u16 opcode = read_u16(data.data() + pos);
            const u16 length = read_u16(data.data() + pos + 2);
            if (opcode == 0xFFFF || length < INSTRUCTION_HEADER ||
                (length % 4) != 0 || pos + length > data.size()) {
                break;
            }
            AnmInstruction ins;
            ins.opcode = opcode;
            ins.length = length;
            ins.offset = u32(pos);
            script.instructions.push_back(ins);
            pos += length;
        }
        script.bytes.assign(data.begin() + script.offset, data.begin() + pos);
        script_list.push_back(std::move(script));
    }

    // THTX blocks: walk forward from thtx_offset.
    textures.clear();
    std::size_t at = thtx_offset;
    u32 guard = 0;
    while (at + 16 <= data.size() && guard++ < 4096) {
        if (std::memcmp(data.data() + at, "THTX", 4) != 0) break;
        AnmTexture texture;
        texture.offset = u32(at);
        texture.format = read_u16(data.data() + at + 6);
        texture.width = read_u16(data.data() + at + 8);
        texture.height = read_u16(data.data() + at + 10);
        texture.size = read_u32(data.data() + at + 12);
        if (at + 16 + texture.size > data.size()) break;
        textures.push_back(texture);
        at += 16 + texture.size;
    }

    return true;
}

}  // namespace th185
