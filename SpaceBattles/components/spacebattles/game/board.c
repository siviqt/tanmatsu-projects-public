// SPDX-License-Identifier: MIT
#include "game.h"
#include <string.h>
bool sb_cell(unsigned x, unsigned y, uint8_t *cell) {
    if (!cell || x >= SB_BOARD_SIDE || y >= SB_BOARD_SIDE) return false;
    *cell = (uint8_t)(y * SB_BOARD_SIDE + x);
    return true;
}
bool sb_fleet_valid(const sb_fleet_t *f, bool complete) {
    if (!f) return false;
    bool occupied[SB_CELLS] = {0};
    const sb_rules_t *r = sb_rules(SB_CLASSIC);
    for (unsigned s = 0; s < r->ship_count; ++s) {
        if (!f->ships[s].placed) { if (complete) return false; else continue; }
        const uint8_t *c = f->ships[s].cells;
        unsigned length = r->lengths[s];
        if (c[0] >= SB_CELLS) return false;
        unsigned step = c[1] > c[0] ? c[1] - c[0] : 0;
        if (step != 1 && step != SB_BOARD_SIDE) return false;
        if (step == 1 && c[0] % SB_BOARD_SIDE + length > SB_BOARD_SIDE) return false;
        for (unsigned i = 0; i < length; ++i) {
            if (c[i] >= SB_CELLS || c[i] != c[0] + step * i || occupied[c[i]]) return false;
            occupied[c[i]] = true;
        }
    }
    return true;
}
bool sb_place(sb_fleet_t *f, unsigned ship, unsigned x, unsigned y, bool vertical) {
    if (!f || f->locked || ship >= SB_SHIPS || !sb_fleet_valid(f, false)) return false;
    unsigned length = sb_rules(SB_CLASSIC)->lengths[ship];
    if (x >= SB_BOARD_SIDE || y >= SB_BOARD_SIDE ||
        (vertical ? y : x) + length > SB_BOARD_SIDE) return false;
    sb_fleet_t candidate = *f;
    candidate.ships[ship] = (sb_ship_t){ .placed = true };
    for (unsigned i = 0; i < length; ++i)
        candidate.ships[ship].cells[i] = (uint8_t)(y * SB_BOARD_SIDE + x + i * (vertical ? SB_BOARD_SIDE : 1));
    if (!sb_fleet_valid(&candidate, false)) return false;
    *f = candidate;
    return true;
}
unsigned sb_intact(const sb_fleet_t *f) {
    unsigned total = 0;
    for (unsigned s = 0; s < SB_SHIPS; ++s)
        if (f->ships[s].placed)
            for (unsigned i = 0; i < sb_rules(SB_CLASSIC)->lengths[s]; ++i)
                total += !f->hit[f->ships[s].cells[i]];
    return total;
}
unsigned sb_alive(const sb_fleet_t *f) {
    unsigned total = 0;
    for (unsigned s = 0; s < SB_SHIPS; ++s) {
        bool alive = false;
        if (f->ships[s].placed)
            for (unsigned i = 0; i < sb_rules(SB_CLASSIC)->lengths[s]; ++i)
                alive |= !f->hit[f->ships[s].cells[i]];
        total += alive;
    }
    return total;
}
