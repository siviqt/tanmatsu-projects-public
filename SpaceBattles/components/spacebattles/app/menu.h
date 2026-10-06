// SPDX-License-Identifier: MIT
#pragma once
#include "input/input.h"
#include "services/config.h"
#include "game/ai.h"

#include "render/menu_view.h"

typedef enum {
    SB_MENU_SETUP, SB_MENU_MAIN, SB_MENU_RULES, SB_MENU_DIFFICULTY, SB_MENU_VERSUS,
    SB_MENU_NETWORK, SB_MENU_SETTINGS, SB_MENU_CATEGORY, SB_MENU_RESET,
    SB_MENU_SAVED, SB_MENU_CALLSIGN, SB_MENU_HELP, SB_MENU_ABOUT, SB_MENU_STORAGE, SB_MENU_DIAGNOSTICS, SB_MENU_LOGOFF
} sb_menu_page_t;
typedef enum { SB_MENU_STAY, SB_MENU_START, SB_MENU_EXIT, SB_MENU_CLOSE, SB_MENU_SAVE } sb_menu_result_t;
typedef struct {
    sb_menu_page_t page, help_return, settings_return;
    unsigned row, category, help_page, character, help_row, settings_row;
    bool in_match, host, changed, select_all;
    sb_ruleset_t rules;
    sb_ai_difficulty_t difficulty;
    sb_play_type_t play_type;
    char callsign[17], feedback[96];
} sb_menu_t;
void sb_menu_init(sb_menu_t *m, const sb_config_t *c, bool in_match);
void sb_menu_settings(sb_menu_t *m);
void sb_menu_help(sb_menu_t *m, unsigned page);
sb_menu_result_t sb_menu_event(sb_menu_t *m, sb_config_t *c, sb_input_event_t e);
void sb_menu_view(const sb_menu_t *m, const sb_config_t *c, const char *storage, sb_menu_view_t *v);
