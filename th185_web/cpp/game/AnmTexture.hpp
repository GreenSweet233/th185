#pragma once
// TH185 ANM texture decoding: raw THTX pixels -> RGBA8.
//
// ## Format codes
//
// ANM uses thtk's format_t (NOT the 20..80 codes th15 uses for .sht):
//
//   1 = BGRA8888   (4 bytes/px)
//   3 = RGB565     (2 bytes/px)
//   5 = ARGB4444   (2 bytes/px)
//   7 = GRAY8      (1 byte/px)
//
// Verified against the shipped data: every THTX block satisfies
// size == width * height * bytes_per_pixel for one of these codes, and the
// measured distribution is {5: 32 files, 1: 19, 3: 4}. See docs/ANM.md.
//
// ## Why decode to RGBA8
//
// The renderer wants one layout. Doing the conversion at the resource boundary
// keeps every later stage (sprite blitting, resampling, blending) free of
// per-format branches -- the same split th15 uses with asset_pixel_format().

#include "Types.hpp"

#include <cstddef>
#include <vector>

namespace th185 {

enum class PixelFormat : u8 {
    Unknown = 0,
    Bgra8888 = 1,
    Rgb565 = 3,
    Argb4444 = 5,
    Gray8 = 7,
};

// Maps a raw ANM format code to PixelFormat. Returns Unknown for codes we have
// not seen, rather than aborting -- a future game revision may add one and a
// hard abort would turn a new format into a crash.
PixelFormat pixel_format_from_code(u32 code);

// Bytes per pixel for a format, or 0 when unknown.
u32 pixel_bytes(PixelFormat format);

// True when `size` equals `width * height * bytes_per_pixel`.
bool pixel_size_matches(PixelFormat format, u32 width, u32 height, u32 size);

struct DecodedImage {
    u32 width = 0;
    u32 height = 0;
    PixelFormat source_format = PixelFormat::Unknown;
    // Tightly packed RGBA8, width * height * 4 bytes.
    std::vector<u8> rgba;

    bool valid() const {
        return width != 0 && height != 0 &&
               rgba.size() == std::size_t(width) * height * 4;
    }
};

// Decodes `size` bytes of `pixels` into RGBA8. Returns an image with
// valid() == false when the format is unknown or the size does not match.
DecodedImage decode_texture(u32 format_code, u32 width, u32 height,
                            const u8* pixels, u32 size);

}  // namespace th185
