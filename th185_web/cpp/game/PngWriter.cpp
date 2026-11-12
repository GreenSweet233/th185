#include "PngWriter.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace th185 {

namespace {

u32 crc32(const u8* data, std::size_t size, u32 crc = 0xFFFFFFFFu) {
    static u32 table[256];
    static bool ready = false;
    if (!ready) {
        for (u32 i = 0; i < 256; ++i) {
            u32 value = i;
            for (u32 bit = 0; bit < 8; ++bit) {
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            }
            table[i] = value;
        }
        ready = true;
    }
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

void put_u32(std::vector<u8>& out, u32 value) {
    out.push_back(u8(value >> 24));
    out.push_back(u8(value >> 16));
    out.push_back(u8(value >> 8));
    out.push_back(u8(value));
}

void chunk(std::vector<u8>& out, const char tag[4], const std::vector<u8>& data) {
    put_u32(out, u32(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), data.begin(), data.end());
    const u32 crc = crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
    put_u32(out, crc);
}

}  // namespace

bool encode_png(u32 width, u32 height, const std::vector<u8>& rgba,
                std::vector<u8>& out) {
    if (width == 0 || height == 0) return false;
    if (rgba.size() != std::size_t(width) * height * 4) return false;

    // Raw scanlines, each prefixed with filter type 0.
    std::vector<u8> raw;
    raw.reserve(std::size_t(height) * (1 + std::size_t(width) * 4));
    for (u32 y = 0; y < height; ++y) {
        raw.push_back(0);
        const std::size_t row = std::size_t(y) * width * 4;
        raw.insert(raw.end(), rgba.begin() + row, rgba.begin() + row + std::size_t(width) * 4);
    }

    // zlib stream: header + stored deflate blocks + adler32.
    std::vector<u8> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);

    constexpr std::size_t BLOCK = 65535;
    for (std::size_t at = 0; at < raw.size(); at += BLOCK) {
        const std::size_t count = (raw.size() - at < BLOCK) ? raw.size() - at : BLOCK;
        const bool last = (at + count >= raw.size());
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(u8(count & 0xFF));
        zlib.push_back(u8((count >> 8) & 0xFF));
        zlib.push_back(u8(~count & 0xFF));
        zlib.push_back(u8((~count >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + at, raw.begin() + at + count);
    }

    u32 a = 1, b = 0;
    for (u8 byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    put_u32(zlib, (b << 16) | a);

    std::vector<u8> png{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<u8> ihdr;
    put_u32(ihdr, width);
    put_u32(ihdr, height);
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(6);   // colour type: RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", zlib);
    chunk(png, "IEND", {});

    out = std::move(png);
    return true;
}

bool write_png(const char* path, u32 width, u32 height,
               const std::vector<u8>& rgba) {
    std::vector<u8> png;
    if (!encode_png(width, height, rgba, png)) return false;
    FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const std::size_t written = std::fwrite(png.data(), 1, png.size(), file);
    std::fclose(file);
    return written == png.size();
}

}  // namespace th185
