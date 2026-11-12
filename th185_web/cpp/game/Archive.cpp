#include "Archive.hpp"

#include <cstdio>

#include <cstring>

namespace th185 {

namespace {

// VA 0x4C9470: 8 entries x 12 bytes {key, step, pad, block, limit}.
// Only key/step/block/limit are used here.
const CipherParams kCipherTable[8] = {
    {0x1B, 0x73, 0x100, 0x3800},
    {0x12, 0x43, 0x200, 0x3E00},
    {0x35, 0x79, 0x400, 0x3C00},
    {0x03, 0x91, 0x080, 0x6400},
    {0xAB, 0xDC, 0x080, 0x7000},
    {0x51, 0x9E, 0x100, 0x4000},
    {0xC1, 0x15, 0x400, 0x2C00},
    {0x99, 0x7D, 0x080, 0x4400},
};

u32 read_u32(const u8* p) noexcept {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

}  // namespace

const CipherParams* cipher_table() noexcept { return kCipherTable; }

CipherParams cipher_for(const std::string& name) noexcept {
    // The original sums bytes until NUL and ANDs with 7 (RVA 0x713C5-0x713F5).
    u32 total = 0;
    for (char c : name) total += u8(c);
    return kCipherTable[(total & 0xFFu) & 7u];
}

void decrypt_in_place(u8* data, u32 size, u32 key, u32 step, u32 block,
                      u32 limit) noexcept {
    if (size == 0) return;

    // The original XORs into a caller buffer while reading from the same one,
    // so keep a copy of the cipher text to read from.
    std::vector<u8> cipher(data, data + size);

    u32 src = 0;
    u32 dst_base = 0;
    u32 remain = size;
    u32 avail = limit;
    u8 cur_key = u8(key);
    const u8 cur_step = u8(step);

    // Measured with a Unicorn write hook on the original function: a partial
    // tail block is only processed when it is at least block/4 bytes long.
    // Below that the original leaves those bytes as CIPHERTEXT.
    //
    // The evidence is a sweep over stored sizes at block = 128/256/512/1024:
    //   block=1024: rem 100/200/255 -> 1 block written, rem 256 -> 2 blocks
    //   block= 512: rem 100 -> 1 block, rem 200 -> 2 blocks
    //   block= 256: rem 100 -> 2 blocks
    //   block= 128: rem 100 -> 2 blocks
    // i.e. the threshold is exactly block/4 in every case (20/20 samples).
    //
    // world04.ecl is why this matters: stored 1198 with block 1024 leaves a
    // 174-byte tail, 174 < 256, so the original never decrypts it. Decrypting
    // it anyway produced 4111 bytes of LZSS output instead of 4612 and the
    // whole scene failed to load.
    const u32 tail_floor = block / 4u;
    while (remain > 0 && avail > 0 && dst_base < size) {
        u32 chunk = remain;
        if (chunk > block) chunk = block;
        if (chunk > size - dst_base) chunk = size - dst_base;
        if (chunk == 0) break;
        if (chunk < block && chunk < tail_floor) break;

        // Write order, measured with a Unicorn memory-write hook on the
        // original function (RVA 0x1E90). For a 69-byte tail the order is
        //   67, 65, ..., 1   then   66, 64, ..., 0
        // i.e. ODD offsets descending first, then EVEN offsets descending.
        //
        // The odd start is the largest odd number BELOW chunk: chunk-1 when
        // chunk is even, chunk-2 when chunk is odd. Using chunk-1
        // unconditionally is only correct for full blocks and silently
        // corrupts the last partial block — that bug left exactly the final
        // 69 bytes of the entry table wrong, which in turn lost 3 entries and
        // broke the LZSS tail.
        const u32 odd_start = (chunk % 2 == 0) ? chunk - 1 : chunk - 2;
        const u32 odd_count = (chunk + 1) / 2;
        const u32 even_start = odd_start - 1;
        const u32 even_count = chunk / 2;

        // IMPORTANT: w must be SIGNED. With an unsigned index, the final
        // w -= 2 wraps to 0xFFFFFFFE instead of going negative, the guard
        // "w < dst_base" never fires, and the next store writes far outside
        // the buffer — in WASM that is a "memory access out of bounds" trap.
        i32 w = i32(dst_base + odd_start);
        for (u32 i = 0; i < odd_count; ++i) {
            if (src >= size || w < i32(dst_base)) break;
            data[u32(w)] = cipher[src] ^ cur_key;
            cur_key = u8(cur_key + cur_step);
            ++src;
            w -= 2;
        }

        w = i32(dst_base + even_start);
        for (u32 i = 0; i < even_count; ++i) {
            if (src >= size || w < i32(dst_base)) break;
            data[u32(w)] = cipher[src] ^ cur_key;
            cur_key = u8(cur_key + cur_step);
            ++src;
            w -= 2;
        }

        dst_base += chunk;
        remain -= chunk;
        avail -= chunk;
    }
}

std::vector<u8> lzss_decompress(const u8* src, u32 src_size, u32 dst_size) {
    std::vector<u8> out;
    out.reserve(dst_size);
    if (src_size == 0 || dst_size == 0) return out;

    std::vector<u8> window(LZSS_WINDOW_SIZE, 0);
    u32 dst_pos = 1;  // the window write pointer starts at 1, not 0

    // Bit reader mirroring the original: a byte is loaded when the mask runs
    // out, bits are consumed MSB first, and reading past the end yields zero
    // bits (the original does `xor ecx,ecx` when the index exceeds the length).
    //
    // `exhausted()` is deliberately NOT "bit counter >= total bits": it is only
    // true once the whole last byte has been consumed (pos past the end AND
    // mask drained). Using a bit counter instead changes how the tail of a
    // stream is handled and produced 117 extra bytes on the real entry table.
    u32 pos = 0;   // next byte to load
    u32 mask = 0;  // 0x80 down to 0x01, then reload
    u8 cur = 0;

    auto exhausted = [&]() { return pos >= src_size && mask == 0; };
    auto bit = [&]() -> u32 {
        if (mask == 0) {
            if (pos >= src_size) {
                cur = 0;
            } else {
                cur = src[pos];
                ++pos;
            }
            mask = 0x80;
        }
        const u32 result = (cur & mask) ? 1u : 0u;
        mask >>= 1;
        return result;
    };
    auto bits = [&](u32 count) -> u32 {
        u32 value = 0;
        for (u32 i = 0; i < count; ++i) value = (value << 1) | bit();
        return value;
    };

    while (out.size() < dst_size) {
        if (exhausted()) break;

        if (bit() == 1) {
            // Literal: 8 bits.
            const u8 literal = u8(bits(8));
            out.push_back(literal);
            window[dst_pos & LZSS_WINDOW_MASK] = literal;
            dst_pos = (dst_pos + 1) & LZSS_WINDOW_MASK;
        } else {
            // Match: 13-bit offset, then a 4-bit length field (+3).
            const u32 offset = bits(13);
            if (offset == 0) break;  // terminator
            const u32 length = bits(4) + 3;
            for (u32 i = 0; i < length; ++i) {
                if (out.size() >= dst_size) break;
                const u8 byte = window[(offset + i) & LZSS_WINDOW_MASK];
                out.push_back(byte);
                window[dst_pos & LZSS_WINDOW_MASK] = byte;
                dst_pos = (dst_pos + 1) & LZSS_WINDOW_MASK;
            }
        }
    }
    return out;
}

bool Archive::parse(const std::vector<u8>& file, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };

