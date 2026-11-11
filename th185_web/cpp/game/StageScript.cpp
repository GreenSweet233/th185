#include "StageScript.hpp"

#include <cstring>

namespace th185 {

namespace {

u16 read_u16(const u8* p) noexcept { return u16(p[0] | (u16(p[1]) << 8)); }

u32 read_u32(const u8* p) noexcept {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

float read_f32(const u8* p) noexcept {
    const u32 bits = read_u32(p);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

}  // namespace

bool StageScript::parse(std::vector<u8> blob, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };

    if (blob.size() < 0x94) return fail("std file too small");
    // Sanity: the header must describe offsets inside the file. Without this a
    // truncated or over-decrypted blob walks off the buffer instead of failing
    // cleanly, which surfaces as a WASM "memory access out of bounds" trap.
    {
        const u32 e = read_u32(blob.data() + 0x04);
        const u32 c = read_u32(blob.data() + 0x08);
        if (e > blob.size() || c > blob.size()) {
            return fail("header offsets lie outside the file");
        }
    }

    data = std::move(blob);

    wave_count = read_u16(data.data() + 0x00);
    group_count = read_u16(data.data() + 0x02);
    entity_offset = read_u32(data.data() + 0x04);
    command_offset = read_u32(data.data() + 0x08);

    // ANM name: NUL-terminated at +0x10.
    {
        const std::size_t start = 0x10;
        std::size_t end = start;
        while (end < data.size() && data[end] != 0) ++end;
        if (end >= data.size()) return fail("ANM name is not terminated");
        anm_name.assign(reinterpret_cast<const char*>(data.data() + start), end - start);
    }

    if (entity_offset >= data.size()) return fail("entity offset out of range");

    // --- Wave offset table at a FIXED +0x90 ---
    // Unlike the ECL container, this offset does not shift with the ANM name
    // length (verified on all 9 files).
    waves.clear();
    for (u16 i = 0; i < wave_count; ++i) {
        const std::size_t pos = STD_WAVE_TABLE_OFFSET + std::size_t(i) * 4;
        if (pos + 4 > data.size()) break;
        const u32 offset = read_u32(data.data() + pos);
        if (offset + STD_WAVE_ENTRY_SIZE > data.size()) break;

        const u8* base = data.data() + offset;
        StageWave wave;
        wave.offset = offset;
        wave.marker = read_u32(base + 0x00);
        wave.f1 = read_f32(base + 0x10);
        wave.f2 = read_f32(base + 0x14);
        wave.f3 = read_f32(base + 0x18);
        wave.flags = read_u32(base + 0x1C);
        wave.slot_id = read_u32(base + 0x20);
        wave.marker2 = read_u32(base + 0x38);
        waves.push_back(wave);
    }
    if (waves.empty()) return fail("no wave entries parsed");

    if (waves.empty()) return fail("no wave entries parsed");

    // --- 28-byte entity-group table ---
    // This sits between the wave entries and the entity table and has a
    // VARIABLE number of entries. The last wave entry does NOT necessarily end
    // where the entity table begins (world04 has 15 extra groups = 420 bytes).
    const u32 groups_start = waves.back().offset + STD_WAVE_ENTRY_SIZE;
    groups.clear();
    // Guard both ends: a malformed file must not walk past the buffer.
    if (groups_start < entity_offset && entity_offset <= data.size()) {
        const u32 span = entity_offset - groups_start;
        if (span % STD_GROUP_ENTRY_SIZE != 0) {
            return fail("entity group span is not a multiple of 28");
        }
        const u32 count = span / STD_GROUP_ENTRY_SIZE;
        for (u32 i = 0; i < count; ++i) {
            const std::size_t at = std::size_t(groups_start) + std::size_t(i) * STD_GROUP_ENTRY_SIZE;
            if (at + STD_GROUP_ENTRY_SIZE > data.size()) break;
            const u8* base = data.data() + at;
            StageGroup group;
            group.type = read_u32(base + 0x00);
            group.x = read_f32(base + 0x04);
            group.y = read_f32(base + 0x08);
            group.reserved = read_u32(base + 0x0C);
            group.size_a = read_f32(base + 0x10);
            group.size_b = read_f32(base + 0x14);
            group.marker = read_u32(base + 0x18);
            groups.push_back(group);
        }
    }

    // --- Entity table (16 bytes each) ---
    // Entries are 0x01xxxxxx; the walk stops at the first non-matching word.
    entities.clear();
    {
        std::size_t pos = entity_offset;
        if (pos >= data.size()) pos = data.size();  // nothing to read
        while (pos + STD_ENTITY_ENTRY_SIZE <= data.size()) {
            const u32 kind = read_u32(data.data() + pos);
            if ((kind >> 24) != 0x01) break;
            StageEntity entity;
            entity.kind = kind;
            entity.x = read_f32(data.data() + pos + 4);
            entity.y = read_f32(data.data() + pos + 8);
            entity.z = read_f32(data.data() + pos + 12);
            entities.push_back(entity);
            pos += STD_ENTITY_ENTRY_SIZE;
        }
    }

    // --- Command stream ---
    // Starts 4 bytes into command_offset, then [u16 id][u16 size] records.
    commands.clear();
    command_end = command_offset + STD_COMMAND_HEADER;
    if (command_end > data.size()) command_end = u32(data.size());
    {
        std::size_t pos = command_end;
        while (pos + STD_COMMAND_HEADER <= data.size()) {
            const u16 id = read_u16(data.data() + pos);
            const u16 size = read_u16(data.data() + pos + 2);
            if (id == 0 && size == 0) break;
            if (size < 4 || pos + size > data.size()) break;

            StageCommand command;
            command.id = id;
            command.size = size;
            command.offset = u32(pos);
            command.payload = data.data() + pos + 4;
            command.payload_size = u32(size) - 4;
            commands.push_back(command);
            pos += size;
        }
        command_end = u32(pos);
    }

    if (commands.empty()) return fail("no commands parsed");
    return true;
}

u32 StageScript::tail_size() const {
    if (command_end >= data.size()) return 0;
    return u32(data.size()) - command_end;
}

}  // namespace th185
