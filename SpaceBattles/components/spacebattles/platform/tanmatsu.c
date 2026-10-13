// SPDX-License-Identifier: MIT
#include "platform/tanmatsu.h"
#include "validation/performance.h"
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/tanmatsu.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t transfer_done;
static sb_display_t parameters;
static bool transfer_uncertain;

static bool IRAM_ATTR transfer_complete(esp_lcd_panel_handle_t handle,
                                       esp_lcd_dpi_panel_event_data_t *event, void *context) {
    (void)handle;
    (void)event;
#if defined(SB_FAULT_transfer)
    // Exercise the real transfer but suppress acknowledgment to the application.
    (void)context;
    return false;
#endif
    if (!xPortInIsrContext()) {
        xSemaphoreGive((SemaphoreHandle_t)context);
        return false;
    }
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &wake);
    return wake == pdTRUE;
}

esp_err_t sb_platform_init(void) {
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGD("M0a", "IDF=%s chip_revision=%u fault=%s", esp_get_idf_version(),
             chip.revision, SB_VALIDATION_NAME);
    ESP_LOGI("M0a", "app=%s version=%s", esp_app_get_description()->project_name,
             esp_app_get_description()->version);
    const bsp_configuration_t config = {
        .display = {.requested_color_format = SB_COLOR_BITS == 24 ? BSP_DISPLAY_COLOR_FORMAT_24_888RGB : BSP_DISPLAY_COLOR_FORMAT_16_565RGB, .num_fbs = 1},
    };
    // BSP owns GPIO ISR and subsystem setup. Any partial error remains fatal.
    esp_err_t err = bsp_device_initialize(&config);
    if (err != ESP_OK) ESP_LOGE("Platform", "BSP initialization failed: %s", esp_err_to_name(err));
    if (err == ESP_OK) {
        tanmatsu_coprocessor_handle_t coprocessor;
        uint16_t version;
        esp_err_t version_err = bsp_tanmatsu_coprocessor_get_handle(&coprocessor);
        if (version_err == ESP_OK)
            version_err = tanmatsu_coprocessor_get_firmware_version(coprocessor, &version);
        if (version_err == ESP_OK) ESP_LOGD("M0a", "coprocessor firmware=%u", version);
        else ESP_LOGW("M0a", "coprocessor version unavailable: %s", esp_err_to_name(version_err));
    }
    return err;
}

esp_err_t sb_platform_display_init(sb_display_t *display) {
    bsp_display_color_format_t format;
    bsp_display_endianness_t endian;
    esp_err_t err = bsp_display_get_parameters(&parameters.width, &parameters.height, &format, &endian);
    if (err != ESP_OK) return err;
    if (format != (SB_COLOR_BITS == 24 ? BSP_DISPLAY_COLOR_FORMAT_24_888RGB : BSP_DISPLAY_COLOR_FORMAT_16_565RGB)) return ESP_ERR_NOT_SUPPORTED;
    parameters.big_endian = endian == BSP_DISPLAY_ENDIAN_BIG;
    switch (bsp_display_get_default_rotation()) {
        case BSP_DISPLAY_ROTATION_0: parameters.rotation = 0; break;
        case BSP_DISPLAY_ROTATION_90: parameters.rotation = 90; break;
        case BSP_DISPLAY_ROTATION_180: parameters.rotation = 180; break;
        case BSP_DISPLAY_ROTATION_270: parameters.rotation = 270; break;
        default: return ESP_ERR_NOT_SUPPORTED;
    }
    err = bsp_display_get_panel(&panel);
    if (err != ESP_OK) return err;
    if (!panel) return ESP_ERR_INVALID_STATE;
    transfer_done = xSemaphoreCreateBinary();
    if (!transfer_done) return ESP_ERR_NO_MEM;
    // BSP 1.4.0 blit has no public completion wait. Own the public IDF callback
    // on the BSP-provided panel; never mix this path with bsp_display_blit().
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_color_trans_done = transfer_complete};
    err = esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, transfer_done);
    if (err != ESP_OK) {
        vSemaphoreDelete(transfer_done);
        transfer_done = NULL;
        return err;
    }
    *display = parameters;
    return ESP_OK;
}

esp_err_t sb_platform_present(const void *pixels) {
    if (!panel || !transfer_done || transfer_uncertain) return ESP_ERR_INVALID_STATE;
    transfer_uncertain = true;
    int64_t start = esp_timer_get_time();
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, parameters.width, parameters.height, pixels);
    if (err != ESP_OK) return err;
    if (xSemaphoreTake(transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE("M0a", "display timeout; frame retained until restart");
        return ESP_ERR_TIMEOUT;
    }
    transfer_uncertain = false;
    if (!SB_PERFORMANCE) ESP_LOGD("M0a", "display completion confirmed in %lld us", (long long)(esp_timer_get_time() - start));
    return ESP_OK;
}

bool sb_platform_can_release_frame(void) {
    return !transfer_uncertain;
}

void sb_platform_exit(void) {
    // Keep the registered callback and semaphore alive until the BSP restart.
    ESP_LOGI("SpaceBattles", "Returning to launcher");
    bsp_device_restart_to_launcher();
    // Fail closed if a future BSP unexpectedly returns; no restart loop.
    for (;;) vTaskDelay(portMAX_DELAY);
}
