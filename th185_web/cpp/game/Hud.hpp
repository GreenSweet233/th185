#pragma once
// TH185 HUD: the frame around the play field plus the counters inside it.
//
// ## Provenance
//
// The layout comes from a screenshot of the original, and the art comes from
// `front.anm`, whose sprite table matches that layout exactly:
//
//   [0]  256x960   left side panel
//   [1]  256x960   right side panel
//   [2]  768x32    top bar
//   [3]  768x32    bottom bar
//   [7..18] 32x40  twelve small cells (the money / power icons and digits)
//   [20..26] 512x64 wide text banners
//
// ## What this is and is not
//
// This draws the frame and the counters the original shows (money, bullet
// money, Power, and the two market modifiers). It does NOT draw the eight card
// slots along the bottom-left, because the card art lives in an atlas that has
// not been located.
//
// Digits come from `ascii.anm`: 320 sprites on a 14x14 grid, 18 per row, which
// is the original glyph sheet.

#include "AnmResource.hpp"
#include "SoftwareRaster.hpp"
#include "Types.hpp"

#include <array>
#include <string>

namespace th185 {

// One bank the HUD can draw from.
struct HudAtlas {
    const AnmResource* resource = nullptr;
    const DecodedImage* texture = nullptr;
};

// Values the HUD displays. Filled by Application each frame.
struct HudState {
    u64 score = 0;
    u32 lives = 0;
    u32 bombs = 0;
    u32 money = 0;
    u32 bullet_money = 0;
    i32 power = 0;
    // The market modifiers TH185 shows under Power. Percentages, already
    // rounded; the original prints them with a leading sign.
    i32 reload_percent = 0;
    i32 break_percent = 0;
    bool market_open = false;
    // Equipped loadout: the card indices to draw in the bottom-left slots.
    // Empty slots are UINT32_MAX.
    static constexpr u32 SLOT_COUNT = 8;
    std::array<u32, SLOT_COUNT> slots{};
    u32 slot_count = 0;
    // `icons[card_id]` is the ability.anm atlas cell for that card, taken from
    // CardEntry::icon_left. Indexed by card id, so the array is sized to the
    // card table.
    static constexpr u32 ICON_COUNT = 87;
    std::array<u32, ICON_COUNT> icons{};
    u32 icon_count = ICON_COUNT;
};

class Hud {
public:
    // `frame` supplies the border art, `glyphs` the digits, `cards` the card
    // icons (ability.anm, whose 157 sprites cover every card's icon_left).
    void bind(const HudAtlas& frame, const HudAtlas& glyphs, const HudAtlas& cards);
    bool bound() const { return frame_.resource != nullptr; }

    // Draws the whole HUD into `raster`. Returns the number of quads drawn.
    u32 draw(SoftwareRaster& raster, const HudState& state);

    u32 quads() const { return quads_; }
    u32 glyph_quads() const { return glyph_quads_; }

private:
    bool draw_sprite(SoftwareRaster& raster, const HudAtlas& atlas,
                     u32 index, float x, float y, float scale = 1.0f);
    u32 draw_text(SoftwareRaster& raster, const std::string& text,
                  float x, float y, float scale, Rgba8 tint);
    u32 draw_number(SoftwareRaster& raster, u64 value, float right, float y,
                    float scale, Rgba8 tint);

    HudAtlas frame_;
    HudAtlas glyphs_;
    HudAtlas cards_;
    u32 quads_ = 0;
    u32 glyph_quads_ = 0;
};

}  // namespace th185