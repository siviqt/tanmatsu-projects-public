// SPDX-License-Identifier: MIT
#include "platform/sdcard.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

#if !defined(CONFIG_BSP_TARGET_TANMATSU)
#error "SD wiring is validated only for the Tanmatsu target"
#endif
#if defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE) && !defined(SB_M0B)
#error "Shared ESP-Hosted SDMMC ownership requires separate validation before enabling"
#endif

static QueueHandle_t requests, results;
static TaskHandle_t worker;
static bool worker_busy;
// Hardware resources are accessed by the worker only.
static sdmmc_card_t *card;
static sd_pwr_ctrl_handle_t power;
static void *dma;

#ifdef SB_M0B
// ESP-Hosted owns the controller and slot 1 for the entire app lifetime.
extern bool sb_radio_host_ready(void);
static esp_err_t shared_host_init(void) {
    return sb_radio_host_ready() ? ESP_OK : ESP_ERR_INVALID_STATE;
}
static esp_err_t shared_slot_deinit(int slot) {
    if (slot != SDMMC_HOST_SLOT_0) return ESP_ERR_INVALID_ARG;
    // Mount can fail before slot creation. Never tear down radio slot 1.
    esp_err_t err = sdmmc_host_deinit_slot(slot);
    return err == ESP_ERR_INVALID_ARG ? ESP_OK : err;
}
#endif

static esp_err_t mount_card(void) {
    const sd_pwr_ctrl_ldo_config_t ldo = {.ldo_chan_id = 4};
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &power);
    if (err != ESP_OK) return err;
    // Launcher reference: reset slot 0 across AppFS warm launches.
    gpio_config_t pins = {
        .pin_bit_mask = BIT64(39) | BIT64(40) | BIT64(41) | BIT64(42) | BIT64(43) | BIT64(44),
        .mode = GPIO_MODE_OUTPUT_OD,
    };
    err = gpio_config(&pins);
    if (err != ESP_OK) return err;
    for (int pin = 39; pin <= 44; ++pin) {
        err = gpio_set_level(pin, 0);
        if (err != ESP_OK) return err;
    }
    err = sd_pwr_ctrl_set_io_voltage(power, 0);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));
    pins.mode = GPIO_MODE_INPUT;
    err = gpio_config(&pins);
    if (err != ESP_OK) return err;
    err = sd_pwr_ctrl_set_io_voltage(power, 3300);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));
    dma = heap_caps_aligned_alloc(64, 2048, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!dma) return ESP_ERR_NO_MEM;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
#ifdef SB_M0B
    host.init = shared_host_init;
    host.flags |= SDMMC_HOST_FLAG_DEINIT_ARG;
    host.deinit_p = shared_slot_deinit;
#endif
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.command_timeout_ms = 1000;
    host.pwr_ctrl_handle = power;
    host.dma_aligned_buffer = dma;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = GPIO_NUM_43;
    slot.cmd = GPIO_NUM_44;
    slot.d0 = GPIO_NUM_39;
    slot.d1 = GPIO_NUM_40;
    slot.d2 = GPIO_NUM_41;
    slot.d3 = GPIO_NUM_42;
    slot.width = 4;
    const esp_vfs_fat_sdmmc_mount_config_t config = {
        .format_if_mount_failed = false, .max_files = 4, .allocation_unit_size = 16384,
    };
    err = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot, &config, &card);
    if (err == ESP_OK) sdmmc_card_print_info(stdout, card);
    return err;
}

static esp_err_t unmount_card(void) {
    if (card) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount("/sd", card);
        // IDF may have freed the card even on a later unregister error. Never retry.
        card = NULL;
        if (err != ESP_OK) return err;
    }
    heap_caps_free(dma);
    dma = NULL;
    if (power) {
        esp_err_t err = sd_pwr_ctrl_del_on_chip_ldo(power);
        if (err != ESP_OK) return err;
        power = NULL;
    }
    return ESP_OK;
}

static void sd_worker(void *unused) {
    (void)unused;
    bool mount;
    while (xQueueReceive(requests, &mount, portMAX_DELAY) == pdTRUE) {
        esp_err_t err = mount ? mount_card() : unmount_card();
        ESP_LOGI("M0a", "SD %s result: %s", mount ? "mount" : "cleanup", esp_err_to_name(err));
        xQueueSend(results, &err, portMAX_DELAY);
    }
    vTaskDelete(NULL);
}

static esp_err_t request(bool mount, unsigned timeout_ms) {
    if (worker_busy) return ESP_ERR_INVALID_STATE;
    worker_busy = true;
    if (xQueueSend(requests, &mount, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t err;
    if (xQueueReceive(results, &err, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE("M0a", "SD worker deadline exceeded; retain resources until restart");
        return ESP_ERR_TIMEOUT;
    }
    worker_busy = false;
    return err;
}

sb_sd_mount_result_t sb_platform_sd_mount(void) {
    if (requests) return (sb_sd_mount_result_t){ESP_ERR_INVALID_STATE, worker_busy};
    requests = xQueueCreate(1, sizeof(bool));
    results = xQueueCreate(1, sizeof(esp_err_t));
    if (!requests || !results || xTaskCreate(sd_worker, "sb_sd", 4096, NULL, 4, &worker) != pdPASS) {
        if (requests) vQueueDelete(requests);
        if (results) vQueueDelete(results);
        requests = results = NULL;
        return (sb_sd_mount_result_t){ESP_ERR_NO_MEM, false};
    }
    esp_err_t err = request(true, 8000);
    return (sb_sd_mount_result_t){err, worker_busy};
}

esp_err_t sb_platform_sd_shutdown(void) {
    if (!requests) return ESP_OK;
    // Do not cancel a driver operation or free resources underneath it.
    if (worker_busy) return ESP_ERR_INVALID_STATE;
    esp_err_t err = request(false, 2000);
    if (!worker_busy) {
        // The reply confirms the driver call returned. Stop the now-idle worker
        // before deleting its queues; a timed-out worker stays alive until reset.
        vTaskDelete(worker);
        worker = NULL;
        vQueueDelete(requests);
        vQueueDelete(results);
        requests = results = NULL;
    }
    return err;
}
