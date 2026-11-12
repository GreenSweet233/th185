#include "AnmTexture.hpp"

namespace th185 {

PixelFormat pixel_format_from_code(u32 code) {
    switch (code) {
    case 1: return PixelFormat::Bgra8888;
    case 3: return PixelFormat::Rgb565;
    case 5: return PixelFormat::Argb4444;
    case 7: return PixelFormat::Gray8;
    default: return PixelFormat::Unknown;
    }
}

u32 pixel_bytes(PixelFormat format) {
    switch (format) {
    case PixelFormat::Bgra8888: return 4;
    case PixelFormat::Rgb565: return 2;
    case PixelFormat::Argb4444: return 2;
    case PixelFormat::Gray8: return 1;
    default: return 0;
    }
}

bool pixel_size_matches(PixelFormat format, u32 width, u32 height, u32 size) {
    const u32 bpp = pixel_bytes(format);
    if (bpp == 0 || width == 0 || height == 0) return false;
    // Use 64-bit so a corrupt header cannot overflow into a false positive.
    const u64 expected = u64(width) * u64(height) * u64(bpp);
    return expected == u64(size);
}

namespace {

// 5-6-5 -> 8 bit, replicating the high bits into the low ones so full-scale
// input maps to 255 rather than 248.
inline u8 expand5(u32 value) { return u8((value << 3) | (value >> 2)); }
inline u8 expand6(u32 value) { return u8((value << 2) | (value >> 4)); }
inline u8 expand4(u32 value) { return u8((value << 4) | value); }

}  // namespace

DecodedImage decode_texture(u32 format_code, u32 width, u32 height,
                            const u8* pixels, u32 size) {
    DecodedImage image;
    image.width = width;
    image.height = height;
    image.source_format = pixel_format_from_code(format_code);
    if (!pixels) return image;
    if (!pixel_size_matches(image.source_format, width, height, size)) {
        return image;
    }

    const std::size_t count = std::size_t(width) * height;
    image.rgba.resize(count * 4);
    u8* out = image.rgba.data();

    switch (image.source_format) {
    case PixelFormat::Bgra8888:
        for (std::size_t i = 0; i < count; ++i) {
            const u8 b = pixels[i * 4 + 0];
            const u8 g = pixels[i * 4 + 1];
            const u8 r = pixels[i * 4 + 2];
            const u8 a = pixels[i * 4 + 3];
            out[i * 4 + 0] = r;
            out[i * 4 + 1] = g;
            out[i * 4 + 2] = b;
            out[i * 4 + 3] = a;
        }
        break;

    case PixelFormat::Rgb565:
        for (std::size_t i = 0; i < count; ++i) {
            const u16 v = u16(pixels[i * 2]) | (u16(pixels[i * 2 + 1]) << 8);
            const u32 r5 = (v >> 11) & 0x1F;
            const u32 g6 = (v >> 5) & 0x3F;
            const u32 b5 = v & 0x1F;
            out[i * 4 + 0] = expand5(r5);
            out[i * 4 + 1] = expand6(g6);
            out[i * 4 + 2] = expand5(b5);
            out[i * 4 + 3] = 255;
        }
        break;

    case PixelFormat::Argb4444:
        // thtk comment: "0xGB 0xAR" -- byte 0 holds GB, byte 1 holds AR.
        for (std::size_t i = 0; i < count; ++i) {
            const u16 v = u16(pixels[i * 2]) | (u16(pixels[i * 2 + 1]) << 8);
            const u32 a4 = (v >> 12) & 0xF;
            const u32 r4 = (v >> 8) & 0xF;
            const u32 g4 = (v >> 4) & 0xF;
            const u32 b4 = v & 0xF;
            out[i * 4 + 0] = expand4(r4);
            out[i * 4 + 1] = expand4(g4);
            out[i * 4 + 2] = expand4(b4);
            out[i * 4 + 3] = expand4(a4);
        }
        break;

    case PixelFormat::Gray8:
        for (std::size_t i = 0; i < count; ++i) {
            const u8 g = pixels[i];
            out[i * 4 + 0] = g;
            out[i * 4 + 1] = g;
            out[i * 4 + 2] = g;
            out[i * 4 + 3] = 255;
        }
        break;

    default:
        image.rgba.clear();
        break;
    }

    return image;
}

}  // namespace th185
