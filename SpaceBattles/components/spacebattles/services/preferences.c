// SPDX-License-Identifier: MIT
#include "services/preferences.h"
#include <string.h>
unsigned sb_audio_gain(const sb_settings_t *s, bool music) {
    return !s || !s->sound_enabled ? 0 : s->master_volume*(music ? s->music_volume : s->effects_volume)/100;
}
unsigned sb_frame_interval_ms(const sb_settings_t *s) {
    return s && (s->power_saving || !strcmp(s->mode,"30")) ? 34 : 17;
}
unsigned sb_animation_level(const sb_settings_t *s) {
    return s && !strcmp(s->animation_quality,"off") ? 0 :
        s && (s->reduced_animations || !strcmp(s->animation_quality,"reduced")) ? 1 : 2;
}
