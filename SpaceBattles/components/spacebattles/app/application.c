// SPDX-License-Identifier: MIT
#include "app/application.h"
#include "app/local_run.h"
#include "validation/performance.h"
#include "input/input.h"
#include "platform/tanmatsu.h"
#include "platform/sdcard.h"
#include "render/renderer.h"
#include "render/ui.h"
#include "services/storage.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Yield until the monotonic visibility deadline; round up to avoid a short hold.
static void wait_until(int64_t deadline) {
    while (deadline > esp_timer_get_time()) {
        int64_t remaining_us = deadline - esp_timer_get_time();
        if (remaining_us <= 0) break;
        vTaskDelay(pdMS_TO_TICKS((remaining_us + 999) / 1000) + 1);
    }
}

void spacebattles_run(void) {
    sb_config_t config;
    sb_storage_t storage = {0};
    sb_config_defaults(&config);
    esp_err_t err = sb_platform_init();
    // A partial BSP failure is still fatal, even if an error screen is possible.
    esp_err_t display_err = sb_renderer_init();
    if (err == ESP_OK) err = display_err;
    if (err == ESP_OK) err = sb_renderer_loading("ATTACHING INPUT", 20);
    int64_t splash_visible_until = err == ESP_OK ? esp_timer_get_time() + 3000000 : 0;
    if (err == ESP_OK) err = sb_input_init();
    if (err == ESP_OK) err = sb_renderer_loading("CHECKING SD CARD", 40);
    bool sd_available = false;
    if (err == ESP_OK) {
        sb_sd_mount_result_t sd = sb_platform_sd_mount();
        sd_available = sd.error == ESP_OK;
        // Only a still-running worker is fatal; missing storage is optional.
        if (sd.worker_pending) err = sd.error == ESP_OK ? ESP_FAIL : sd.error;
    }
    if (err == ESP_OK) {
        sb_storage_load(&storage, &config, sd_available);
        ESP_LOGI("M2", "%s", sb_storage_status_text(&storage));
        ESP_LOGI("M2", "Settings: brightness=%u mode=%s master_volume=%u dirty=%d",
                 config.settings.brightness, config.settings.mode,
                 config.settings.master_volume, config.dirty);
    }
    if (err == ESP_OK) {
        sb_renderer_set_font_profile(config.settings.font_size);
        err = sb_renderer_loading("INTERFACE READY", 100);
    }
    if (err == ESP_OK) {
        wait_until(splash_visible_until);
        ESP_LOGI("M1", "startup splash minimum 3000 ms elapsed");
        sb_input_reset();
#if SB_PERFORMANCE
        err = sb_performance_run();
        sb_input_reset();
#endif
#if !SB_LOCAL_GAME
        if (err == ESP_OK) err = sb_renderer_tactical_preview(0,0,false);
#endif
    }
    if (err != ESP_OK) {
        ESP_LOGE("SpaceBattles", "M1 startup failed: %s; display: %s", esp_err_to_name(err),
                 esp_err_to_name(display_err));
        if (display_err == ESP_OK) {
            (void)sb_renderer_message("Initialization failed.\nSee serial log. Returning to launcher...");
        }
        // Startup failures bypass the splash hold and use a separate error window.
        vTaskDelay(pdMS_TO_TICKS(2000));
    } else {
#if SB_LOCAL_GAME
        err = sb_local_run(&config, &storage);
#else
        ESP_LOGI("SpaceBattles", "M6 rendering preview: arrows move, F2/F3 view, F4 font, F6 page, Escape / Backspace logs off");
        unsigned page = 0, cursor = 0;
        bool enemy = false;
        bool exiting = false;
        while (!exiting) {
            sb_input_event_t event;
            unsigned batch = 0;
            int64_t input_start = esp_timer_get_time();
            // Apply queued edges in order, then draw once. Bound work so an
            // active producer cannot starve presentation or lifecycle handling.
            for (unsigned n = 0; n < 32; ++n) {
                if (!sb_input_poll(&event)) break;
                if (event.phase == SB_INPUT_RELEASE) continue;
                if (event.action == SB_ACTION_CANCEL) { exiting = true; break; }
                if (page < 4 && event.action >= SB_ACTION_UP && event.action <= SB_ACTION_RIGHT) {
                    unsigned old = cursor;
                    if (event.action == SB_ACTION_UP && cursor >= 10) cursor -= 10;
                    if (event.action == SB_ACTION_DOWN && cursor < 90) cursor += 10;
                    if (event.action == SB_ACTION_LEFT && cursor % 10) --cursor;
                    if (event.action == SB_ACTION_RIGHT && cursor % 10 < 9) ++cursor;
                    if (cursor != old) ++batch;
                    continue;
                }
                if (event.phase != SB_INPUT_PRESS) continue;
                if (event.action == SB_ACTION_PAUSE) {
                    sb_font_profile_t profile = sb_font_profile_parse(config.settings.font_size);
                    const char *name = sb_font_profile_name((profile+1)%SB_FONT_PROFILE_COUNT);
                    sb_config_set(&config,"display","font_size",name);
                    sb_renderer_set_font_profile(name);
                } else if (event.action == SB_ACTION_CONTEXT) {
                    page = (page+1)%SB_TACTICAL_PREVIEW_PAGES;
                } else if (page < 4 && (event.action == SB_ACTION_OWN_FLEET ||
                           event.action == SB_ACTION_ENEMY_VIEW || event.action == SB_ACTION_NEXT_PANEL)) {
                    enemy = event.action == SB_ACTION_NEXT_PANEL ? !enemy : event.action == SB_ACTION_ENEMY_VIEW;
                } else continue;
                ++batch;
                sb_input_reset();
                break;
            }
            int64_t input_us = esp_timer_get_time() - input_start;
            if (exiting) break;
            if (!batch) {
                vTaskDelay(pdMS_TO_TICKS(10) + 1);
                continue;
            }
            int64_t render_start = esp_timer_get_time();
            err = sb_renderer_tactical_preview(page,cursor,enemy);
            ESP_LOGI("M6", "page=%u font=%s action=%s input_us=%lld render_us=%lld",
                     page, config.settings.font_size, sb_input_action_name(event.action),
                     (long long)input_us, (long long)(esp_timer_get_time() - render_start));
            if (err != ESP_OK) {
                ESP_LOGE("M6", "UI preview display failed: %s", esp_err_to_name(err));
                break;
            }
        }
#endif
        sb_input_reset();
    }
    // A failed display must never prevent bounded cleanup and launcher return.
    int64_t exit_visible_until = 0;
    if (display_err == ESP_OK) {
        esp_err_t exit_err = sb_renderer_exit();
        // Start only after confirmed presentation. Cleanup counts toward the
        // owner's minimum three-second visibility requirement.
        if (exit_err == ESP_OK) exit_visible_until = esp_timer_get_time() + 3000000;
        ESP_LOGI("M1", "exit screen: %s", esp_err_to_name(exit_err));
    }
    if (!sb_storage_save(&storage, &config)) {
        ESP_LOGE("M2", "Configuration save failed; session changes remain unsaved");
    }
    esp_err_t cleanup_err = sb_platform_sd_shutdown();
    ESP_LOGI("M1", "storage shutdown: %s", esp_err_to_name(cleanup_err));
    wait_until(exit_visible_until);
    if (exit_visible_until) ESP_LOGI("M1", "exit screen minimum 3000 ms elapsed");
    sb_renderer_shutdown();
    sb_platform_exit();
}
