#pragma once
// TH185 PBG4 archive reader.
//
// th185.dat is a "PBG4" container: a 16-byte encrypted header, per-entry
// encrypted payloads (optionally LZSS-compressed), and an entry table stored
// at the END of the file.
//
// Everything here is reverse-engineered and verified byte-identically against
// the original (see docs/PBG4.md and scripts/native/pbg4.py):
//
//   * header decryption  — RVA 0x1E90, key 0x1B step 0x37 block 0x10 limit 0x10
//   * entry decryption   — same routine, params chosen by sum(name) & 7
//   * LZSS decompression — RVA 0x71E70
//   * entry table at file END (IFile->Seek(filesize - table_size) at 0x715555)
//
// Self-check: run `python th185_web/scripts/native/pbg4.py --selftest`
// (9 checks) and `archive_cpp_check.py --selftest` for the C++ mirror.

#include "Types.hpp"

#include <string>
#include <vector>

namespace th185 {

constexpr u32 PBG4_MAGIC = 0x31414854u;  // "THA1"

// Header field deltas. The stored values are `real_value + delta`, so reading
// subtracts the delta (or adds the negated constant) with 32-bit wrap.
constexpr u32 PBG4_DECOMP_DELTA = 0x075BCD15u;   // stored - delta = decompressed size
constexpr u32 PBG4_COMP_DELTA = 0x3ADE68B1u;     // stored - delta = compressed size
constexpr u32 PBG4_COUNT_DELTA = 0xF7E7F8ACu;    // stored + 0xF7E7F8AC = entry count

constexpr u32 PBG4_HEADER_KEY = 0x1B;
constexpr u32 PBG4_HEADER_STEP = 0x37;
constexpr u32 PBG4_HEADER_BLOCK = 0x10;
constexpr u32 PBG4_HEADER_LIMIT = 0x10;

constexpr u32 LZSS_WINDOW_SIZE = 8192;
constexpr u32 LZSS_WINDOW_MASK = LZSS_WINDOW_SIZE - 1;

// Cipher parameters for one entry, selected by sum(filename_bytes) & 7.
struct CipherParams {
    u8 key;
    u8 step;
    u16 block;
    u32 limit;
};

// The 8-entry table at VA 0x4C9470, in index order.
const CipherParams* cipher_table() noexcept;

// Pick the cipher params for an entry name (sum of bytes & 7).
CipherParams cipher_for(const std::string& name) noexcept;

// In-place PBG4 decryption — mirrors RVA 0x1E90.
//
// The key stream advances by READ index, but plaintext is written with a
// PERMUTED index within each block: ODD offsets descending first, then EVEN
// offsets descending. This permutation is why a naive XOR loop produces
// garbage.
//
// The odd start offset is the largest ODD number below the block chunk:
//   chunk even -> odd_start = chunk - 1
//   chunk odd  -> odd_start = chunk - 2      (this case is easy to miss)
// Using chunk-1 unconditionally is correct for full blocks only and corrupts
// the final partial block. The entry table is exactly such a case:
//   3653 = 28 * 128 + 69, and 69 is odd,
// so the last block was silently wrong until this was fixed. That single bug
// made the archive reader lose 3 entries and mis-handle the LZSS tail.
//
// KNOWN LIMITATION: whether the original processes the final partial block at
// all is NOT fully determined. Measured behaviour:
//   entry table  stored=3653 block=128 tail=69  -> tail IS processed
//   wave07.ecl   stored=1436 block=128 tail=28  -> tail is NOT processed
//   title_v.anm  stored=2678 block=512 tail=118 -> tail is NOT processed
// Our implementation always processes the tail, which is correct for the
// entry table (the only case the archive reader depends on) but over-decrypts
// trailing bytes of some entries. Those bytes sit past the LZSS end-of-stream
// marker, so extraction results are unaffected. See docs/PBG4.md.
void decrypt_in_place(u8* data, u32 size, u32 key, u32 step, u32 block, u32 limit) noexcept;

// LZSS decompression — mirrors RVA 0x71E70.
//
// Bit stream, MSB first. flag=1 -> literal (8 bits). flag=0 -> 13-bit offset;
// offset 0 terminates; otherwise a 4-bit length field and copy (field + 3)
// bytes from an 8192-byte window whose write pointer starts at 1.
std::vector<u8> lzss_decompress(const u8* src, u32 src_size, u32 dst_size);

// One archive entry.
struct ArchiveEntry {
    std::string name;
    u32 offset = 0;         // absolute file offset of the payload
    u32 size = 0;           // DECOMPRESSED size (not the stored size)
    u32 reserved = 0;
};

// Parsed PBG4 container.
struct Archive {
    u32 decompressed_table_size = 0;
    u32 compressed_table_size = 0;
    u32 declared_entry_count = 0;
    u32 table_offset = 0;
    std::vector<ArchiveEntry> entries;

    // Parse the container in memory. Returns false and fills `error` on failure.
    bool parse(const std::vector<u8>& file, std::string* error);

    // Extract entry `index` fully: decrypt, then LZSS-decompress if needed.
    // Returns an empty vector on failure and fills `error`.
    std::vector<u8> extract(const std::vector<u8>& file, std::size_t index,
                            std::string* error) const;

    // The actual stored byte count: next entry offset minus this one, with the
    // last entry ending at the table offset. The `size` field is the
    // DECOMPRESSED size, so it cannot be used as a read length.
    u32 stored_size(std::size_t index) const;
};

}  // namespace th185
