// SPDX-License-Identifier: MIT
#include "game.h"
#include <string.h>
static const sb_rules_t rules[] = {
    { SB_CLASSIC, 1, SB_BOARD_SIDE, SB_SHIPS, {5, 4, 3, 3, 2} },
    { SB_ESCALATED, 1, SB_BOARD_SIDE, SB_SHIPS, {5, 4, 3, 3, 2} },
};
const sb_rules_t *sb_rules(sb_ruleset_t id) {
    return id == SB_CLASSIC ? &rules[0] : id == SB_ESCALATED ? &rules[1] : NULL;
}
bool sb_spec_make(sb_spec_t *out, sb_ruleset_t id, sb_play_type_t type, uint8_t starter) {
    const sb_rules_t *r = sb_rules(id);
    if (!out || !r || (type != SB_SINGLE_PLAYER && type != SB_VERSUS && type != SB_SHARED_DEVICE) ||
        starter > 1) return false;
    *out = (sb_spec_t){ .rules = *r, .play_type = type, .starter = starter };
    return true;
}
bool sb_spec_valid(const sb_spec_t *s) {
    sb_spec_t expected;
    return s && sb_spec_make(&expected, s->rules.id, s->play_type, s->starter) &&
        s->rules.version == expected.rules.version && s->rules.board_side == SB_BOARD_SIDE &&
        s->rules.ship_count == SB_SHIPS && !memcmp(s->rules.lengths, expected.rules.lengths, SB_SHIPS) &&
        s->deployment_ms == expected.deployment_ms && s->selection_ms == expected.selection_ms &&
        s->match_ms == expected.match_ms;
}
bool sb_series_starter(sb_series_t *s, sb_ruleset_t id, bool same, sb_random_fn random,
                       void *context, uint8_t *starter) {
    if (!s || !starter || !sb_rules(id)) return false;
    if (id != SB_CLASSIC) { *s = (sb_series_t){0}; *starter = 0; return true; }
    if (same && s->classic) *starter = (uint8_t)(1 - s->last_starter);
    else { if (!random) return false; *starter = (uint8_t)(random(context) & 1u); }
    s->classic = true;
    s->last_starter = *starter;
    return true;
}
