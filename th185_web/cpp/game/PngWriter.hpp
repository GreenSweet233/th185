#pragma once
// Minimal PNG writer (RGBA8, no compression beyond stored deflate blocks).
//
// The project needs to LOOK at what the rasteriser produced. Pulling in a
// library for that would be a new dependency for one debugging feature, so this
// writes the smallest valid PNG: IHDR + a stored-deflate IDAT + IEND.
//
// Not suitable for large images or for shipping; it exists so a harness can
// dump a frame and a human can inspect it.

#include "Types.hpp"

#include <vector>

namespace th185 {

// Encodes into `out`. Returns false when the input size is wrong.
bool encode_png(u32 width, u32 height, const std::vector<u8>& rgba,
                std::vector<u8>& out);

bool write_png(const char* path, u32 width, u32 height,
               const std::vector<u8>& rgba);

}  // namespace th185
