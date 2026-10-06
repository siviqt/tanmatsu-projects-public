// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#define SB_MENU_ROWS 8
// Presentation contains public text only; never private match state.
typedef struct {
    char title[64], subtitle[96], rows[SB_MENU_ROWS][96];
    char status[96], footer[96];
    unsigned count, selected;
    bool selectable;
    unsigned key_mask;
    bool edit_field, select_all;
    char edit_text[17];
    unsigned caret;
} sb_menu_view_t;
