// SPDX-License-Identifier: MIT
#include "internal.h"
bool sb_escalated_lock(sb_match_t *m, unsigned player, uint32_t sequence, bool confirmed) {
    if (!m || m->spec.rules.id != SB_ESCALATED || !sb_command(m, player, sequence)) return false;
    if (m->selected[player] < sb_allowance(m, player) && !confirmed) return false;
    m->salvo_locked[player] = true;
    if (m->salvo_locked[0] && m->salvo_locked[1]) m->phase = SB_LOCKED;
    return true;
}
