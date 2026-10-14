// SPDX-License-Identifier: MIT
#include <stdint.h>
#include "platform/tanmatsu.h"
#include "bsp/device.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "soc/hp_system_reg.h"
#include "soc/soc.h"
#endif

void sb_platform_exit(void) {
#if CONFIG_IDF_TARGET_ESP32P4
    /* IDF 6.0.2 powers down ECC RAM after P-256 pairing. Its CPU restart
     * clears FORCE_PD but leaves MEM_PD set and FORCE_PU cleared. The IDF
     * 5.5.2 launcher has no-op ECC power-up/down helpers, so TLS signature
     * verification inherits unpowered RAM. Restore the power-on bits here,
     * after the owner task has finished all synchronous crypto operations.
     * Keep unrelated register fields and the normal BSP restart intact. */
    uint32_t before = REG_READ(HP_SYSTEM_ECC_PD_CTRL_REG);
    REG_CLR_BIT(HP_SYSTEM_ECC_PD_CTRL_REG,
                HP_SYSTEM_ECC_MEM_PD | HP_SYSTEM_ECC_MEM_FORCE_PD);
    REG_SET_BIT(HP_SYSTEM_ECC_PD_CTRL_REG, HP_SYSTEM_ECC_MEM_FORCE_PU);
    uint32_t after = REG_READ(HP_SYSTEM_ECC_PD_CTRL_REG);
    if (before != after) {
        ESP_LOGI("Platform", "Launcher ECC RAM power: 0x%lx -> 0x%lx",
                 (unsigned long)before, (unsigned long)after);
    }
#endif
    // Keep live display/transport callbacks and their storage until BSP restart.
    ESP_LOGI("SpaceBattles", "Returning to launcher");
    bsp_device_restart_to_launcher();
    // Fail closed if a future BSP unexpectedly returns; no restart loop.
    for (;;) vTaskDelay(portMAX_DELAY);
}
