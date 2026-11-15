#include "Hud.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace th185 {


namespace {

// `ascii.anm` is a 14x14 grid, 18 cells per row. Sprite index 0 is the first
// glyph in the sheet; the original maps ASCII through this table.
constexpr u32 GLYPH_COLUMNS = 18;
constexpr float GLYPH_SIZE = 14.0f;

// The play field is 640x480 in this build. The original draws the field inside
// a frame, so the counters sit in the margin.
constexpr float PLAY_W = 640.0f;
constexpr float PLAY_H = 480.0f;

// Sprite indices in front.anm, read off its sprite table.
constexpr u32 FRAME_LEFT = 0;    // 256x960
constexpr u32 FRAME_RIGHT = 1;   // 256x960
constexpr u32 FRAME_TOP = 2;     // 768x32
constexpr u32 FRAME_BOTTOM = 3;  // 768x32

Rgba8 white() { return Rgba8{255, 255, 255, 255}; }
Rgba8 gold() { return Rgba8{255, 220, 96, 255}; }
Rgba8 cyan() { return Rgba8{160, 220, 255, 255}; }
Rgba8 magenta() { return Rgba8{255, 160, 220, 255}; }

}  // namespace

void Hud::bind(const HudAtlas& frame, const HudAtlas& glyphs) {
    frame_ = frame;
    glyphs_ = glyphs;
}

bool Hud::draw_sprite(SoftwareRaster& raster, const HudAtlas& atlas,
                      u32 index, float x, float y, float scale) {
    if (atlas.resource == nullptr || atlas.texture == nullptr) return false;
    if (index >= atlas.resource->sprites.size()) return false;
    const AnmSprite& sprite = atlas.resource->sprites[index];

    AnmQuad quad;
    const float w = sprite.w * scale;
    const float h = sprite.h * scale;
    quad.x[0] = x;     quad.y[0] = y;
    quad.x[1] = x + w; quad.y[1] = y;
    quad.x[2] = x + w; quad.y[2] = y + h;
    quad.x[3] = x;     quad.y[3] = y + h;
    // The sprite carries normalised UVs already.
    quad.u[0] = sprite.u0; quad.v[0] = sprite.v0;
    quad.u[1] = sprite.u1; quad.v[1] = sprite.v0;
    quad.u[2] = sprite.u1; quad.v[2] = sprite.v1;
    quad.u[3] = sprite.u0; quad.v[3] = sprite.v1;
    quad.texture = atlas.texture;
    quad.blend = AnmBlendMode::Alpha;
    quad.nearest = true;
    raster.draw(quad);
    return true;
}


u32 Hud::draw_text(SoftwareRaster& raster, const std::string& text,
                   float x, float y, float scale, Rgba8 tint) {
    if (glyphs_.resource == nullptr || glyphs_.texture == nullptr) {
        return 0;
    }
    u32 drawn = 0;
    float cursor = x;
    for (const char ch : text) {
        // The sheet is indexed by the ASCII code directly: sprite 33 is '!',
        // sprite 65 is 'A'. Subtracting 32 (the printable-ASCII convention)
        // shifted every glyph by one cell and drew the wrong characters --
        // the frame appeared but no legible text did.
        const u32 index = u32(u8(ch));
        if (ch == ' ') {
            cursor += GLYPH_SIZE * 0.55f * scale;
            continue;
        }
        if (index >= glyphs_.resource->sprites.size()) continue;
        const AnmSprite& sprite = glyphs_.resource->sprites[index];

        AnmQuad quad;
        const float w = sprite.w * scale;
        const float h = sprite.h * scale;
        quad.x[0] = cursor;     quad.y[0] = y;
        quad.x[1] = cursor + w; quad.y[1] = y;
        quad.x[2] = cursor + w; quad.y[2] = y + h;
        quad.x[3] = cursor;     quad.y[3] = y + h;
        quad.u[0] = sprite.u0; quad.v[0] = sprite.v0;
        quad.u[1] = sprite.u1; quad.v[1] = sprite.v0;
        quad.u[2] = sprite.u1; quad.v[2] = sprite.v1;
        quad.u[3] = sprite.u0; quad.v[3] = sprite.v1;
        quad.texture = glyphs_.texture;
        quad.tint = tint;
        quad.blend = AnmBlendMode::Alpha;
        quad.nearest = true;
        raster.draw(quad);
        ++drawn;
        cursor += w;
    }
    glyph_quads_ += drawn;
    return drawn;
}

