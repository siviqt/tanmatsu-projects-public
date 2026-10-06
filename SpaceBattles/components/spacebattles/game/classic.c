// SPDX-License-Identifier: MIT
#include "internal.h"
bool sb_classic_fire(sb_match_t *m, unsigned player, uint32_t sequence, uint8_t cell) {
    if (!m || m->spec.rules.id != SB_CLASSIC || !sb_select(m, player, sequence, &cell, 1)) return false;
    m->salvo_locked[player] = true;
    m->phase = SB_LOCKED;
    return true;
}
