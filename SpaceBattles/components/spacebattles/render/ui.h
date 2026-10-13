// SPDX-License-Identifier: MIT
#pragma once
#include "render/typography.h"
#include "render/menu_view.h"
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
typedef struct { uint8_t length, cells[SB_SEGMENTS]; } sb_ui_ship_t;
typedef struct {
    sb_ruleset_t ruleset;
    sb_ui_play_type_t play_type;
    bool own_board, active, player_b, owner_turn;
    bool dual, result;
    bool can_surrender, link_available, deployment_locked;
    unsigned view_mode; // Reserved; combat always renders both boards.
    const char *owner_name, *target_name;
    sb_cell_state_t fleet_cells[SB_CELLS], target_cells[SB_CELLS];
    bool fleet_occupied[SB_CELLS];
    sb_ui_ship_t ships[SB_SHIPS]; // Owner-only geometry; touching ships stay distinct.
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
    sb_ui_tone_t notice_tone;
} sb_ui_tactical_t;
bool sb_ui_tactical(pax_buf_t *buf, sb_font_profile_t profile, const sb_ui_tactical_t *view);
#define SB_TACTICAL_PREVIEW_PAGES 7
bool sb_ui_tactical_preview(pax_buf_t *buf, sb_font_profile_t profile,
                            unsigned page, unsigned cursor, bool enemy);

#include "render/menu_view.h"
// Uses the canonical outcome; abandonment is never presented as defeat.
static inline void sb_ui_result_style(sb_menu_view_t *v,sb_result_t result) {
    v->surface=SB_MENU_SURFACE_RESULT;
    const char *label="Match ended";
    v->tone=SB_TONE_NORMAL;
    if(result==SB_RESULT_VICTORY || result==SB_RESULT_OPPONENT_SURRENDERED || result==SB_RESULT_OPPONENT_LEFT) {
        label="Victory"; v->tone=SB_TONE_SUCCESS;
    } else if(result==SB_RESULT_DEFEAT || result==SB_RESULT_SURRENDERED || result==SB_RESULT_LEFT) {
        label="Defeat"; v->tone=SB_TONE_ERROR;
    } else if(result==SB_RESULT_DRAW) label="Draw";
    else if(result==SB_RESULT_ABORTED) label="Match abandoned";
    unsigned i=0; for(;label[i] && i+1<sizeof(v->headline);i++) v->headline[i]=label[i];
    v->headline[i]=0;
}
bool sb_ui_menu(pax_buf_t *buf, sb_font_profile_t profile, const sb_menu_view_t *view);

// Fixed F1..F6 positions; inactive keys have neither icon nor label.
bool sb_ui_keybar(pax_buf_t *buf, sb_font_profile_t profile, unsigned enabled);

// Shared by rendering and input: a hidden function key has no action.
static inline unsigned sb_ui_tactical_keys(const sb_ui_tactical_t *v) {
    if (!v || v->cursor>=SB_CELLS) return 0;
    if (v->result) return 1u << 3;
    unsigned keys=1u | (v->link_available ? 1u<<4 : 0) | (v->can_surrender ? 1u<<5 : 0);
    if (v->deployment) {
        if(v->deployment_locked) return keys;
        keys |= (1u<<1) | (1u<<2);
        unsigned placed=0;
        for(unsigned i=0;i<SB_SHIPS;i++) placed+=v->ships_placed[i];
        if(placed==SB_SHIPS) keys |= 1u<<3;
    } else if(v->active) {
        if(v->target_cells[v->cursor]==SB_UNKNOWN &&
           (v->targets[v->cursor] || v->selected<v->available || v->ruleset==SB_CLASSIC)) keys |= 1u<<2;
        if(v->ruleset==SB_ESCALATED || v->selected) keys |= 1u<<3;
    }
    return keys;
}

// Borrowed decoded artwork; caller keeps it alive until detached. No allocation.
void sb_ui_set_menu_art(const pax_buf_t *art);

// Caller-owned cache must outlive rendering; prepare once, join, then attach.
// Cache geometry, pixel format, rotation and byte order must match the target.
void sb_ui_prepare_menu_cache(pax_buf_t *cache);
void sb_ui_set_menu_cache(const pax_buf_t *cache);

// Caller guarantees the buffer still contains before, rendered with this font.
// Returns false without drawing if a complete repaint is required.
bool sb_ui_menu_selection(pax_buf_t *buf, sb_font_profile_t profile,
                          const sb_menu_view_t *before, const sb_menu_view_t *view);
