// SPDX-License-Identifier: MIT
#include "platform/tanmatsu.h"
#include "bsp/display.h"
#include "bsp/audio.h"
#include "bsp/input.h"
// App task only. Audio channels will apply per-channel gains when mixed;
// hardware master attenuates both speaker and headphone outputs. Future
// playback must also suppress samples when sound_enabled is false.
esp_err_t sb_platform_apply_settings(const sb_settings_t *s) {
    if (!s) return ESP_ERR_INVALID_ARG;
    esp_err_t display=bsp_display_set_backlight_brightness(s->brightness<10 ? 10 : s->brightness);
    esp_err_t audio=bsp_audio_set_volume(s->sound_enabled ? s->master_volume : 0);
    esp_err_t keyboard=bsp_input_set_backlight_brightness(s->keyboard_enabled ? s->keyboard_brightness : 0);
    esp_err_t leds=sb_platform_indicator_settings(s->leds_enabled,s->led_brightness);
    return display!=ESP_OK ? display : audio!=ESP_OK ? audio : keyboard!=ESP_OK ? keyboard : leds;
}
