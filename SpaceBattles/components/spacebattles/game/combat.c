// SPDX-License-Identifier: MIT
#include "internal.h"
#include <string.h>
void sb_knowledge_init(sb_knowledge_t *k) {
    *k = (sb_knowledge_t){0};
    const sb_rules_t *r = sb_rules(SB_CLASSIC);
    for (unsigned s = 0; s < r->ship_count; ++s) ++k->remaining[r->lengths[s]];
}
bool sb_disclose(sb_knowledge_t *k, unsigned length, const uint8_t *cells) {
    if (!k || !cells || length < 2 || length > SB_SEGMENTS || !k->remaining[length]) return false;
    uint8_t sorted[SB_SEGMENTS];
    memcpy(sorted, cells, length);
    for (unsigned i = 0; i < length; ++i) {
        if (sorted[i] >= SB_CELLS || k->cells[sorted[i]] != SB_HIT) return false;
        for (unsigned j = i; j > 0 && sorted[j] < sorted[j - 1]; --j) {
            uint8_t swap = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = swap;
        }
    }
    unsigned step = sorted[1] - sorted[0];
    if (step != 1 && step != SB_BOARD_SIDE) return false;
    if (step == 1 && sorted[0] % SB_BOARD_SIDE + length > SB_BOARD_SIDE) return false;
    for (unsigned i = 0; i < length; ++i)
        if (sorted[i] != sorted[0] + i * step) return false;
    for (unsigned i = 0; i < length; ++i) k->cells[sorted[i]] = SB_SUNK;
    --k->remaining[length];
    return true;
}
void sb_attack(sb_match_t *m, unsigned player, uint8_t cell) {
    sb_fleet_t *defender = &m->fleets[1 - player];
    sb_knowledge_t *k = &m->knowledge[player];
    sb_event_t *event = &m->record.events[m->record.count++];
    *event = (sb_event_t){ .type = SB_ATTACK_EVENT, .attacker = (uint8_t)player, .cell = cell };
    defender->hit[cell] = true;
    k->cells[cell] = SB_MISS;
    for (unsigned s = 0; s < m->spec.rules.ship_count; ++s) {
        const sb_ship_t *ship = &defender->ships[s];
        unsigned length = m->spec.rules.lengths[s];
        bool contains = false, sunk = true;
        for (unsigned i = 0; i < length; ++i) {
            contains |= ship->cells[i] == cell;
            sunk &= defender->hit[ship->cells[i]];
        }
        if (!contains) continue;
        event->hit = true;
        k->cells[cell] = SB_HIT;
        if (sunk) {
            sb_event_t *disclosure = &m->record.events[m->record.count++];
            *disclosure = (sb_event_t){ .type = SB_SUNK_EVENT, .attacker = (uint8_t)player, .length = (uint8_t)length };
            memcpy(disclosure->cells, ship->cells, length);
            sb_disclose(k, length, ship->cells);
        }
        return;
    }
}
void sb_check_result(sb_match_t *m) {
    unsigned a = sb_alive(&m->fleets[0]), b = sb_alive(&m->fleets[1]);
    if (!a || !b) m->outcome = a ? SB_WIN_A : b ? SB_WIN_B : SB_DRAW;
    else if (m->clock_expired) {
        if (a == b) { a = sb_intact(&m->fleets[0]); b = sb_intact(&m->fleets[1]); }
        m->outcome = a > b ? SB_WIN_A : b > a ? SB_WIN_B : SB_DRAW;
    }
    if (m->outcome != SB_ONGOING) m->phase = SB_FINISHED;
}
bool sb_match_resolve(sb_match_t *m, uint32_t sequence, sb_record_t *record) {
    if (!m || !record || m->paused || m->phase != SB_LOCKED || m->sequence != sequence) return false;
    m->record = (sb_record_t){ .sequence = sequence };
    if (m->spec.rules.id == SB_CLASSIC) sb_attack(m, m->actor, m->selection[m->actor][0]);
    else {
        /* Both salvos were validated and snapshotted before either is applied.
         * Neither damage nor terminal checks can cancel the other salvo. */
        for (unsigned p = 0; p < 2; ++p)
            for (unsigned i = 0; i < m->selected[p]; ++i) sb_attack(m, p, m->selection[p][i]);
    }
    m->phase = SB_PRESENTING;
    sb_check_result(m);
    m->record.outcome = m->outcome;
    *record = m->record;
    return true;
}
