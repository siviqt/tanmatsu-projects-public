// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t sb_renderer_init(void);
esp_err_t sb_renderer_loading(const char *stage, unsigned percent);
esp_err_t sb_renderer_ready(const char *config_status);
esp_err_t sb_renderer_input_test(const char *config_status, unsigned x, unsigned y,
                                 unsigned panel, const char *action, unsigned count, bool repeat);
esp_err_t sb_renderer_exit(void);
esp_err_t sb_renderer_message(const char *message);
void sb_renderer_shutdown(void);

// M5 public typography preview; no gameplay actions are dispatched.
void sb_renderer_set_font_profile(const char *name);
esp_err_t sb_renderer_ui_preview(unsigned page);
// M6 rendering harness; cursor is a row-major cell index, no game mutations.
esp_err_t sb_renderer_tactical_preview(unsigned page, unsigned cursor, bool enemy);

#include "validation/performance.h"
#if SB_PERFORMANCE
esp_err_t sb_renderer_benchmark(unsigned scenario, uint64_t elapsed_us, unsigned cursor,
                                sb_frame_sample_t *sample);
#endif

#include "render/ui.h"
esp_err_t sb_renderer_tactical(const sb_ui_tactical_t *view);
esp_err_t sb_renderer_handover(bool player_b, bool deployment, bool first_press);
esp_err_t sb_renderer_menu(const sb_menu_view_t *view);

esp_err_t sb_renderer_handover_match(bool player_b, bool deployment, bool first_press, unsigned starter);