    if (file.size() < 16) return fail("file too small for a PBG4 header");

    // Decrypt the 16-byte header.
    u8 header[16];
    std::memcpy(header, file.data(), 16);
    decrypt_in_place(header, 16, PBG4_HEADER_KEY, PBG4_HEADER_STEP,
                     PBG4_HEADER_BLOCK, PBG4_HEADER_LIMIT);

    const u32 magic = read_u32(header + 0);
    if (magic != PBG4_MAGIC) return fail("not a PBG4 container (magic mismatch)");

    decompressed_table_size = read_u32(header + 4) - PBG4_DECOMP_DELTA;
    compressed_table_size = read_u32(header + 8) - PBG4_COMP_DELTA;
    declared_entry_count = read_u32(header + 12) + PBG4_COUNT_DELTA;

    if (compressed_table_size == 0 || compressed_table_size > file.size()) {
        return fail("invalid compressed table size");
    }

    // The entry table lives at the END of the file:
    //   IFile->Seek(filesize - table_size) at RVA 0x715555.
    table_offset = u32(file.size()) - compressed_table_size;

    // Decrypt the table. Its cipher params use the table size as the limit.
    std::vector<u8> table(file.begin() + table_offset, file.end());
    decrypt_in_place(table.data(), u32(table.size()), 0x3E, 0x9B, 0x80,
                     compressed_table_size);

