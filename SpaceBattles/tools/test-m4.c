// SPDX-License-Identifier: MIT
#include "game/game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); assert(x); } } while (0)
static uint32_t rng(void *context) {
    uint32_t *state = context;
    *state = *state * 1664525u + 1013904223u;
    return *state;
}
static sb_match_t deployed(sb_ruleset_t rules, sb_play_type_t type) {
    sb_match_t m;
    sb_spec_t spec;
    CHECK(sb_spec_make(&spec, rules, type, 0));
    CHECK(sb_match_init(&m, &spec, 100));
    for (unsigned p = 0; p < 2; ++p) {
        for (unsigned s = 0; s < SB_SHIPS; ++s) CHECK(sb_match_place(&m, p, s, 0, s, false));
        CHECK(sb_match_ready(&m, p));
    }
    CHECK(m.phase == SB_TARGETING && m.sequence == 1);
    return m;
}
static sb_record_t resolve(sb_match_t *m) {
    sb_record_t record;
    CHECK(sb_match_resolve(m, m->sequence, &record));
    sb_match_t before = *m;
    CHECK(!sb_match_resolve(m, m->sequence, &record));
    CHECK(!memcmp(&before, m, sizeof(*m)));
    return record;
}
static sb_record_t classic(sb_match_t *m, uint8_t cell) {
    CHECK(sb_classic_fire(m, m->actor, m->sequence, cell));
    return resolve(m);
}
static void salvo(sb_match_t *m, unsigned p, const uint8_t *cells, size_t count) {
    CHECK(sb_select(m, p, m->sequence, cells, count));
    CHECK(sb_escalated_lock(m, p, m->sequence, true));
}
static void specs(void) {
    for (int r = SB_CLASSIC; r <= SB_ESCALATED; ++r)
        for (int t = SB_SINGLE_PLAYER; t <= SB_SHARED_DEVICE; ++t) {
            sb_spec_t spec;
            bool supported = true;
            CHECK(sb_spec_make(&spec, r, t, 1) == supported);
            if (!supported) continue;
            CHECK(sb_spec_valid(&spec));
            CHECK(spec.rules.ship_count == 5);
            CHECK(!memcmp(spec.rules.lengths, (uint8_t[]){5,4,3,3,2}, 5));
            spec.rules.lengths[4] = 1; CHECK(!sb_spec_valid(&spec));
            CHECK(sb_spec_make(&spec, r, t, 1));
            spec.rules.version++; CHECK(!sb_spec_valid(&spec));
            CHECK(sb_spec_make(&spec, r, t, 1));
            spec.match_ms++; CHECK(!sb_spec_valid(&spec));
        }
    sb_spec_t spec;
    CHECK(!sb_spec_make(&spec, 0, SB_SINGLE_PLAYER, 0));
    CHECK(!sb_spec_make(&spec, SB_CLASSIC, -1, 0));
    CHECK(!sb_spec_make(&spec, SB_CLASSIC, SB_SINGLE_PLAYER, 2));
    uint32_t seed = 42;
    sb_series_t series = {0};
    uint8_t first, next;
    CHECK(sb_series_starter(&series, SB_CLASSIC, false, rng, &seed, &first));
    CHECK(sb_series_starter(&series, SB_CLASSIC, true, NULL, NULL, &next));
    CHECK(first != next);
    CHECK(sb_series_starter(&series, SB_CLASSIC, true, NULL, NULL, &next));
    CHECK(first == next);
    CHECK(sb_series_starter(&series, SB_ESCALATED, true, NULL, NULL, &next));
    CHECK(!sb_series_starter(&series, SB_CLASSIC, true, NULL, NULL, &next));
    CHECK(sb_series_starter(&series, SB_CLASSIC, true, rng, &seed, &next));
    CHECK(!sb_series_starter(&series, SB_CLASSIC, false, NULL, NULL, &next));
}
static void placement(void) {
    uint8_t cell;
    CHECK(sb_cell(9, 9, &cell) && cell == 99);
    CHECK(!sb_cell(10, 0, &cell)); CHECK(!sb_cell(0, 10, &cell));
    sb_fleet_t f = {0};
    CHECK(!sb_fleet_valid(&f, true));
    CHECK(sb_place(&f, 0, 5, 0, false));
    sb_fleet_t before = f;
    CHECK(!sb_place(&f, 1, 6, 0, true)); CHECK(!memcmp(&f, &before, sizeof(f)));
    CHECK(!sb_place(&f, 0, 6, 0, false));
    CHECK(!sb_place(&f, 0, 0, 6, true));
    CHECK(sb_place(&f, 0, 9, 5, true)); // rotation / relocation
    CHECK(sb_place(&f, 1, 8, 5, true)); // edge touching
    CHECK(sb_place(&f, 2, 5, 4, false)); // diagonal touching
    CHECK(sb_place(&f, 3, 5, 3, false));
    CHECK(sb_place(&f, 4, 0, 0, false));
    CHECK(sb_fleet_valid(&f, true) && sb_alive(&f) == 5 && sb_intact(&f) == 17);
    f.locked = true;
    sb_completion_t completion;
    CHECK(!sb_place(&f, 0, 0, 0, false));
    CHECK(!sb_complete(&f, 10, NULL, NULL, &completion));
    f = (sb_fleet_t){0};
    CHECK(sb_place(&f, 2, 3, 3, true));
    sb_ship_t retained = f.ships[2];
    CHECK(sb_complete(&f, 10000, NULL, NULL, &completion));
    CHECK(completion == SB_COMPLETED && !memcmp(&retained, &f.ships[2], sizeof(retained)));
    CHECK(sb_complete(&f, 0, NULL, NULL, &completion) && completion == SB_COMPLETED);
    f = (sb_fleet_t){0};
    CHECK(sb_place(&f, 0, 9, 0, true));
    CHECK(sb_complete(&f, 0, NULL, NULL, &completion));
    CHECK(completion == SB_REPLACED && sb_fleet_valid(&f, true));
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        uint32_t state = seed;
        f = (sb_fleet_t){0};
        CHECK(sb_complete(&f, seed % 2 ? 10000 : 1, rng, &state, &completion));
        CHECK(sb_fleet_valid(&f, true) && sb_intact(&f) == 17);
    }
}
static void disclosure(void) {
    sb_knowledge_t k;
    sb_knowledge_init(&k);
    CHECK(k.remaining[3] == 2);
    CHECK(!sb_disclose(&k, 3, (uint8_t[]){0,1,2}));
    for (unsigned i = 0; i < 6; ++i) k.cells[i] = SB_HIT;
    sb_knowledge_t before = k;
    CHECK(!sb_disclose(&k, 3, (uint8_t[]){0,0,1}));
    CHECK(!sb_disclose(&k, 3, (uint8_t[]){0,1,3}));
    CHECK(!sb_disclose(&k, 3, (uint8_t[]){0,1,100}));
    CHECK(!memcmp(&before, &k, sizeof(k)));
    CHECK(sb_disclose(&k, 3, (uint8_t[]){2,0,1}));
    CHECK(k.remaining[3] == 1 && k.cells[3] == SB_HIT && k.cells[10] == SB_UNKNOWN);
    CHECK(!sb_disclose(&k, 3, (uint8_t[]){0,1,2}));
    CHECK(sb_disclose(&k, 3, (uint8_t[]){3,4,5}));
    CHECK(k.remaining[3] == 0);
    k.cells[8] = k.cells[9] = k.cells[10] = SB_HIT;
    k.remaining[3] = 1;
    CHECK(!sb_disclose(&k, 3, (uint8_t[]){8,9,10}));
}
static void classic_rules(void) {
    sb_match_t m = deployed(SB_CLASSIC, SB_SHARED_DEVICE);
    CHECK(sb_match_tick(&m, UINT64_C(1000000000), NULL, NULL));
    CHECK(m.phase == SB_TARGETING && !m.clock_expired);
    CHECK(sb_allowance(&m, 0) == 1 && sb_allowance(&m, 1) == 0);
    sb_match_t before = m;
    CHECK(!sb_classic_fire(&m, 1, 1, 0));
    CHECK(!sb_classic_fire(&m, 0, 0, 0));
    CHECK(!sb_classic_fire(&m, 0, 1, 100));
    CHECK(!sb_select(&m, 0, 1, NULL, 0));
    CHECK(!sb_escalated_lock(&m, 0, 1, true));
    CHECK(!memcmp(&before, &m, sizeof(m)));
    // Preview/cancel never locks, damages or consumes a turn.
    CHECK(sb_select(&m, 0, 1, (uint8_t[]){0}, 1));
    CHECK(m.sequence == 1 && m.phase == SB_TARGETING && sb_intact(&m.fleets[1]) == 17);
    CHECK(sb_classic_fire(&m, 0, 1, 0));
    CHECK(!sb_classic_fire(&m, 0, 1, 1));
    sb_record_t record = resolve(&m);
    CHECK(record.count == 1 && record.events[0].hit && record.events[0].length == 0);
    CHECK(sb_intact(&m.fleets[1]) == 16 && sb_alive(&m.fleets[1]) == 5);
    CHECK(!sb_classic_fire(&m, 1, 1, 1));
    CHECK(!sb_match_advance(&m, 0));
    CHECK(sb_match_advance(&m, 1) && m.actor == 1 && m.sequence == 2);
    classic(&m, 99);
    CHECK(sb_match_advance(&m, 2) && m.actor == 0);
    CHECK(!sb_classic_fire(&m, 0, 1, 1));
    CHECK(!sb_classic_fire(&m, 0, 3, 0));
    CHECK(record.events[0].hit && record.sequence == 1); // copied history stable
    unsigned miss = 98;
    for (unsigned s = 0; s < SB_SHIPS; ++s) {
        for (unsigned i = s == 0 ? 1 : 0; i < m.spec.rules.lengths[s]; ++i) {
            sb_record_t r = classic(&m, (uint8_t)(s * 10 + i));
            bool sunk = i + 1 == m.spec.rules.lengths[s];
            CHECK(r.count == (sunk ? 2 : 1));
            if (sunk) CHECK(r.events[1].length == m.spec.rules.lengths[s]);
            if (m.phase == SB_FINISHED) break;
            CHECK(sb_match_advance(&m, m.sequence) && m.actor == 1);
            CHECK(sb_allowance(&m, 1) == 1); // independent of losses
            classic(&m, (uint8_t)miss--);
            CHECK(sb_match_advance(&m, m.sequence));
        }
    }
    CHECK(m.outcome == SB_WIN_A && m.phase == SB_FINISHED);
    before = m;
    CHECK(!sb_match_advance(&m, m.sequence)); CHECK(!sb_classic_fire(&m, 1, m.sequence, 0));
    CHECK(!sb_match_surrender(&m, 0)); CHECK(!sb_match_abandon(&m));
    CHECK(!memcmp(&before, &m, sizeof(m)));
}
static void escalated_rules(void) {
    sb_match_t m = deployed(SB_ESCALATED, SB_SINGLE_PLAYER);
    CHECK(sb_allowance(&m, 0) == 5 && sb_allowance(&m, 1) == 5);
    sb_match_t before = m;
    CHECK(!sb_select(&m, 0, 1, (uint8_t[]){0,0}, 2));
    CHECK(!sb_select(&m, 0, 1, (uint8_t[]){100}, 1));
    CHECK(!sb_select(&m, 0, 1, (uint8_t[]){0,1,2,3,4,5}, 6));
    CHECK(!sb_escalated_lock(&m, 0, 1, false));
    CHECK(!sb_classic_fire(&m, 0, 1, 0));
    CHECK(!memcmp(&before, &m, sizeof(m)));
    salvo(&m, 0, (uint8_t[]){0,1,2,3,4}, 5);
    CHECK(!sb_select(&m, 0, 1, (uint8_t[]){99}, 1));
    CHECK(!sb_escalated_lock(&m, 0, 1, true));
    CHECK(sb_allowance(&m, 1) == 5);
    salvo(&m, 1, (uint8_t[]){10,11,12,13,20}, 5);
    sb_record_t r = resolve(&m);
    CHECK(r.count == 12); // ten attacks and two independent sink disclosures
    CHECK(sb_alive(&m.fleets[0]) == 4 && sb_alive(&m.fleets[1]) == 4);
    CHECK(sb_intact(&m.fleets[0]) == 12 && sb_intact(&m.fleets[1]) == 12);
    CHECK(sb_match_advance(&m, 1));
    CHECK(sb_allowance(&m, 0) == 4 && sb_allowance(&m, 1) == 4);
    CHECK(!sb_select(&m, 0, 2, (uint8_t[]){0}, 1));
    CHECK(sb_select(&m, 0, 2, NULL, 0));
    CHECK(!sb_escalated_lock(&m, 0, 2, false));
    CHECK(sb_escalated_lock(&m, 0, 2, true));
    salvo(&m, 1, NULL, 0);
    r = resolve(&m); CHECK(r.count == 0 && r.outcome == SB_ONGOING);
    CHECK(sb_match_advance(&m, 2));
    // Artificial public-knowledge boundary: excess slots require no confirmation.
    for (unsigned c = 0; c < SB_CELLS; ++c) m.knowledge[0].cells[c] = SB_MISS;
    m.knowledge[0].cells[99] = SB_UNKNOWN;
    CHECK(sb_allowance(&m, 0) == 1);
    CHECK(sb_select(&m, 0, 3, (uint8_t[]){99}, 1));
    CHECK(sb_escalated_lock(&m, 0, 3, false));
}
/* Prepare a valid endgame fixture with one intact segment in each distinct ship. */
static sb_match_t near_end(void) {
    sb_match_t m = deployed(SB_ESCALATED, SB_SINGLE_PLAYER);
    for (unsigned p = 0; p < 2; ++p)
        for (unsigned s = 0; s < SB_SHIPS; ++s)
            for (unsigned i = 0; i + 1 < m.spec.rules.lengths[s]; ++i) {
                unsigned cell = s * 10 + i;
                m.fleets[1-p].hit[cell] = true;
                m.knowledge[p].cells[cell] = SB_HIT;
            }
    return m;
}
static void simultaneous(void) {
    sb_match_t m = near_end();
    const uint8_t final[] = {4,13,22,32,41};
    salvo(&m, 0, final, 5); salvo(&m, 1, final, 5);
    sb_record_t r = resolve(&m);
    CHECK(r.count == SB_EVENTS && r.outcome == SB_DRAW && m.outcome == SB_DRAW);
    CHECK(sb_intact(&m.fleets[0]) == 0 && sb_intact(&m.fleets[1]) == 0);
    CHECK(m.knowledge[0].remaining[3] == 0 && m.knowledge[1].remaining[3] == 0);
    CHECK(!sb_match_advance(&m, 1));
    // Swapping attacker roles/order yields exactly the swapped fleet state.
    sb_match_t a = near_end(), b = a;
    salvo(&a, 0, final, 5); salvo(&a, 1, (uint8_t[]){4,13,22}, 3);
    salvo(&b, 1, final, 5); salvo(&b, 0, (uint8_t[]){4,13,22}, 3);
    resolve(&a); resolve(&b);
    CHECK(a.outcome == SB_WIN_A && b.outcome == SB_WIN_B);
    CHECK(!memcmp(&a.fleets[0], &b.fleets[1], sizeof(sb_fleet_t)));
    CHECK(!memcmp(&a.fleets[1], &b.fleets[0], sizeof(sb_fleet_t)));
}
static void clocks(void) {
    sb_spec_t spec;
    sb_match_t m;
    CHECK(sb_spec_make(&spec, SB_CLASSIC, SB_SINGLE_PLAYER, 1));
    CHECK(sb_match_init(&m, &spec, 0));
    spec.starter = 0; CHECK(m.spec.starter == 1); // specification copied
    CHECK(sb_match_tick(&m, UINT64_C(1000000000), NULL, NULL));
    CHECK(m.phase == SB_DEPLOYMENT && !m.ready[0]);
    CHECK(!sb_match_ready(&m, 0));
    CHECK(sb_spec_make(&spec, SB_ESCALATED, SB_SINGLE_PLAYER, 0));
    CHECK(sb_match_init(&m, &spec, 100));
    CHECK(sb_match_place(&m, 0, 0, 9, 0, true));
    sb_ship_t retained = m.fleets[0].ships[0];
    CHECK(sb_match_tick(&m, 60099, NULL, NULL) && m.phase == SB_DEPLOYMENT);
    CHECK(sb_match_tick(&m, 60100, NULL, NULL) && m.phase == SB_TARGETING);
    CHECK(!memcmp(&retained, &m.fleets[0].ships[0], sizeof(retained)));
    CHECK(m.combat_elapsed_ms == 0 && m.fleets[0].locked);
    CHECK(!sb_match_place(&m, 0, 0, 0, 0, false));
    CHECK(sb_match_pause(&m, true, 60100));
    CHECK(sb_match_tick(&m, 1060100, NULL, NULL));
    CHECK(m.combat_elapsed_ms == 0);
    CHECK(!sb_select(&m, 0, 1, (uint8_t[]){0}, 1));
    CHECK(sb_match_pause(&m, false, 1060100));
    CHECK(sb_match_tick(&m, 1660099, NULL, NULL) && m.phase == SB_TARGETING);
    CHECK(sb_match_tick(&m, 1660100, NULL, NULL) && m.phase == SB_LOCKED);
    sb_record_t r = resolve(&m);
    CHECK(r.outcome == SB_DRAW && m.phase == SB_FINISHED);
    CHECK(!sb_match_tick(&m, 0, NULL, NULL));
    m = deployed(SB_ESCALATED, SB_VERSUS);
    CHECK(!sb_match_pause(&m, true, 100));
    CHECK(sb_select(&m, 0, 1, (uint8_t[]){0}, 1));
    CHECK(sb_match_tick(&m, 30099, NULL, NULL) && m.phase == SB_TARGETING);
    CHECK(sb_match_tick(&m, 30100, NULL, NULL) && m.phase == SB_LOCKED);
    r = resolve(&m); CHECK(r.count == 1 && r.outcome == SB_ONGOING);
    CHECK(sb_match_advance(&m, 1));
    CHECK(sb_match_tick(&m, 60099, NULL, NULL) && m.phase == SB_TARGETING);
    m = deployed(SB_ESCALATED, SB_SINGLE_PLAYER);
    CHECK(sb_select(&m, 0, 1, (uint8_t[]){0}, 1));
    CHECK(sb_match_tick(&m, 600100, NULL, NULL));
    r = resolve(&m); CHECK(r.outcome == SB_WIN_A); // intact segments tie-break
    m = deployed(SB_ESCALATED, SB_SINGLE_PLAYER);
    salvo(&m, 0, (uint8_t[]){40,41}, 2); // fewer segments damaged, but a ship sunk
    salvo(&m, 1, (uint8_t[]){0,1,2,3}, 4);
    CHECK(sb_match_tick(&m, 600100, NULL, NULL));
    r = resolve(&m); CHECK(r.outcome == SB_WIN_A); // ships take precedence
    m = deployed(SB_ESCALATED, SB_SINGLE_PLAYER);
    salvo(&m, 0, (uint8_t[]){0}, 1); salvo(&m, 1, NULL, 0);
    r = resolve(&m); sb_record_t saved = r;
    CHECK(sb_match_tick(&m, 600100, NULL, NULL)); // animation expiry: no second damage
    CHECK(m.phase == SB_FINISHED && m.outcome == SB_WIN_A && sb_intact(&m.fleets[1]) == 16);
    CHECK(!memcmp(&saved, &m.record, sizeof(saved)));
    CHECK(!sb_match_advance(&m, 1));
    m = near_end();
    salvo(&m, 0, (uint8_t[]){4,13,22,32,41}, 5);
    salvo(&m, 1, (uint8_t[]){4,13,22,32,41}, 5);
    CHECK(sb_match_tick(&m, 600100, NULL, NULL));
    CHECK(resolve(&m).outcome == SB_DRAW); // destruction before clock score
    m = deployed(SB_CLASSIC, SB_SINGLE_PLAYER);
    CHECK(sb_match_pause(&m, true, 100));
    CHECK(!sb_classic_fire(&m, 0, 1, 0));
    CHECK(sb_match_pause(&m, false, 10000000));
    CHECK(sb_match_surrender(&m, 0) && m.outcome == SB_WIN_B);
    m = deployed(SB_ESCALATED, SB_SINGLE_PLAYER);
    CHECK(sb_match_abandon(&m) && m.outcome == SB_ABANDONED);
}
static void complete_matches(void) {
    // Sweep both boards in seeded random order. Check public/private consistency
    // after every resolution and a hard upper bound on match progress.
    for (unsigned rules = SB_CLASSIC; rules <= SB_ESCALATED; ++rules) {
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            uint32_t state = seed;
            sb_spec_t spec;
            sb_match_t m;
            CHECK(sb_spec_make(&spec, rules, SB_SINGLE_PLAYER, seed % 2));
            CHECK(sb_match_init(&m, &spec, 0));
            for (unsigned p = 0; p < 2; ++p) {
                sb_completion_t completion;
                CHECK(sb_complete(&m.fleets[p], SB_PLACEMENT_BUDGET, rng, &state, &completion));
                CHECK(sb_match_ready(&m, p));
            }
            unsigned resolved = 0;
            while (m.phase != SB_FINISHED) {
                CHECK(++resolved <= 200);
                for (unsigned p = 0; p < 2; ++p) {
                    if (rules == SB_CLASSIC && p != m.actor) continue;
                    uint8_t candidates[SB_CELLS];
                    unsigned count = 0;
                    for (unsigned c = 0; c < SB_CELLS; ++c)
                        if (m.knowledge[p].cells[c] == SB_UNKNOWN) candidates[count++] = (uint8_t)c;
                    for (unsigned i = count; i > 1; --i) {
                        unsigned j = rng(&state) % i;
                        uint8_t swap = candidates[i-1]; candidates[i-1] = candidates[j]; candidates[j] = swap;
                    }
                    unsigned allowance = sb_allowance(&m, p);
                    CHECK(allowance > 0 && allowance <= count);
                    if (rules == SB_CLASSIC) CHECK(sb_classic_fire(&m, p, m.sequence, candidates[0]));
                    else salvo(&m, p, candidates, allowance);
                }
                sb_record_t record = resolve(&m);
                CHECK(record.count <= SB_EVENTS);
                for (unsigned p = 0; p < 2; ++p) {
                    unsigned alive = 0, intact = 0;
                    uint8_t remaining[SB_SEGMENTS+1] = {0};
                    for (unsigned ship = 0; ship < SB_SHIPS; ++ship) {
                        unsigned hp = 0, length = m.spec.rules.lengths[ship];
                        for (unsigned i = 0; i < length; ++i) {
                            unsigned c = m.fleets[1-p].ships[ship].cells[i];
                            hp += !m.fleets[1-p].hit[c];
                            CHECK((m.knowledge[p].cells[c] == SB_HIT || m.knowledge[p].cells[c] == SB_SUNK) == m.fleets[1-p].hit[c]);
                        }
                        intact += hp; alive += hp > 0;
                        remaining[length] += hp > 0;
                    }
                    CHECK(sb_alive(&m.fleets[1-p]) == alive && sb_intact(&m.fleets[1-p]) == intact);
                    CHECK(!memcmp(remaining, m.knowledge[p].remaining, sizeof(remaining)));
                }
                if (m.phase != SB_FINISHED) CHECK(sb_match_advance(&m, m.sequence));
            }
            CHECK(m.outcome == SB_WIN_A || m.outcome == SB_WIN_B || (rules == SB_ESCALATED && m.outcome == SB_DRAW));
        }
    }
}
int main(void) {
    specs(); placement(); disclosure(); classic_rules(); escalated_rules(); simultaneous(); clocks(); complete_matches();
    printf("M4: %u assertions passed\n", checks);
    return 0;
}
