// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SB_BOARD_SIDE 10
#define SB_CELLS 100
#define SB_SHIPS 5
#define SB_SEGMENTS 5
#define SB_SALVO 5
#define SB_EVENTS 20 /* ten attacks plus up to ten separate sunk disclosures */
#define SB_PLACEMENT_BUDGET 10000u

typedef enum { SB_CLASSIC = 1, SB_ESCALATED = 2 } sb_ruleset_t;
typedef enum { SB_SINGLE_PLAYER, SB_VERSUS, SB_SHARED_DEVICE } sb_play_type_t;
typedef enum { SB_DEPLOYMENT, SB_TARGETING, SB_LOCKED, SB_PRESENTING, SB_FINISHED } sb_phase_t;
typedef enum { SB_ONGOING, SB_WIN_A, SB_WIN_B, SB_DRAW, SB_ABANDONED } sb_outcome_t;
typedef enum { SB_UNKNOWN, SB_MISS, SB_HIT, SB_SUNK } sb_cell_state_t;
typedef enum { SB_ATTACK_EVENT, SB_SUNK_EVENT } sb_event_type_t;
typedef uint32_t (*sb_random_fn)(void *context);
typedef struct {
    sb_ruleset_t id;
    uint8_t version, board_side, ship_count, lengths[SB_SHIPS];
} sb_rules_t;
typedef struct {
    sb_rules_t rules;
    sb_play_type_t play_type;
    uint8_t starter;
    uint32_t deployment_ms, selection_ms, match_ms; /* zero explicitly means untimed */
} sb_spec_t;
typedef struct { bool placed; uint8_t cells[SB_SEGMENTS]; } sb_ship_t;
typedef struct { sb_ship_t ships[SB_SHIPS]; bool hit[SB_CELLS], locked; } sb_fleet_t;
typedef struct { sb_cell_state_t cells[SB_CELLS]; uint8_t remaining[SB_SEGMENTS + 1]; } sb_knowledge_t;
typedef struct {
    sb_event_type_t type;
    uint8_t attacker, cell, length, cells[SB_SEGMENTS];
    bool hit;
} sb_event_t;
typedef struct {
    uint32_t sequence;
    uint8_t count;
    sb_event_t events[SB_EVENTS];
    sb_outcome_t outcome;
} sb_record_t;
typedef enum { SB_COMPLETED, SB_REPLACED } sb_completion_t;
/* Storage is caller-owned, with no allocation. Treat fields as read-only; all
 * mutations go through this API. Never pass sb_match_t to an enemy UI or AI. */
typedef struct {
    sb_spec_t spec;
    sb_fleet_t fleets[2];
    sb_knowledge_t knowledge[2]; /* index is the observing/attacking player */
    sb_phase_t phase;
    sb_outcome_t outcome;
    uint32_t sequence;
    uint8_t actor, allowance[2], selection[2][SB_SALVO], selected[2];
    bool ready[2], salvo_locked[2], replaced[2], paused, clock_expired;
    uint64_t now_ms, phase_started_ms, combat_elapsed_ms;
    sb_record_t record;
} sb_match_t;
/* Keep across rematches (independent of network session identity). Reset on a
 * fresh opponent; preparing any non-Classic proposal resets the sequence. */
typedef struct { bool classic; uint8_t last_starter; } sb_series_t;

const sb_rules_t *sb_rules(sb_ruleset_t id);
bool sb_spec_make(sb_spec_t *out, sb_ruleset_t id, sb_play_type_t play_type, uint8_t starter);
bool sb_spec_valid(const sb_spec_t *spec);
bool sb_series_starter(sb_series_t *series, sb_ruleset_t id, bool same_opponent,
                       sb_random_fn random, void *context, uint8_t *starter);
/* Cell indices are row * 10 + column; coordinate conversion rejects overflow. */
bool sb_cell(unsigned x, unsigned y, uint8_t *cell);
bool sb_fleet_valid(const sb_fleet_t *fleet, bool complete);
bool sb_place(sb_fleet_t *fleet, unsigned ship, unsigned x, unsigned y, bool vertical);
unsigned sb_intact(const sb_fleet_t *fleet);
unsigned sb_alive(const sb_fleet_t *fleet);
/* Bounded backtracking preserves partial layouts when possible. Budget counts
 * candidate placements; exhaustion replaces with a guaranteed legal layout. */
bool sb_complete(sb_fleet_t *fleet, unsigned budget, sb_random_fn random,
                 void *context, sb_completion_t *result);
void sb_knowledge_init(sb_knowledge_t *knowledge);
/* Public-only validation, atomic on failure; no adjacent cells are inferred. */
bool sb_disclose(sb_knowledge_t *knowledge, unsigned length, const uint8_t *cells);

bool sb_match_init(sb_match_t *match, const sb_spec_t *spec, uint64_t now_ms);
bool sb_match_place(sb_match_t *match, unsigned player, unsigned ship,
                    unsigned x, unsigned y, bool vertical);
bool sb_match_ready(sb_match_t *match, unsigned player);
unsigned sb_allowance(const sb_match_t *match, unsigned player);
/* Tick with monotonic milliseconds before processing input. Timed expiry locks
 * staged selections. Resolution remains explicit, once, even on expiry. */
bool sb_match_tick(sb_match_t *match, uint64_t now_ms, sb_random_fn random, void *context);
bool sb_match_pause(sb_match_t *match, bool paused, uint64_t now_ms);
bool sb_select(sb_match_t *match, unsigned player, uint32_t sequence,
               const uint8_t *cells, size_t count);
bool sb_classic_fire(sb_match_t *match, unsigned player, uint32_t sequence, uint8_t cell);
bool sb_escalated_lock(sb_match_t *match, unsigned player, uint32_t sequence, bool forfeit_confirmed);
bool sb_match_resolve(sb_match_t *match, uint32_t sequence, sb_record_t *record);
/* Presentation/network barriers are owned by the caller. Terminal matches
 * cannot advance. Previously copied records never change. */
bool sb_match_advance(sb_match_t *match, uint32_t sequence);
bool sb_match_surrender(sb_match_t *match, unsigned player);
bool sb_match_abandon(sb_match_t *match);
