#include "AnmResource.hpp"

#include <cstring>

namespace th185 {

namespace {

u16 read_u16(const u8* p) noexcept { return u16(p[0] | (u16(p[1]) << 8)); }

u32 read_u32(const u8* p) noexcept {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

float read_f32(const u8* p) noexcept { return bits_float(read_u32(p)); }

// An ANM *instruction* header is 8 bytes. The CONTAINER header is 0x40. Mixing
// them up silently rejects every instruction, so they stay separate constants.
constexpr u32 INSTRUCTION_HEADER = 8;

}  // namespace

bool AnmResource::parse(std::vector<u8> blob, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };

    if (blob.size() < ANM_HEADER_SIZE) return fail("anm blob too small");
    data = std::move(blob);

    // ------------------------------------------------------------------
    // A .anm file is a CHAIN of blocks. Each block has its own header,
    // sprite table and script table, and `nextoffset` gives the distance to
    // the next block (0 = last). Sprite and script INDICES are global across
    // the chain: th15's AnmResource notes that "the original loader appends
    // sprites and scripts in table order across chained chunks".
    //
    // Parsing only the first block is what produced "sprite index out of
    // range": front.anm has 14 blocks and 167 sprites in total, but block 0
    // declares only 39.
    // ------------------------------------------------------------------
    sprites.clear();
    script_list.clear();
    textures.clear();
    block_count = 0;

    std::size_t base = 0;
    while (base + ANM_HEADER_SIZE <= data.size() && block_count < 64) {
        const u8* h = data.data() + base;
        const u32 block_version = read_u32(h + 0x00);
        if (block_version != ANM_VERSION) {
            // A malformed chain end is not fatal for the blocks we already
            // parsed; stop rather than discard the whole file.
            if (block_count == 0) return fail("unexpected anm version");
            break;
        }

        const u16 block_sprites = read_u16(h + 0x04);
        const u16 block_scripts = read_u16(h + 0x06);
        const u32 name_offset = read_u32(h + 0x10);
        const u32 thtx_offset = read_u32(h + 0x1C);
        const u32 next_offset = read_u32(h + 0x24);

        if (block_count == 0) {
            // File-level fields come from the first block.
            version = block_version;
            width = read_u16(h + 0x0A);
            height = read_u16(h + 0x0C);
            format = read_u16(h + 0x0E);
            memory_priority = read_u32(h + 0x18);
            has_data = read_u16(h + 0x20);
            low_res_scale = read_u16(h + 0x22);
            name_offset_ = name_offset;
            thtx_offset_ = thtx_offset;
            next_offset_ = next_offset;

            if (name_offset < data.size()) {
                std::size_t end = name_offset;
                while (end < data.size() && data[end] != 0) ++end;
                if (end < data.size()) {
                    atlas.assign(
                        reinterpret_cast<const char*>(data.data() + name_offset),
                        end - name_offset);
                }
            }
        }

        const std::size_t block_texture_base = textures.size();
        const std::size_t block_sprite_base = sprites.size();

        // Sprite records, addressed through this block's offset table.
        for (u32 i = 0; i < block_sprites; ++i) {
            const std::size_t table_at = base + ANM_HEADER_SIZE + std::size_t(i) * 4;
            if (table_at + 4 > data.size()) break;
            // Sprite offsets are relative to THIS BLOCK's base, not the file.
            // Block 0 has base 0 so both readings coincide there, which is why
            // the bug only showed up once chained blocks were parsed.
            const std::size_t at = base + read_u32(data.data() + table_at);
            if (at + ANM_SPRITE_SIZE > data.size()) break;
            AnmSprite sprite;
            sprite.id = read_u32(data.data() + at);
            sprite.x = read_f32(data.data() + at + 4);
            sprite.y = read_f32(data.data() + at + 8);
            sprite.w = read_f32(data.data() + at + 12);
            sprite.h = read_f32(data.data() + at + 16);
            // Every sprite in a block belongs to that block's first texture
            // (thtk/thanm treats a block as one atlas).
            sprite.texture = u32(block_texture_base);
            sprites.push_back(sprite);
        }

        // Script table, right after the sprite offset array.
        const std::size_t script_table =
            base + ANM_HEADER_SIZE + std::size_t(block_sprites) * 4;

        // A script has NO explicit terminator-based extent: thtk's thanm.c
        // computes its end from the NEXT script's offset --
        //
        //     limit += script_offsets[s + 1].offset;
        //
        // Stopping at the first malformed length instead truncates the slice,
        // which is what produced the "invalid jump" and "invalid instruction
        // length" faults: jump targets pointed past the truncated buffer.
        for (u32 i = 0; i < block_scripts; ++i) {
            const std::size_t at = script_table + std::size_t(i) * 8;
            if (at + 8 > data.size()) break;
            AnmScriptEntry script;
            script.id = read_u32(data.data() + at);
            // Relative to this block's base (see the sprite note above).
            script.offset = u32(base + read_u32(data.data() + at + 4));
            if (script.offset >= data.size()) break;

            // Upper bound: the next script's offset, or the end of this chunk's
            // script region for the last one.
            std::size_t limit = data.size();
            if (i + 1 < block_scripts) {
                const std::size_t next_at = at + 8;
                if (next_at + 8 <= data.size()) {
                    const std::size_t next_offset =
                        base + read_u32(data.data() + next_at + 4);
                    if (next_offset > script.offset && next_offset <= data.size()) {
                        limit = next_offset;
                    }
                }
            }

            std::size_t pos = script.offset;
            u32 guard = 0;
            while (pos + INSTRUCTION_HEADER <= limit && guard++ < 100000) {
                const u16 opcode = read_u16(data.data() + pos);
                const u16 length = read_u16(data.data() + pos + 2);
                if (opcode == 0xFFFF) break;
                if (length < INSTRUCTION_HEADER || (length % 4) != 0 ||
                    pos + length > limit) {
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

        // THTX blocks in this chunk.
        std::size_t at = base + thtx_offset;
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

        // Derive UVs for the sprites just added, now that this block's texture
        // sizes are known. th15 does the same in AnmResource.cpp:
        //     sprite.u0 = sprite.x / texture.width
        //     sprite.u1 = (sprite.x + sprite.width) / texture.width
        for (std::size_t s = block_sprite_base; s < sprites.size(); ++s) {
            AnmSprite& sprite = sprites[s];
            if (sprite.texture >= textures.size()) continue;
            const AnmTexture& texture = textures[sprite.texture];
            if (texture.width == 0 || texture.height == 0) continue;
            const float tw = float(texture.width);
            const float th = float(texture.height);
            sprite.u0 = float(double(sprite.x) / double(tw));
            sprite.v0 = float(double(sprite.y) / double(th));
            sprite.u1 = float((double(sprite.x) + double(sprite.w)) / double(tw));
            sprite.v1 = float((double(sprite.y) + double(sprite.h)) / double(th));
        }

        ++block_count;
        if (next_offset == 0) break;
        base += next_offset;
    }

    if (block_count == 0) return fail("no anm block parsed");
    sprite_count = u16(sprites.size() > 0xFFFF ? 0xFFFF : sprites.size());
    script_count = u16(script_list.size() > 0xFFFF ? 0xFFFF : script_list.size());
    return true;
}

}  // namespace th185
