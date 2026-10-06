// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct {
    size_t width, height;
    unsigned rotation;
    bool big_endian;
} sb_display_t;

esp_err_t sb_platform_init(void);
esp_err_t sb_platform_display_init(sb_display_t *display);
// Synchronous, bounded transfer. On failure retain pixels until launcher restart.
esp_err_t sb_platform_present(const void *pixels);
bool sb_platform_can_release_frame(void);
void sb_platform_exit(void) __attribute__((noreturn));
// Shared Device: 0=A, 1=B, -1=both off. Does not change global LED brightness.
esp_err_t sb_platform_turn_indicator(int player);

#include "services/config.h"
esp_err_t sb_platform_apply_settings(const sb_settings_t *settings);

esp_err_t sb_platform_indicator_settings(bool enabled, unsigned percent);
