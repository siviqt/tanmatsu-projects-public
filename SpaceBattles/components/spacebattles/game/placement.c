// SPDX-License-Identifier: MIT
#include "game.h"
static bool search(sb_fleet_t *f, unsigned ship, unsigned *budget, sb_random_fn random, void *ctx) {
    while (ship < SB_SHIPS && f->ships[ship].placed) ++ship;
    if (ship == SB_SHIPS) return true;
    unsigned start = random ? random(ctx) % (SB_CELLS * 2) : 0;
    for (unsigned i = 0; i < SB_CELLS * 2 && *budget; ++i) {
        --*budget;
        unsigned candidate = (start + i) % (SB_CELLS * 2);
        unsigned cell = candidate / 2;
        if (!sb_place(f, ship, cell % SB_BOARD_SIDE, cell / SB_BOARD_SIDE, candidate % 2)) continue;
        if (search(f, ship + 1, budget, random, ctx)) return true;
        f->ships[ship] = (sb_ship_t){0};
    }
    return false;
}
bool sb_complete(sb_fleet_t *f, unsigned budget, sb_random_fn random, void *ctx, sb_completion_t *result) {
    if (!f || !result || f->locked || !sb_fleet_valid(f, false)) return false;
    sb_fleet_t candidate = *f;
    if (search(&candidate, 0, &budget, random, ctx)) *result = SB_COMPLETED;
    else {
        candidate = (sb_fleet_t){0};
        /* Known legal fallback needs no random source or additional search. */
        for (unsigned s = 0; s < SB_SHIPS; ++s) sb_place(&candidate, s, 0, s, false);
        *result = SB_REPLACED;
    }
    *f = candidate;
    return true;
}