u32 Hud::draw_number(SoftwareRaster& raster, u64 value, float right, float y,
                     float scale, Rgba8 tint) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    const std::string text(buffer);
    // Right-align: each digit is one glyph wide.
    const float width = float(text.size()) * GLYPH_SIZE * scale;
    return draw_text(raster, text, right - width, y, scale, tint);
}

u32 Hud::draw(SoftwareRaster& raster, const HudState& state) {
    quads_ = 0;
    glyph_quads_ = 0;
    if (!bound()) return 0;

    // Everything is drawn INSIDE the 640x480 canvas.
    //
    // The original draws the field inside a frame, so its counters sit in the
    // margin outside the play area. This build has no margin: the canvas IS
    // the field. Laying the HUD out in the original's margin put every quad at
    // a negative x (measured: side panel at -96), so the frame was drawn and
    // nothing appeared. Anchoring to the canvas edges instead keeps the same
    // information visible.
    const float W = float(raster.width());
    const float H = float(raster.height());

    // --- frame: thin bars hugging the canvas edges --------------------------
    const float bar_scale = W / 768.0f;
    if (draw_sprite(raster, frame_, FRAME_TOP, 0.0f, 0.0f, bar_scale)) ++quads_;
    if (draw_sprite(raster, frame_, FRAME_BOTTOM, 0.0f,
                    H - 32.0f * bar_scale, bar_scale)) {
        ++quads_;
    }

    // --- score, centred at the top -----------------------------------------
    const float score_scale = 1.4f;
    char score_text[32];
    std::snprintf(score_text, sizeof(score_text), "%llu.%02llu",
                  static_cast<unsigned long long>(state.score / 100u),
                  static_cast<unsigned long long>(state.score % 100u));
    const float score_w = float(std::strlen(score_text)) * GLYPH_SIZE * score_scale;
    draw_text(raster, score_text, W * 0.5f - score_w * 0.5f, 10.0f,
              score_scale, white());

    // --- counters, right column --------------------------------------------
    const float small = 1.0f;
    const float line = GLYPH_SIZE * small + 3.0f;
    float row = 26.0f;
    const float label_x = W - 168.0f;
    const float value_x = W - 56.0f;

    draw_text(raster, "MONEY", label_x, row, small, gold());
    draw_number(raster, state.money, value_x + 40.0f, row, small, white());
    row += line;
    draw_text(raster, "BULLET", label_x, row, small, cyan());
    draw_number(raster, state.bullet_money, value_x + 40.0f, row, small, white());
    row += line * 1.6f;

    draw_text(raster, "Power", label_x, row, small, magenta());
    draw_number(raster, u64(state.power), value_x + 40.0f, row, small, white());
    row += line;

    if (state.market_open) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "M.Reload %+d%%", int(state.reload_percent));
        draw_text(raster, buffer, label_x, row, small, cyan());
        row += line;
        std::snprintf(buffer, sizeof(buffer), "M.Break %+d%%", int(state.break_percent));
        draw_text(raster, buffer, label_x, row, small, magenta());
    }

    // --- lives / bombs, bottom left ----------------------------------------
    const float foot_y = H - 24.0f;
    draw_text(raster, "LIFE", 8.0f, foot_y, small, white());
    draw_number(raster, u64(state.lives), 56.0f, foot_y, small, white());
    draw_text(raster, "BOMB", 76.0f, foot_y, small, white());
    draw_number(raster, u64(state.bombs), 124.0f, foot_y, small, white());

    return quads_;
}

}  // namespace th185