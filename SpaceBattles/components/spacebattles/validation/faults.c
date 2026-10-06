// SPDX-License-Identifier: MIT
// Link-time test seams only: production never calls private BSP initializers.
// These skip one subsystem inside the real BSP sequence without editing the SDK.
#include "bsp/display.h"
#include "esp_log.h"

#if defined(SB_FAULT_i2c) || defined(SB_FAULT_display) || defined(SB_FAULT_audio)
static esp_err_t injected_failure(void) {
    ESP_LOGW("M0a", "INJECTED BSP failure: %s", SB_VALIDATION_NAME);
    return ESP_FAIL;
}
#endif
#if defined(SB_FAULT_i2c)
esp_err_t __wrap_bsp_i2c_primary_bus_initialize(void) { return injected_failure(); }
#elif defined(SB_FAULT_display)
esp_err_t __wrap_bsp_display_initialize(const bsp_display_configuration_t *config) {
    (void)config;
    return injected_failure();
}
#elif defined(SB_FAULT_audio)
esp_err_t __wrap_bsp_audio_initialize(void) { return injected_failure(); }
#endif
