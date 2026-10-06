// SPDX-License-Identifier: MIT
#pragma once
#include "render/typography.h"
#include <stdbool.h>
#include <stdint.h>
// Presentation models contain public labels only; no boards or private fleets.
typedef enum { SB_UI_SINGLE_PLAYER, SB_UI_VERSUS, SB_UI_SHARED_DEVICE } sb_ui_play_type_t;
typedef struct {
    sb_ui_play_type_t play_type;
    bool escalated;
    uint32_t turn_or_round;
    unsigned shots_selected, shots_available, seconds_remaining;
    char target_column; unsigned target_row;
} sb_ui_hud_t;
// False means invalid model or insufficient display space; never shrink the selected font.
bool sb_ui_hud(pax_buf_t *buf, sb_font_profile_t profile, const sb_ui_hud_t *hud);
bool sb_ui_handover(pax_buf_t *buf, sb_font_profile_t profile,
                    bool player_b, bool deployment, bool first_press_received);
bool sb_ui_preview(pax_buf_t *buf, sb_font_profile_t profile, unsigned page);
#define SB_UI_PREVIEW_PAGES 7

#include "game/game.h"
// A projected view, never a match or an opponent fleet. Occupancy is permitted
// only on the owner's board. Enemy cells contain disclosed knowledge only.
typedef struct {
    sb_ruleset_t ruleset;
    sb_ui_play_type_t play_type;
    bool own_board, active, player_b;
    bool dual, result;
    unsigned view_mode; // 0=both, 1=fleet, 2=targets
    const char *owner_name, *target_name;
    sb_cell_state_t fleet_cells[SB_CELLS], target_cells[SB_CELLS];
    bool fleet_occupied[SB_CELLS];
    uint32_t sequence;
    unsigned seconds_remaining, selected, available, cursor;
    sb_cell_state_t cells[SB_CELLS];
    bool occupied[SB_CELLS];
    bool targets[SB_CELLS];
    // Deployment ghost; separate from committed occupancy and public targets.
    bool placement[SB_CELLS], placement_valid;
    bool deployment, ships_placed[SB_SHIPS];
    unsigned ship_selected;
    // Optional immutable resolved record; capacity covers both five-shot salvos
    // and all sunk disclosures. M6 draws static impacts, M11 animates them.
    const sb_record_t *effects;
    // Optional live-session labels; NULL retains the validation preview labels.
    const char *sequence_label, *phase_label, *action_label, *detail_label, *footer_label, *notice;
} sb_ui_tactical_t;
bool sb_ui_tactical(pax_buf_t *buf, sb_font_profile_t profile, const sb_ui_tactical_t *view);
#define SB_TACTICAL_PREVIEW_PAGES 7
bool sb_ui_tactical_preview(pax_buf_t *buf, sb_font_profile_t profile,
                            unsigned page, unsigned cursor, bool enemy);

#include "render/menu_view.h"
bool sb_ui_menu(pax_buf_t *buf, sb_font_profile_t profile, const sb_menu_view_t *view);

// Fixed F1..F6 order; disabled keys retain their hardware cue.
bool sb_ui_keybar(pax_buf_t *buf, sb_font_profile_t profile, unsigned enabled);
