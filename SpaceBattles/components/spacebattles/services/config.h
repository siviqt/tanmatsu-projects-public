// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define SB_CONFIG_TEXT_MAX 4096
#define SB_CONFIG_VERSION 1
// Strings contain canonical INI values; callers change settings through set().
typedef struct {
    unsigned keyboard_brightness, led_brightness;
    bool keyboard_enabled, leds_enabled;
    unsigned brightness, master_volume, effects_volume, music_volume;
    bool reduced_animations, power_saving, targeting_confirmation, sound_enabled;
    char font_size[8], animation_quality[8], mode[8], ai_difficulty[8];
    char callsign[17], region[8], channel[16];
    char preferred_ruleset[10];
} sb_settings_t;
typedef struct { sb_settings_t settings; bool dirty; } sb_config_t;
typedef enum { SB_CONFIG_VALID, SB_CONFIG_INVALID, SB_CONFIG_FUTURE } sb_config_result_t;
void sb_config_defaults(sb_config_t *config);
// Unknown/malformed individual values retain defaults; an invalid version or
// structurally unreadable file is rejected. Future versions must be preserved.
sb_config_result_t sb_config_parse(sb_config_t *config, const char *text);
bool sb_config_set(sb_config_t *config, const char *section, const char *key, const char *value);
void sb_config_reset(sb_config_t *config);
size_t sb_config_serialize(const sb_config_t *config, char *text, size_t capacity);
