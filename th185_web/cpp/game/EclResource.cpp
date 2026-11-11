#include "EclResource.hpp"

#include <cstring>

namespace th185 {

namespace {

u16 read_u16(const u8* p) noexcept { return u16(p[0] | (u16(p[1]) << 8)); }

u32 read_u32(const u8* p) noexcept {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

// Find a 4-byte tag inside the buffer. Returns SIZE_MAX when absent.
std::size_t find_tag(const std::vector<u8>& buf, u32 tag) noexcept {
    if (buf.size() < 4) return std::size_t(-1);
    const u8 b0 = u8(tag & 0xFF);
    const u8 b1 = u8((tag >> 8) & 0xFF);
    const u8 b2 = u8((tag >> 16) & 0xFF);
    const u8 b3 = u8((tag >> 24) & 0xFF);
    for (std::size_t i = 0; i + 4 <= buf.size(); ++i) {
        if (buf[i] == b0 && buf[i + 1] == b1 && buf[i + 2] == b2 && buf[i + 3] == b3) {
            return i;
        }
    }
    return std::size_t(-1);
}

// Read a NUL-terminated ASCII string starting at `pos`, advancing `pos` past
// the terminator. Returns false when the buffer ends without a NUL.
bool read_cstring(const std::vector<u8>& buf, std::size_t& pos, std::string& out) {
    const std::size_t start = pos;
    while (pos < buf.size() && buf[pos] != 0) ++pos;
    if (pos >= buf.size()) return false;
    out.assign(reinterpret_cast<const char*>(buf.data() + start), pos - start);
    ++pos;  // consume the NUL
    return true;
}

}  // namespace

bool EclResource::parse(std::vector<u8> blob, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };

    if (blob.size() < 0x38) return fail("SCPT file too small");
    if (read_u32(blob.data()) != ECL_MAGIC_SCPT) {
        return fail("not an SCPT container (magic mismatch)");
    }

    data = std::move(blob);

    version = read_u32(data.data() + 0x04);
    name_count = read_u32(data.data() + 0x08);
    sub_count = read_u32(data.data() + 0x10);

    anim_names.clear();
    names.clear();
    subs.clear();

    // --- ANIM section (variable length) ---
    // Layout: "ANIM" u32 count, then `count` NUL-terminated names.
    const std::size_t anim_pos = find_tag(data, ECL_MAGIC_ANIM);
    if (anim_pos != std::size_t(-1) && anim_pos + 8 <= data.size()) {
        const u32 anim_count = read_u32(data.data() + anim_pos + 4);
        std::size_t pos = anim_pos + 8;
        for (u32 i = 0; i < anim_count; ++i) {
            std::string name;
            if (!read_cstring(data, pos, name)) break;
            anim_names.push_back(std::move(name));
        }
    }

    // --- ECLI section: tag + u32 count + count names, then the offset table ---
    //
    // Like ANIM, ECLI is VARIABLE LENGTH: it lists the .ecl files this script
    // depends on (e.g. boss01.ecl -> ["default.ecl"]). Observed counts: 0 (77
    // files), 1 (28), and 5/15/16/17/25/35/58 (one or two files each). Assuming
    // the table sits at a fixed offset works only for the 77 count==0 files.
    const std::size_t ecli_pos = find_tag(data, ECL_MAGIC_ECLI);
    if (ecli_pos == std::size_t(-1)) return fail("no ECLI section");
    ecli_offset = u32(ecli_pos);
    if (ecli_pos + 8 > data.size()) return fail("truncated ECLI section");

    ecli_names.clear();
    const u32 ecli_count = read_u32(data.data() + ecli_pos + 4);
    std::size_t ecli_name_pos = ecli_pos + 8;
    for (u32 i = 0; i < ecli_count; ++i) {
        std::string name;
        if (!read_cstring(data, ecli_name_pos, name)) break;
        ecli_names.push_back(std::move(name));
    }
    const std::size_t table_pos = (ecli_name_pos + 3) & ~std::size_t(3);

    std::vector<u32> sub_offsets;
    sub_offsets.reserve(sub_count);
    for (u32 i = 0; i < sub_count; ++i) {
        const std::size_t pos = table_pos + std::size_t(i) * 4;
        if (pos + 4 > data.size()) break;
        const u32 offset = read_u32(data.data() + pos);
        if (offset + 4 > data.size()) break;
        if (read_u32(data.data() + offset) != ECL_MAGIC_ECLH) break;
        sub_offsets.push_back(offset);
    }
    if (sub_offsets.empty()) return fail("no ECLH blocks found");

    // --- Name table starts right after the offset table ---
    std::size_t name_pos = table_pos + std::size_t(sub_count) * 4;
    while (name_pos < data.size() && names.size() < name_count) {
        std::string name;
        if (!read_cstring(data, name_pos, name)) break;
        if (!name.empty()) names.push_back(std::move(name));
    }

    // --- Parse each ECLH block ---
    for (std::size_t i = 0; i < sub_offsets.size(); ++i) {
        const u32 offset = sub_offsets[i];
        const u32 end = (i + 1 < sub_offsets.size()) ? sub_offsets[i + 1]
                                                      : u32(data.size());
        if (offset + ECLH_INSTRUCTION_START > end) break;

        EclSubroutine sub;
        sub.index = u32(i);
        sub.offset = offset;
        if (i < names.size()) sub.name = names[i];

        u32 pos = offset + ECLH_INSTRUCTION_START;
        while (pos + 4 <= end) {
            const u16 id = read_u16(data.data() + pos);
            const u16 size = read_u16(data.data() + pos + 2);
            if (size < 4 || pos + size > end) break;

            EclInstruction ins;
            ins.id = id;
            ins.size = size;
            ins.offset = pos;
            if (size >= 8) {
                ins.rank = read_u16(data.data() + pos + 4);
                ins.argc = read_u16(data.data() + pos + 6);
                ins.payload = data.data() + pos + 8;
                ins.payload_size = u32(size) - 8;
            } else {
                ins.payload = data.data() + pos + size;
                ins.payload_size = 0;
            }
            sub.instructions.push_back(ins);
            pos += size;
        }

        sub.end = pos;
        sub.tail = end - pos;
        subs.push_back(std::move(sub));
    }

    if (subs.empty()) return fail("no subroutines parsed");
    return true;
}

const EclSubroutine* EclResource::find(const std::string& name) const {
    for (const EclSubroutine& sub : subs) {
        if (sub.name == name) return &sub;
    }
    return nullptr;
}

std::size_t EclResource::instruction_count() const {
    std::size_t total = 0;
    for (const EclSubroutine& sub : subs) total += sub.instructions.size();
    return total;
}

}  // namespace th185
