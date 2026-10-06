// SPDX-License-Identifier: MIT
#include "internal.h"
#include <string.h>
void sb_snapshot(sb_match_t *m) {
    for (unsigned p = 0; p < 2; ++p) {
        m->allowance[p] = m->spec.rules.id == SB_CLASSIC ? 1 : (uint8_t)sb_alive(&m->fleets[p]);
        m->selected[p] = 0;
        m->salvo_locked[p] = false;
        memset(m->selection[p], 0, sizeof(m->selection[p]));
    }
    m->phase = SB_TARGETING;
    m->phase_started_ms = m->now_ms;
}
void sb_begin_combat(sb_match_t *m) {
    if (!m->ready[0] || !m->ready[1]) return;
    m->sequence = 1;
    m->actor = m->spec.starter;
    sb_snapshot(m);
}
bool sb_match_init(sb_match_t *m, const sb_spec_t *spec, uint64_t now) {
    if (!m || !sb_spec_valid(spec)) return false;
    sb_spec_t frozen = *spec;
    *m = (sb_match_t){ .spec = frozen, .phase = SB_DEPLOYMENT, .now_ms = now, .phase_started_ms = now };
    sb_knowledge_init(&m->knowledge[0]);
    sb_knowledge_init(&m->knowledge[1]);
    return true;
}
bool sb_match_place(sb_match_t *m, unsigned p, unsigned ship, unsigned x, unsigned y, bool vertical) {
    return m && p < 2 && m->phase == SB_DEPLOYMENT && !m->paused && !m->ready[p] &&
        sb_place(&m->fleets[p], ship, x, y, vertical);
}
bool sb_match_ready(sb_match_t *m, unsigned p) {
    if (!m || p > 1 || m->phase != SB_DEPLOYMENT || m->paused || m->ready[p] ||
        !sb_fleet_valid(&m->fleets[p], true)) return false;
    m->ready[p] = m->fleets[p].locked = true;
    sb_begin_combat(m);
    return true;
}
unsigned sb_allowance(const sb_match_t *m, unsigned p) {
    if (!m || p > 1 || m->phase != SB_TARGETING || m->paused || m->salvo_locked[p] ||
        (m->spec.rules.id == SB_CLASSIC && p != m->actor)) return 0;
    unsigned unknown = 0;
    for (unsigned c = 0; c < SB_CELLS; ++c) unknown += m->knowledge[p].cells[c] == SB_UNKNOWN;
    return unknown < m->allowance[p] ? unknown : m->allowance[p];
}
bool sb_command(const sb_match_t *m, unsigned p, uint32_t sequence) {
    return m && p < 2 && m->phase == SB_TARGETING && !m->paused && !m->salvo_locked[p] &&
        m->sequence == sequence && (m->spec.rules.id != SB_CLASSIC || m->actor == p);
}
bool sb_select(sb_match_t *m, unsigned p, uint32_t sequence, const uint8_t *cells, size_t count) {
    if (!sb_command(m, p, sequence) || count > sb_allowance(m, p) || (count && !cells) ||
        (m->spec.rules.id == SB_CLASSIC && count != 1)) return false;
    bool seen[SB_CELLS] = {0};
    for (size_t i = 0; i < count; ++i) {
        if (cells[i] >= SB_CELLS || seen[cells[i]] || m->knowledge[p].cells[cells[i]] != SB_UNKNOWN) return false;
        seen[cells[i]] = true;
    }
    /* Permit callers to use the current selection as the source. */
    if (count) memmove(m->selection[p], cells, count);
    m->selected[p] = (uint8_t)count;
    return true;
}
bool sb_match_tick(sb_match_t *m, uint64_t now, sb_random_fn random, void *context) {
    if (!m || now < m->now_ms) return false;
    uint64_t delta = now - m->now_ms;
    m->now_ms = now;
    if (m->paused) {
        /* Shift deployment origin to exclude explicit single-player pause. */
        m->phase_started_ms += delta;
        return true;
    }
    if (m->phase == SB_FINISHED) return true;
    if (m->phase == SB_DEPLOYMENT) {
        if (m->spec.deployment_ms && now - m->phase_started_ms >= m->spec.deployment_ms) {
            for (unsigned p = 0; p < 2; ++p) {
                if (m->ready[p]) continue;
                sb_completion_t result;
                if (!sb_complete(&m->fleets[p], SB_PLACEMENT_BUDGET, random, context, &result)) return false;
                m->replaced[p] = result == SB_REPLACED;
                m->ready[p] = m->fleets[p].locked = true;
            }
            sb_begin_combat(m);
        }
        return true;
    }
    if (m->spec.match_ms) {
        uint64_t remaining = m->spec.match_ms - m->combat_elapsed_ms;
        m->combat_elapsed_ms += delta < remaining ? delta : remaining;
        m->clock_expired = m->combat_elapsed_ms == m->spec.match_ms;
    }
    if (m->phase == SB_TARGETING && (m->clock_expired ||
        (m->spec.selection_ms && now - m->phase_started_ms >= m->spec.selection_ms))) {
        m->salvo_locked[0] = m->salvo_locked[1] = true;
        m->phase = SB_LOCKED;
    }
    if (m->phase == SB_PRESENTING && m->clock_expired) sb_check_result(m);
    return true;
}
bool sb_match_pause(sb_match_t *m, bool paused, uint64_t now) {
    if (!m || m->spec.play_type != SB_SINGLE_PLAYER || m->phase == SB_FINISHED) return false;
    if (!sb_match_tick(m, now, NULL, NULL)) return false;
    if (m->phase == SB_FINISHED) return false;
    m->paused = paused;
    return true;
}
bool sb_match_advance(sb_match_t *m, uint32_t sequence) {
    if (!m || m->paused || m->phase != SB_PRESENTING || m->sequence != sequence || m->sequence == UINT32_MAX) return false;
    ++m->sequence;
    if (m->spec.rules.id == SB_CLASSIC) m->actor = (uint8_t)(1 - m->actor);
    sb_snapshot(m);
    return true;
}
bool sb_match_surrender(sb_match_t *m, unsigned p) {
    if (!m || p > 1 || m->phase == SB_FINISHED) return false;
    m->outcome = p ? SB_WIN_A : SB_WIN_B;
    m->phase = SB_FINISHED;
    return true;
}
bool sb_match_abandon(sb_match_t *m) {
    if (!m || m->phase == SB_FINISHED) return false;
    m->outcome = SB_ABANDONED;
    m->phase = SB_FINISHED;
    return true;
}
