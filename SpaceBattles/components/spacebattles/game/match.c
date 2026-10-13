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
    (void)random; (void)context;
    m->now_ms = now;
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
    m->end_reason = p ? SB_END_SURRENDER_B : SB_END_SURRENDER_A;
    m->record.outcome = m->outcome; m->record.end_reason = m->end_reason;
    m->phase = SB_FINISHED;
    return true;
}
bool sb_match_leave(sb_match_t *m, unsigned p) {
    if(!sb_match_surrender(m,p)) return false;
    m->end_reason=p ? SB_END_LEFT_B : SB_END_LEFT_A;
    m->record.end_reason=m->end_reason;
    return true;
}
bool sb_match_abandon(sb_match_t *m) {
    if (!m || m->phase == SB_FINISHED) return false;
    m->outcome = SB_ABANDONED;
    m->end_reason = SB_END_ABORTED;
    m->record.outcome=m->outcome; m->record.end_reason=m->end_reason;
    m->phase = SB_FINISHED;
    return true;
}

/* Canonical outcome/reason validation is also the network trust boundary. */
bool sb_result_valid(sb_outcome_t o, sb_end_reason_t r) {
    switch(r) {
        case SB_END_NONE: return o==SB_ONGOING;
        case SB_END_COMBAT: return o==SB_WIN_A || o==SB_WIN_B || o==SB_DRAW;
        case SB_END_SURRENDER_A: case SB_END_LEFT_A: return o==SB_WIN_B;
        case SB_END_SURRENDER_B: case SB_END_LEFT_B: return o==SB_WIN_A;
        case SB_END_ABORTED: return o==SB_ABANDONED;
        default: return false;
    }
}
sb_result_t sb_result_for(sb_outcome_t o, sb_end_reason_t r, unsigned p) {
    if(p>1 || !sb_result_valid(o,r)) return SB_RESULT_ABORTED;
    if(o==SB_ONGOING) return SB_RESULT_ONGOING;
    if(o==SB_ABANDONED) return SB_RESULT_ABORTED;
    if(o==SB_DRAW) return SB_RESULT_DRAW;
    bool won=o==(p ? SB_WIN_B : SB_WIN_A);
    if(r==SB_END_SURRENDER_A || r==SB_END_SURRENDER_B)
        return won ? SB_RESULT_OPPONENT_SURRENDERED : SB_RESULT_SURRENDERED;
    if(r==SB_END_LEFT_A || r==SB_END_LEFT_B)
        return won ? SB_RESULT_OPPONENT_LEFT : SB_RESULT_LEFT;
    return won ? SB_RESULT_VICTORY : SB_RESULT_DEFEAT;
}
const char *sb_result_message(sb_result_t r) {
    switch(r) {
        case SB_RESULT_ONGOING: return "Match in progress";
        case SB_RESULT_VICTORY: return "You win";
        case SB_RESULT_DEFEAT: return "You lose";
        case SB_RESULT_DRAW: return "Draw";
        case SB_RESULT_SURRENDERED: return "You surrendered - you lose";
        case SB_RESULT_OPPONENT_SURRENDERED: return "Opponent surrendered - you win";
        case SB_RESULT_LEFT: return "You abandoned - you lose";
        case SB_RESULT_OPPONENT_LEFT: return "Opponent abandoned - you win";
        default: return "Match abandoned - no winner";
    }
}
