#include "WaveSelector.hpp"

namespace th185 {

namespace {

// VA 0x4B5D50, 111 entries. Index 0 is a placeholder and is never a real
// template, so there are 110 selectable templates:
//   1 tutorial boss + 27 regular bosses + 3 tutorial waves + 79 regular waves.
const std::array<std::string, WAVE_TABLE_SIZE> kNames = {
    "",
    "Boss01t",
    "Boss01",
    "Boss02",
    "Boss03",
    "Boss04",
    "Boss05",
    "Boss06",
    "Boss07",
    "Boss08",
    "Boss09",
    "Boss10",
    "Boss11",
    "Boss12",
    "Boss13",
    "Boss14",
    "Boss15",
    "Boss16",
    "Boss17",
    "Boss18",
    "Boss19",
    "Boss20",
    "Boss21",
    "Boss22",
    "Boss23",
    "Boss24",
    "Boss25",
    "Boss26",
    "Boss27",
    "Wave01t",
    "Wave02t",
    "Wave03t",
    "Wave01",
    "Wave02",
    "Wave03",
    "Wave04",
    "Wave05",
    "Wave06",
    "Wave07",
    "Wave08",
    "Wave09",
    "Wave10",
    "Wave11",
    "Wave12",
    "Wave13",
    "Wave14",
    "Wave15",
    "Wave16",
    "Wave17",
    "Wave18",
    "Wave19",
    "Wave20",
    "Wave21",
    "Wave22",
    "Wave23",
    "Wave24",
    "Wave25",
    "Wave26",
    "Wave27",
    "Wave28",
    "Wave29",
    "Wave30",
    "Wave31",
    "Wave32",
    "Wave33",
    "Wave34",
    "Wave35",
    "Wave36",
    "Wave37",
    "Wave38",
    "Wave39",
    "Wave40",
    "Wave41",
    "Wave42",
    "Wave43",
    "Wave44",
    "Wave45",
    "Wave46",
    "Wave47",
    "Wave48",
    "Wave49",
    "Wave50",
    "Wave51",
    "Wave52",
    "Wave53",
    "Wave54",
    "Wave55",
    "Wave56",
    "Wave57",
    "Wave58",
    "Wave59",
    "Wave60",
    "Wave61",
    "Wave62",
    "Wave63",
    "Wave64",
    "Wave65",
    "Wave66",
    "Wave67",
    "Wave68",
    "Wave69",
    "Wave70",
    "Wave71",
    "Wave72",
    "Wave73",
    "Wave74",
    "Wave75",
    "Wave76",
    "Wave77",
    "Wave78",
    "Wave79",
};

}  // namespace

const std::array<std::string, WAVE_TABLE_SIZE>& wave_names() { return kNames; }

u32 wave_weight_total(const std::array<u32, WAVE_TABLE_SIZE>& weights,
                      u32 skip_index) {
    // Pass 1 of RVA 0x3D017-0x3D044. Both passes skip `skip_index`, which is
    // what prevents the same template from being drawn twice in a row.
    u32 total = 0;
    for (u32 i = 0; i < WAVE_TABLE_SIZE; ++i) {
        if (i == skip_index) continue;
        total += weights[i];
    }
    return total;
}

i32 wave_pick_index(const std::array<u32, WAVE_TABLE_SIZE>& weights,
                    u32 roll, u32 skip_index) {
    // Pass 2 of RVA 0x3D080-0x3D0A4. The original compares the running value
    // as a SIGNED int and stops when it goes negative, so the subtraction must
    // be done in signed arithmetic — an unsigned compare would never trigger.
    i32 remaining = i32(roll);
    for (u32 i = 0; i < WAVE_TABLE_SIZE; ++i) {
        if (i == skip_index) continue;
        remaining -= i32(weights[i]);
        if (remaining < 0) return i32(i);
    }
    return -1;
}

i32 select_wave(const std::array<u32, WAVE_TABLE_SIZE>& weights, Rng& rng,
                u32 skip_index) {
    // Exactly ONE RNG call, then one modulo — matching the original sequence
    // (RVA 0x3D053 `call 0x2800` followed by `div`). Adding or removing a call
    // here desynchronises every subsequent draw.
    const u32 total = wave_weight_total(weights, skip_index);
    if (total == 0) return -1;
    const u32 roll = rng.next_below(total);
    return wave_pick_index(weights, roll, skip_index);
}

u32 forced_boss_index(u32 stage, u32 card_id) {
    // RVA 0x3D121 `mov esi, 0x1B` and 0x3D151 `mov esi, 0x1C`, guarded by
    // checks on the stage number [0x4D1004] and the selected card id.
    // See docs/WAVES.md 9.
    if (stage == 5 && card_id == 0) return WAVE_FORCED_BOSS_STAGE5;   // Boss26
    if (stage == 7 && card_id == 66) return WAVE_FORCED_BOSS_STAGE7;  // Boss27
    return 0xFFFFFFFFu;
}

}  // namespace th185
