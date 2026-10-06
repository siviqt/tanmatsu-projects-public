// SPDX-License-Identifier: MIT
#pragma once
#include "game/game.h"
#include "game/ai.h"
#include "input/input.h"
#include "render/ui.h"

typedef enum {
    SB_LOCAL_START, SB_LOCAL_COVER, SB_LOCAL_DEPLOY, SB_LOCAL_TARGET,
    SB_LOCAL_RESULT, SB_LOCAL_HELP, SB_LOCAL_EXIT,
    SB_LOCAL_SETTINGS, SB_LOCAL_PAUSED, SB_LOCAL_FINISHED
} sb_local_screen_t;
// Application-task owned. Renderers receive only the projected tactical view.
typedef struct {
    sb_match_t match;
    sb_series_t series;
    sb_local_screen_t screen, resume, settings_return, help_return;
    unsigned player, cursor, ship, presses, view_mode, menu_row;
    char names[2][17];
    bool vertical, own, space_down, reset_input;
    uint64_t result_started_ms;
    unsigned result_seconds;
    bool result_started, review_result;
    unsigned result_seen;
    sb_ai_difficulty_t difficulty;
    sb_random_fn random;
    void *random_context;
    unsigned timer_seconds;
    char notice[64];
} sb_local_t;
bool sb_local_begin(sb_local_t *local, bool rematch,
                    sb_random_fn random, void *context);
bool sb_local_begin_single(sb_local_t *local, sb_ruleset_t ruleset,
                           sb_ai_difficulty_t difficulty, bool rematch,
                           sb_random_fn random, void *context, uint64_t now_ms);
// Arm only after the first result frame has completed presentation.
void sb_local_presented(sb_local_t *local, uint64_t now_ms);
// Returns true only when a countdown label or screen changes.
bool sb_local_tick(sb_local_t *local, uint64_t now_ms);
void sb_local_event(sb_local_t *local, sb_input_event_t event);
bool sb_local_open_settings(sb_local_t *local);
void sb_local_close_settings(sb_local_t *local);
// False while covered or in a dialog, with a zeroed output in all such cases.
bool sb_local_project(const sb_local_t *local, sb_ui_tactical_t *view);
// -1 means both indicators off.
int sb_local_indicator(const sb_local_t *local);

bool sb_local_begin_shared(sb_local_t *s, sb_ruleset_t rules, bool rematch, sb_random_fn random, void *context);
