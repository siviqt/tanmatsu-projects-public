// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <stdint.h>
#ifndef SB_PERFORMANCE
#define SB_PERFORMANCE 0
#endif
#ifndef SB_COLOR_BITS
#define SB_COLOR_BITS 16
#endif
typedef struct { uint32_t draw_us, transfer_us; } sb_frame_sample_t;
esp_err_t sb_performance_run(void);
