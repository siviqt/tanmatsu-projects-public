// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    esp_err_t error;
    bool worker_pending;
} sb_sd_mount_result_t;
// One mount attempt per application launch. Never formats or writes files.
// A completed driver timeout means unavailable storage, not a stuck worker.
// Only worker_pending requires restart while retaining worker-owned resources.
sb_sd_mount_result_t sb_platform_sd_mount(void);
esp_err_t sb_platform_sd_shutdown(void);