    // The table is LZSS-compressed.
    //
    // NOTE: decompressed_table_size (7172 for th185.dat) is NOT the size to stop
    // at. The original passes a large malloc size and the LZSS stream terminates
    // on its own offset==0 marker, so stopping at the declared size truncates
    // the table and loses entries. The Python reference uses 0x100000 here for
    // exactly that reason; we do the same and then trim trailing NULs.
    std::vector<u8> plain = lzss_decompress(table.data(), u32(table.size()),
                                            0x100000);
#ifdef TH185_ARCHIVE_DEBUG
    {
        std::printf("DEBUG table_cipher_first16 ");
        for (u32 i = 0; i < 16 && i < u32(table.size()); ++i)
            std::printf("%02X", unsigned(table[i]));
        std::printf("\n");
        std::printf("DEBUG lzss in=%u out=%zu\n", u32(table.size()), plain.size());
    }
#endif
    if (plain.empty()) return fail("entry table did not decompress");
    while (!plain.empty() && plain.back() == 0) plain.pop_back();

    // Walk the entry list: NUL-terminated name, padded to a 4-byte multiple of
    // (len + 1), then u32 offset, u32 size (decompressed), u32 reserved.
    entries.clear();
    std::size_t pos = 0;
#ifdef TH185_ARCHIVE_DEBUG
    std::printf("DEBUG table_plain %zu\n", plain.size());
#endif
    while (pos < plain.size()) {
        const std::size_t name_start = pos;
        while (pos < plain.size() && plain[pos] != 0) ++pos;
        if (pos >= plain.size()) break;
        if (pos == name_start) break;  // empty name terminates the list
        ArchiveEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(plain.data() + name_start),
                          pos - name_start);

        // Skip the NUL and pad to a 4-byte multiple of (len + 1).
        const std::size_t name_len = entry.name.size() + 1;
        const std::size_t padded = (name_len + 3) & ~std::size_t(3);
        pos = name_start + padded;
        if (pos + 12 > plain.size()) break;

        entry.offset = read_u32(plain.data() + pos);
        entry.size = read_u32(plain.data() + pos + 4);
        entry.reserved = read_u32(plain.data() + pos + 8);
        pos += 12;

        if (entry.offset >= file.size()) {
#ifdef TH185_ARCHIVE_DEBUG
            std::printf("DEBUG break at %zu name=%s offset=%u\n",
                        entries.size(), entry.name.c_str(), entry.offset);
#endif
            break;
        }
        entries.push_back(std::move(entry));
    }

#ifdef TH185_ARCHIVE_DEBUG
    std::printf("DEBUG parsed %zu entries, pos=%zu of %zu\n",
                entries.size(), pos, plain.size());
#endif
    if (entries.empty()) return fail("no entries parsed");
    return true;
}

u32 Archive::stored_size(std::size_t index) const {
    if (index >= entries.size()) return 0;
    const u32 next = (index + 1 < entries.size()) ? entries[index + 1].offset
                                                  : table_offset;
    if (next <= entries[index].offset) return 0;
    return next - entries[index].offset;
}

std::vector<u8> Archive::extract(const std::vector<u8>& file, std::size_t index,
                                 std::string* error) const {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return std::vector<u8>();
    };
    if (index >= entries.size()) return fail("entry index out of range");

    const ArchiveEntry& entry = entries[index];
    const u32 stored = stored_size(index);
    if (stored == 0) return fail("invalid stored size");
    if (entry.offset + stored > file.size()) return fail("entry runs past EOF");

    std::vector<u8> data(file.begin() + entry.offset,
                         file.begin() + entry.offset + stored);

    // Decrypt with this entry is cipher params.
    const CipherParams params = cipher_for(entry.name);

    decrypt_in_place(data.data(), u32(data.size()), params.key, params.step,
                     params.block, params.limit);

    // `size` is the DECOMPRESSED size. If the stored size matches it, the data
    // is stored raw (only decryption was needed). Verified on help_01.png and
    // se_plst00.wav, which come out as valid PNG / RIFF.
    if (stored != entry.size) {
        data = lzss_decompress(data.data(), u32(data.size()), entry.size);
    }

    if (data.size() != entry.size) {
        return fail("extracted size does not match the declared size");
    }
    return data;
}

}  // namespace th185
