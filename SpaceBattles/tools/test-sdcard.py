#!/usr/bin/env python3
"""Run production SD mount/unmount against an IDF 6 shared-controller model."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CONFIG_BSP_TARGET_TANMATSU 1
#define CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE 1
#define CONFIG_ESP_HOSTED_SDIO_SLOT_1 1
#define CONFIG_ESP_HOSTED_SDIO_RESET_SLAVE_USING_CALLBACK 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_TIMEOUT 0x107
#define SDMMC_HOST_SLOT_0 0
#define SDMMC_HOST_FLAG_DEINIT_ARG 1
#define SDMMC_FREQ_HIGHSPEED 40000
#define GPIO_MODE_OUTPUT_OD 1
#define GPIO_MODE_INPUT 2
#define GPIO_NUM_39 39
#define GPIO_NUM_40 40
#define GPIO_NUM_41 41
#define GPIO_NUM_42 42
#define GPIO_NUM_43 43
#define GPIO_NUM_44 44
#define BIT64(n) (1ULL<<(n))
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
#define pdMS_TO_TICKS(n) (n)
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffff
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef void *sd_pwr_ctrl_handle_t;
typedef struct { int ldo_chan_id; } sd_pwr_ctrl_ldo_config_t;
typedef struct { uint64_t pin_bit_mask; int mode; } gpio_config_t;
typedef struct { int unused; } sdmmc_card_t;
typedef struct {
 int slot, flags, max_freq_khz, command_timeout_ms;
 esp_err_t (*init)(void);
 union { esp_err_t (*deinit)(void); esp_err_t (*deinit_p)(int); };
 void *pwr_ctrl_handle, *dma_aligned_buffer;
} sdmmc_host_t;
typedef struct { int clk,cmd,d0,d1,d2,d3,width; } sdmmc_slot_config_t;
typedef struct { bool format_if_mount_failed; int max_files,allocation_unit_size; } esp_vfs_fat_sdmmc_mount_config_t;
int sdmmc_host_init(void);
int sdmmc_host_deinit(void);
int sdmmc_host_deinit_slot(int);
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){.init=sdmmc_host_init,.deinit=sdmmc_host_deinit})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
int esp_hosted_init(void);
int gpio_config(const gpio_config_t *);
int gpio_set_level(int,int);
int sd_pwr_ctrl_new_on_chip_ldo(const sd_pwr_ctrl_ldo_config_t *,sd_pwr_ctrl_handle_t *);
int sd_pwr_ctrl_set_io_voltage(void *,int);
int sd_pwr_ctrl_del_on_chip_ldo(void *);
void *heap_caps_aligned_alloc(size_t,size_t,int);
void heap_caps_free(void *);
int esp_vfs_fat_sdmmc_mount(const char *,const sdmmc_host_t *,const sdmmc_slot_config_t *,const esp_vfs_fat_sdmmc_mount_config_t *,sdmmc_card_t **);
int esp_vfs_fat_sdcard_unmount(const char *,sdmmc_card_t *);
void sdmmc_card_print_info(FILE *,const sdmmc_card_t *);
void vTaskDelay(unsigned);
void vTaskDelete(void *);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void **);
void *xQueueCreate(unsigned,unsigned);
int xQueueSend(void *,const void *,unsigned);
int xQueueReceive(void *,void *,unsigned);
void vQueueDelete(void *);
'''
harness = r'''
#include <assert.h>
#include <string.h>
#include "platform/sdcard.c"
static bool controller=true, radio_slot=true, sd_slot, missing;
static unsigned duplicate_init, whole_deinit, slot_deinit;
static sdmmc_host_t mounted_host;
static sdmmc_card_t test_card;
// ESP-Hosted constructor has run; LoRa initialization has NOT run.
bool sb_radio_host_ready(void) { return false; }
int esp_hosted_init(void) { assert(controller && radio_slot); return ESP_OK; }
int sdmmc_host_init(void) { ++duplicate_init; return controller ? ESP_ERR_NOT_FOUND : ESP_OK; }
int sdmmc_host_deinit(void) { ++whole_deinit; controller=radio_slot=false; return ESP_OK; }
int sdmmc_host_deinit_slot(int slot) {
 assert(slot==0 && controller && radio_slot); ++slot_deinit;
 if (!sd_slot) return ESP_ERR_INVALID_ARG;
 sd_slot=false; return ESP_OK;
}
int gpio_config(const gpio_config_t *p) { (void)p; return ESP_OK; }
int gpio_set_level(int p,int v) { (void)p; (void)v; return ESP_OK; }
int sd_pwr_ctrl_new_on_chip_ldo(const sd_pwr_ctrl_ldo_config_t *p,void **out) { assert(p->ldo_chan_id==4); *out=malloc(1); return ESP_OK; }
int sd_pwr_ctrl_set_io_voltage(void *p,int v) { assert(p); (void)v; return ESP_OK; }
int sd_pwr_ctrl_del_on_chip_ldo(void *p) { free(p); return ESP_OK; }
void *heap_caps_aligned_alloc(size_t a,size_t n,int caps) { (void)a; (void)caps; return malloc(n); }
void heap_caps_free(void *p) { free(p); }
void vTaskDelay(unsigned n) { (void)n; }
void sdmmc_card_print_info(FILE *f,const sdmmc_card_t *c) { (void)f; (void)c; }
static int release(const sdmmc_host_t *h) {
 return h->flags & SDMMC_HOST_FLAG_DEINIT_ARG ? h->deinit_p(h->slot) : h->deinit();
}
int esp_vfs_fat_sdmmc_mount(const char *path,const sdmmc_host_t *h,const sdmmc_slot_config_t *slot,const esp_vfs_fat_sdmmc_mount_config_t *cfg,sdmmc_card_t **out) {
 assert(!strcmp(path,"/sd") && h->slot==0 && slot->width==4 && !cfg->format_if_mount_failed);
 int err=h->init(); if (err!=ESP_OK) return err;
 sd_slot=true;
 if (missing) { assert(release(h)==ESP_OK); return ESP_ERR_TIMEOUT; }
 mounted_host=*h; *out=&test_card; return ESP_OK;
}
int esp_vfs_fat_sdcard_unmount(const char *p,sdmmc_card_t *c) {
 assert(!strcmp(p,"/sd") && c==&test_card); return release(&mounted_host);
}
int main(void) {
 for (unsigned i=0;i<3;i++) {
  assert(mount_card()==ESP_OK); // Reproduces startup failure before the fix.
  assert(card && sd_slot && controller && radio_slot);
  assert(unmount_card()==ESP_OK);
  assert(!card && !power && !dma && !sd_slot && radio_slot && controller);
 }
 missing=true;
 assert(mount_card()==ESP_ERR_TIMEOUT);
 assert(unmount_card()==ESP_OK && !sd_slot && radio_slot && controller);
 assert(shared_slot_deinit(0)==ESP_OK); // Failure before slot creation is safe.
 assert(shared_slot_deinit(1)==ESP_ERR_INVALID_ARG);
 assert(!duplicate_init && !whole_deinit && slot_deinit==5);
 puts("SD ownership: pre-main Hosted initialization, startup/remount, missing card and slot-only cleanup passed");
}
'''
with tempfile.TemporaryDirectory(prefix="sb-sdcard-") as tmp:
    p = Path(tmp)
    (p / "mock.h").write_text(header)
    for name in ("esp_err.h", "esp_hosted.h", "sdkconfig.h", "driver/gpio.h", "driver/sdmmc_host.h", "esp_heap_caps.h", "esp_log.h", "esp_vfs_fat.h", "freertos/FreeRTOS.h", "freertos/queue.h", "freertos/task.h", "sd_pwr_ctrl_by_on_chip_ldo.h", "sdmmc_cmd.h"):
        dest = p / name
        dest.parent.mkdir(exist_ok=True)
        dest.write_text('#include "mock.h"\n')
    (p / "test.c").write_text(harness)
    for radio, extra in ((0, []), (1, []), (1, ["-DSB_M0B"])):
        exe = p / "test"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", f"-DSB_NET_RADIO={radio}", *extra, "-I"+str(p), "-I"+str(root / "components/spacebattles"), str(p / "test.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
