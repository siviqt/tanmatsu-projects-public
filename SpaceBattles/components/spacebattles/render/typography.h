// SPDX-License-Identifier: MIT
#pragma once
#include "pax_gfx.h"
typedef enum { SB_FONT_SMALL, SB_FONT_MEDIUM, SB_FONT_LARGE, SB_FONT_PROFILE_COUNT } sb_font_profile_t;
typedef enum {
    SB_FONT_CAPTION, SB_FONT_BODY, SB_FONT_BUTTON, SB_FONT_HEADING,
    SB_FONT_TIMER, SB_FONT_COORDINATE, SB_FONT_TELEMETRY, SB_FONT_ROLE_COUNT
} sb_font_role_t;
typedef struct { const pax_font_t *font; float size; } sb_font_t;
extern const sb_font_t sb_font_assets[SB_FONT_PROFILE_COUNT][SB_FONT_ROLE_COUNT];
sb_font_profile_t sb_font_profile_parse(const char *name);
const char *sb_font_profile_name(sb_font_profile_t profile);
sb_font_t sb_font_get(sb_font_profile_t profile, sb_font_role_t role);
