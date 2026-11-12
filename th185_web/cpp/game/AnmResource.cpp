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

    // sprite_t records.
    sprites.clear();
    sprites.reserve(sprite_count);
    for (u32 i = 0; i < sprite_count; ++i) {
        const std::size_t at = std::size_t(sprite_offset) + std::size_t(i) * ANM_SPRITE_SIZE;
        if (at + ANM_SPRITE_SIZE > data.size()) break;
        AnmSprite sprite;
        sprite.id = read_u32(data.data() + at);
        sprite.x = read_f32(data.data() + at + 4);
        sprite.y = read_f32(data.data() + at + 8);
        sprite.w = read_f32(data.data() + at + 12);
        sprite.h = read_f32(data.data() + at + 16);
        sprites.push_back(sprite);
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
