// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#define SB_MENU_ROWS 8
typedef enum { SB_MENU_SURFACE_PLAIN, SB_MENU_SURFACE_COMMAND, SB_MENU_SURFACE_SETUP,
    SB_MENU_SURFACE_DIALOG, SB_MENU_SURFACE_STATUS, SB_MENU_SURFACE_VERIFY,
    SB_MENU_SURFACE_RESULT, SB_MENU_SURFACE_MATCH, SB_MENU_SURFACE_RULES } sb_menu_surface_t;
typedef enum { SB_TONE_NORMAL, SB_TONE_PENDING, SB_TONE_SUCCESS, SB_TONE_ERROR } sb_ui_tone_t;
// Presentation contains public text only; never private match state.
typedef struct {
    char title[64], subtitle[96], rows[SB_MENU_ROWS][96];
    char status[96], footer[96];
    unsigned count, selected;
    bool selectable;
    unsigned key_mask, return_keys;
    unsigned disabled_rows; // Visible explanation, but action is unavailable.
    bool edit_field, select_all;
    char edit_text[17];
    unsigned caret;
    sb_menu_surface_t surface;
    sb_ui_tone_t tone;
    unsigned value_rows; // Label: value rows, aligned as two columns when they fit.
    char headline[32]; // Public result or connection state, never a control hint.
} sb_menu_view_t;
