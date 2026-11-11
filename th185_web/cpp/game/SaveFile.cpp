#include "SaveFile.hpp"

#include "Archive.hpp"

#include <cstring>

namespace th185 {

namespace {

u16 read_u16(const u8* p) noexcept { return u16(p[0] | (u16(p[1]) << 8)); }

u32 read_u32(const u8* p) noexcept {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

}  // namespace

bool SaveFile::parse(std::vector<u8> blob, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };

    if (blob.size() < SAVE_HEADER_SIZE) return fail("save file too small");

    header.magic = read_u32(blob.data() + 0x00);
    header.file_size = read_u32(blob.data() + 0x04);
    header.version = read_u16(blob.data() + 0x08);
    header.reserved = read_u16(blob.data() + 0x0A);
    header.constant = read_u32(blob.data() + 0x0C);
    header.compressed_size = read_u32(blob.data() + 0x10);
    header.decompressed_size = read_u32(blob.data() + 0x14);

    if (header.magic != SAVE_MAGIC) return fail("bad save magic (expected 581T)");
    if (header.version != SAVE_VERSION) return fail("unsupported save version");
    if (SAVE_HEADER_SIZE + header.compressed_size > blob.size()) {
        return fail("compressed size exceeds the file");
    }

    // Decrypt, then decompress. Same primitives as the archive, different params.
    std::vector<u8> cipher(blob.begin() + SAVE_HEADER_SIZE,
                           blob.begin() + SAVE_HEADER_SIZE + header.compressed_size);
    decrypt_in_place(cipher.data(), u32(cipher.size()), SAVE_KEY, SAVE_STEP,
                     SAVE_BLOCK, header.compressed_size);

    raw = lzss_decompress(cipher.data(), u32(cipher.size()),
                          header.decompressed_size);
    if (raw.size() != header.decompressed_size) {
        return fail("save payload did not decompress to the declared size");
    }

    // Walk the self-describing record sequence.
    records.clear();
    std::size_t pos = 0;
    while (pos + 16 <= raw.size()) {
        const u16 magic = read_u16(raw.data() + pos);
        const u16 version = read_u16(raw.data() + pos + 2);
        const u32 value = read_u32(raw.data() + pos + 4);
        const u32 length = read_u32(raw.data() + pos + 8);

        if (magic != SAVE_RECORD_CR && magic != SAVE_RECORD_ST) break;
        if (length < 16 || pos + length > raw.size()) break;

        SaveRecord record;
        record.magic = magic;
        record.version = version;
        record.value = value;
        record.length = length;
        record.offset = pos;

        if (magic == SAVE_RECORD_CR) {
            // 32-byte slots in groups of 10, from +0x10.
            std::size_t slot_pos = pos + SAVE_CR_HEADER;
            const std::size_t limit = pos + length;
            while (slot_pos + SAVE_SLOT_SIZE <= limit) {
                const u32 score = read_u32(raw.data() + slot_pos);
                // +0x04 is NOT a single u16 flag: the observed bytes are
                // "01 00" in one record and "01 01" in another, so it is two
                // independent u8 fields. Reading it as u16 and rejecting
                // values > 1 made every record after the first parse as empty.
                const u8 field_a = raw[slot_pos + 4];
                const u8 field_b = raw[slot_pos + 5];
                if (score > 10000000u) break;

                // Name must be 8 printable characters; otherwise this is not a
                // slot and the table has ended. Compute the flag up front rather
                // than mutating slot_pos inside the scan loop — doing that made
                // every record after the first parse as zero slots.
                const u8* name_bytes = raw.data() + slot_pos + 6;
                bool printable = true;
                for (u32 i = 0; i < 8; ++i) {
                    const u8 ch = name_bytes[i];
                    if (ch < 32 || ch > 126) { printable = false; break; }
                }
                if (!printable) break;

                SaveSlot slot;
                slot.score = score;
                slot.flag_a = field_a;
                slot.flag_b = field_b;
                slot.name.assign(reinterpret_cast<const char*>(name_bytes), 8);
                record.slots.push_back(std::move(slot));
                slot_pos += SAVE_SLOT_SIZE;
            }
        }

        records.push_back(std::move(record));
        pos += length;
    }

    consumed = u32(pos);
    if (records.empty()) return fail("no save records parsed");
    return true;
}

bool SaveFile::is_self_consistent() const {
    return consumed == raw.size();
}

u32 SaveFile::cr_count() const {
    u32 count = 0;
    for (const SaveRecord& record : records) {
        if (record.magic == SAVE_RECORD_CR) ++count;
    }
    return count;
}

u32 SaveFile::st_count() const {
    u32 count = 0;
    for (const SaveRecord& record : records) {
        if (record.magic == SAVE_RECORD_ST) ++count;
    }
    return count;
}

}  // namespace th185
