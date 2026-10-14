// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nikolay Enev
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <pax_gfx.h>

#include "eyes_engine.h"

typedef struct {
    int preferred_width;
    uint32_t blink_seen, blink_drawn_at, last_draw;
    bool has_drawn, blink_presenting;
} eyes_widget;

// Fixed-size, allocation-free prototype. A NULL/stale sample gives a neutral pose.
int eyes_widget_draw(eyes_widget *widget, pax_buf_t *buffer,
                     int x_right, int y, int height,
                     const eyes_snapshot *snapshot, uint32_t now);
