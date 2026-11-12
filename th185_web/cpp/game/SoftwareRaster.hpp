#pragma once
// TH185 software rasteriser.
//
// ## Why software, not GLES
//
// th15 drives a full GLES3 pipeline (`portable/sdl/Renderer.cpp`, 34 KB).
// Reproducing that needs a window, a GL context and a shader toolchain, none of
// which exist here yet. A software rasteriser gets to a verifiable IMAGE in one
// step: it is plain C++, produces bytes, and those bytes can be diffed against
// expectations.
//
// This is a stepping stone, not the destination. It answers "does the ANM tree
// produce the right geometry and colours?" before any GPU is involved.
//
// ## Scope
//
//   * RGBA8 target with alpha blending
//   * textured and untextured axis-aligned / rotated quads
//   * the 10 blend modes th15 uses (see `AnmBlendMode` below)
//   * bilinear or nearest sampling with repeat / clamp / mirror addressing
//   * NO depth buffer, NO fog, NO geometry shaders, NO primitives (opcode
//     600-611 draw fans/strips that are not implemented here)

#include "AnmTexture.hpp"
#include "Types.hpp"

#include <cstddef>
#include <vector>

namespace th185 {

// Blend modes as th15 maps them in AnmRenderer::material.
enum class AnmBlendMode : u8 {
    Alpha = 0,          // SrcAlpha / InvSrcAlpha
    Additive = 1,       // SrcAlpha / One
    Subtractive = 2,    // SrcAlpha / One, ReverseSubtract
    Opaque = 3,         // One / Zero
    MultiplyInverse = 4,// InvDstColor / InvSrcColor
    MultiplyDst = 5,    // DstColor / Zero
    InverseSrcAlpha = 6,// InvSrcColor / InvSrcAlpha
    DestAlpha = 7,      // DstAlpha / InvDstAlpha
    Minimum = 8,
    Maximum = 9,
};

AnmBlendMode blend_mode_from_flags(u32 flags);

// Address modes for texture sampling.
enum class AnmAddress : u8 { Repeat = 0, Clamp = 1, Mirror = 2 };

struct Rgba8 {
    u8 r = 0, g = 0, b = 0, a = 0;
};

// A single textured or coloured quad, in target pixels.
struct AnmQuad {
    // Four corners, clockwise from top-left.
    float x[4] = {0, 0, 0, 0};
    float y[4] = {0, 0, 0, 0};
    // Texture coordinates per corner, in 0..1.
    float u[4] = {0, 0, 1, 1};
    float v[4] = {0, 1, 1, 0};
    // Tint, applied per channel (0..255). 255 = unchanged.
    Rgba8 tint{255, 255, 255, 255};
    AnmBlendMode blend = AnmBlendMode::Alpha;
    // Source texture; null means draw the tint as a solid colour.
    const DecodedImage* texture = nullptr;
    AnmAddress address_u = AnmAddress::Clamp;
    AnmAddress address_v = AnmAddress::Clamp;
    bool nearest = false;
};

class SoftwareRaster {
public:
    void resize(u32 width, u32 height);
    void clear(Rgba8 color = Rgba8{0, 0, 0, 0});
    void draw(const AnmQuad& quad);

    u32 width() const { return width_; }
    u32 height() const { return height_; }
    const std::vector<u8>& pixels() const { return pixels_; }

    // Diagnostics.
    u32 quads_drawn = 0;
    u32 pixels_written = 0;
    u32 quads_rejected = 0;

private:
    Rgba8 sample(const AnmQuad& quad, float u, float v) const;
    void blend(u32 x, u32 y, Rgba8 source, AnmBlendMode mode);

    u32 width_ = 0;
    u32 height_ = 0;
    std::vector<u8> pixels_;
};

}  // namespace th185
