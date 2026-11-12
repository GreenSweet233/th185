#include "SoftwareRaster.hpp"

#include <algorithm>
#include <cmath>

namespace th185 {

AnmBlendMode blend_mode_from_flags(u32 flags) {
    // th15 AnmRenderer::material reads the mode from flags bits 25..29.
    const u32 mode = (flags >> 25) & 0x1Fu;
    if (mode >= 9) return AnmBlendMode::Maximum;
    return AnmBlendMode(mode);
}

namespace {

inline u8 clamp_u8(float value) {
    if (!(value > 0.0f)) return 0;   // also catches NaN
    if (value >= 255.0f) return 255;
    return u8(value);
}

// Wraps `coord` into 0..size-1 according to the address mode.
inline i32 address(i32 coord, i32 size, AnmAddress mode) {
    if (size <= 0) return 0;
    if (mode == AnmAddress::Clamp) {
        if (coord < 0) return 0;
        if (coord >= size) return size - 1;
        return coord;
    }
    if (mode == AnmAddress::Mirror) {
        const i32 period = size * 2;
        i32 value = coord % period;
        if (value < 0) value += period;
        return value < size ? value : period - 1 - value;
    }
    i32 value = coord % size;
    if (value < 0) value += size;
    return value;
}

}  // namespace

void SoftwareRaster::resize(u32 width, u32 height) {
    width_ = width;
    height_ = height;
    pixels_.assign(std::size_t(width) * height * 4, 0);
}

void SoftwareRaster::clear(Rgba8 color) {
    for (std::size_t i = 0; i < pixels_.size(); i += 4) {
        pixels_[i + 0] = color.r;
        pixels_[i + 1] = color.g;
        pixels_[i + 2] = color.b;
        pixels_[i + 3] = color.a;
    }
}

Rgba8 SoftwareRaster::sample(const AnmQuad& quad, float u, float v) const {
    if (!quad.texture || quad.texture->rgba.empty()) return quad.tint;
    const u32 tw = quad.texture->width;
    const u32 th = quad.texture->height;
    if (tw == 0 || th == 0) return quad.tint;

    // Nearest: pick the texel containing (u,v).
    if (quad.nearest) {
        const i32 tx = address(i32(u * float(tw)), i32(tw), quad.address_u);
        const i32 ty = address(i32(v * float(th)), i32(th), quad.address_v);
        const std::size_t at = (std::size_t(ty) * tw + std::size_t(tx)) * 4;
        Rgba8 texel{quad.texture->rgba[at], quad.texture->rgba[at + 1],
                    quad.texture->rgba[at + 2], quad.texture->rgba[at + 3]};
        // Tint multiplies, matching th15's multiply_color (>>7 then min 255).
        texel.r = u8(std::min(255, (texel.r * quad.tint.r) >> 7));
        texel.g = u8(std::min(255, (texel.g * quad.tint.g) >> 7));
        texel.b = u8(std::min(255, (texel.b * quad.tint.b) >> 7));
        texel.a = u8(std::min(255, (texel.a * quad.tint.a) >> 7));
        return texel;
    }

    // Bilinear.
    const float fx = float(u * float(tw)) - 0.5f;
    const float fy = float(v * float(th)) - 0.5f;
    const i32 x0 = i32(std::floor(fx));
    const i32 y0 = i32(std::floor(fy));
    const float tx = fx - float(x0);
    const float ty = fy - float(y0);

    auto texel_at = [&](i32 x, i32 y) -> Rgba8 {
        const i32 cx = address(x, i32(tw), quad.address_u);
        const i32 cy = address(y, i32(th), quad.address_v);
        const std::size_t at = (std::size_t(cy) * tw + std::size_t(cx)) * 4;
        return Rgba8{quad.texture->rgba[at], quad.texture->rgba[at + 1],
                     quad.texture->rgba[at + 2], quad.texture->rgba[at + 3]};
    };

    const Rgba8 c00 = texel_at(x0, y0);
    const Rgba8 c10 = texel_at(x0 + 1, y0);
    const Rgba8 c01 = texel_at(x0, y0 + 1);
    const Rgba8 c11 = texel_at(x0 + 1, y0 + 1);

    auto mix = [&](u8 a, u8 b, u8 c, u8 d) {
        const float top = float(a) + (float(b) - float(a)) * tx;
        const float bottom = float(c) + (float(d) - float(c)) * tx;
        return clamp_u8(top + (bottom - top) * ty);
    };

    Rgba8 texel{mix(c00.r, c10.r, c01.r, c11.r),
                mix(c00.g, c10.g, c01.g, c11.g),
                mix(c00.b, c10.b, c01.b, c11.b),
                mix(c00.a, c10.a, c01.a, c11.a)};
    texel.r = u8(std::min(255, (texel.r * quad.tint.r) >> 7));
    texel.g = u8(std::min(255, (texel.g * quad.tint.g) >> 7));
    texel.b = u8(std::min(255, (texel.b * quad.tint.b) >> 7));
    texel.a = u8(std::min(255, (texel.a * quad.tint.a) >> 7));
    return texel;
}

void SoftwareRaster::blend(u32 x, u32 y, Rgba8 source, AnmBlendMode mode) {
    if (x >= width_ || y >= height_) return;
    const std::size_t at = (std::size_t(y) * width_ + x) * 4;
    const Rgba8 dest{pixels_[at], pixels_[at + 1], pixels_[at + 2], pixels_[at + 3]};

    float sr = float(source.r), sg = float(source.g), sb = float(source.b);
    float sa = float(source.a) / 255.0f;
    float dr = float(dest.r), dg = float(dest.g), db = float(dest.b);
    float da = float(dest.a) / 255.0f;

    float out_r = 0, out_g = 0, out_b = 0, out_a = 0;

    switch (mode) {
    case AnmBlendMode::Alpha:
        out_r = sr * sa + dr * (1.0f - sa);
        out_g = sg * sa + dg * (1.0f - sa);
        out_b = sb * sa + db * (1.0f - sa);
        out_a = sa + da * (1.0f - sa);
        break;
    case AnmBlendMode::Additive:
        out_r = sr * sa + dr;
        out_g = sg * sa + dg;
        out_b = sb * sa + db;
        out_a = sa + da;
        break;
    case AnmBlendMode::Subtractive:
        out_r = dr - sr * sa;
        out_g = dg - sg * sa;
        out_b = db - sb * sa;
        out_a = da;
        break;
    case AnmBlendMode::Opaque:
        out_r = sr;
        out_g = sg;
        out_b = sb;
        out_a = float(source.a);
        break;
    case AnmBlendMode::MultiplyInverse:
        out_r = (1.0f - dr / 255.0f) * (1.0f - sr / 255.0f) * 255.0f;
        out_g = (1.0f - dg / 255.0f) * (1.0f - sg / 255.0f) * 255.0f;
        out_b = (1.0f - db / 255.0f) * (1.0f - sb / 255.0f) * 255.0f;
        out_a = float(dest.a);
        break;
    case AnmBlendMode::MultiplyDst:
        out_r = dr * sr / 255.0f;
        out_g = dg * sg / 255.0f;
        out_b = db * sb / 255.0f;
        out_a = float(dest.a);
        break;
    case AnmBlendMode::InverseSrcAlpha:
        out_r = (1.0f - sr / 255.0f) * dr / 255.0f * 255.0f;
        out_g = (1.0f - sg / 255.0f) * dg / 255.0f * 255.0f;
        out_b = (1.0f - sb / 255.0f) * db / 255.0f * 255.0f;
        out_a = da * (1.0f - sa);
        break;
    case AnmBlendMode::DestAlpha:
        out_r = sr * da + dr * (1.0f - da);
        out_g = sg * da + dg * (1.0f - da);
        out_b = sb * da + db * (1.0f - da);
        out_a = sa + da * (1.0f - sa);
        break;
    case AnmBlendMode::Minimum:
        out_r = std::min(sr * sa + dr, dr);
        out_g = std::min(sg * sa + dg, dg);
        out_b = std::min(sb * sa + db, db);
        out_a = std::max(da, sa);
        break;
    case AnmBlendMode::Maximum:
        out_r = std::max(sr * sa + dr, dr);
        out_g = std::max(sg * sa + dg, dg);
        out_b = std::max(sb * sa + db, db);
        out_a = std::max(da, sa);
        break;
    }

    pixels_[at + 0] = clamp_u8(out_r);
    pixels_[at + 1] = clamp_u8(out_g);
    pixels_[at + 2] = clamp_u8(out_b);
    pixels_[at + 3] = clamp_u8(out_a * 255.0f);
}

void SoftwareRaster::draw(const AnmQuad& quad) {
    if (width_ == 0 || height_ == 0) return;

    // Bounding box, clipped to the target.
    float min_x = quad.x[0], max_x = quad.x[0];
    float min_y = quad.y[0], max_y = quad.y[0];
    for (u32 i = 1; i < 4; ++i) {
        min_x = std::min(min_x, quad.x[i]);
        max_x = std::max(max_x, quad.x[i]);
        min_y = std::min(min_y, quad.y[i]);
        max_y = std::max(max_y, quad.y[i]);
    }
    if (!(max_x > min_x) || !(max_y > min_y)) {
        ++quads_rejected;
        return;
    }

    const i32 x0 = std::max(0, i32(std::floor(min_x)));
    const i32 x1 = std::min(i32(width_) - 1, i32(std::ceil(max_x)));
    const i32 y0 = std::max(0, i32(std::floor(min_y)));
    const i32 y1 = std::min(i32(height_) - 1, i32(std::ceil(max_y)));
    if (x1 < x0 || y1 < y0) {
        ++quads_rejected;
        return;
    }

    ++quads_drawn;

    // The quad is split into triangles (0,1,2) and (0,2,3). Edge functions in
    // half-space form; the winding is fixed by the caller's corner order.
    const float ax = quad.x[0], ay = quad.y[0];
    const float bx = quad.x[1], by = quad.y[1];
    const float cx = quad.x[2], cy = quad.y[2];
    const float dx = quad.x[3], dy = quad.y[3];

    auto edge = [](float px, float py, float qx, float qy, float rx, float ry) {
        return (rx - px) * (qy - py) - (ry - py) * (qx - px);
    };

    const float area_a = edge(ax, ay, bx, by, cx, cy);
    const float area_b = edge(ax, ay, cx, cy, dx, dy);
    if (std::fabs(area_a) < 1e-6f || std::fabs(area_b) < 1e-6f) {
        ++quads_rejected;
        return;
    }

    for (i32 py = y0; py <= y1; ++py) {
        for (i32 px = x0; px <= x1; ++px) {
            const float fx = float(px) + 0.5f;
            const float fy = float(py) + 0.5f;

            float w0 = 0, w1 = 0, w2 = 0;
            float u = 0, v = 0;
            bool inside = false;

            if (edge(ax, ay, bx, by, fx, fy) * (area_a > 0 ? 1.0f : -1.0f) >= 0.0f &&
                edge(bx, by, cx, cy, fx, fy) * (area_a > 0 ? 1.0f : -1.0f) >= 0.0f &&
                edge(cx, cy, ax, ay, fx, fy) * (area_a > 0 ? 1.0f : -1.0f) >= 0.0f) {
                w0 = edge(bx, by, cx, cy, fx, fy) / area_a;
                w1 = edge(cx, cy, ax, ay, fx, fy) / area_a;
                w2 = edge(ax, ay, bx, by, fx, fy) / area_a;
                u = quad.u[0] * w0 + quad.u[1] * w1 + quad.u[2] * w2;
                v = quad.v[0] * w0 + quad.v[1] * w1 + quad.v[2] * w2;
                inside = true;
            } else if (edge(ax, ay, cx, cy, fx, fy) * (area_b > 0 ? 1.0f : -1.0f) >= 0.0f &&
                       edge(cx, cy, dx, dy, fx, fy) * (area_b > 0 ? 1.0f : -1.0f) >= 0.0f &&
                       edge(dx, dy, ax, ay, fx, fy) * (area_b > 0 ? 1.0f : -1.0f) >= 0.0f) {
                w0 = edge(cx, cy, dx, dy, fx, fy) / area_b;
                w1 = edge(dx, dy, ax, ay, fx, fy) / area_b;
                w2 = edge(ax, ay, cx, cy, fx, fy) / area_b;
                u = quad.u[0] * w0 + quad.u[2] * w1 + quad.u[3] * w2;
                v = quad.v[0] * w0 + quad.v[2] * w1 + quad.v[3] * w2;
                inside = true;
            }

            if (!inside) continue;
            const Rgba8 texel = sample(quad, u, v);
            if (texel.a == 0 && quad.blend != AnmBlendMode::Opaque) continue;
            blend(u32(px), u32(py), texel, quad.blend);
            ++pixels_written;
        }
    }
}

}  // namespace th185
