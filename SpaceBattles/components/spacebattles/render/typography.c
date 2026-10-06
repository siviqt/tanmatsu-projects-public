// SPDX-License-Identifier: MIT
#include "render/typography.h"
#include "pax_fonts.h"
#include <string.h>
sb_font_profile_t sb_font_profile_parse(const char *name) {
    if (name && !strcmp(name,"small")) return SB_FONT_SMALL;
    if (name && !strcmp(name,"large")) return SB_FONT_LARGE;
    return SB_FONT_MEDIUM;
}
const char *sb_font_profile_name(sb_font_profile_t profile) {
    static const char *names[] = {"small", "medium", "large"};
    return names[(unsigned)profile < SB_FONT_PROFILE_COUNT ? profile : SB_FONT_MEDIUM];
}
sb_font_t sb_font_get(sb_font_profile_t profile, sb_font_role_t role) {
    if ((unsigned)profile >= SB_FONT_PROFILE_COUNT) profile = SB_FONT_MEDIUM;
    if ((unsigned)role >= SB_FONT_ROLE_COUNT) return (sb_font_t){pax_font_sky_mono,16};
    return sb_font_assets[profile][role];
}
