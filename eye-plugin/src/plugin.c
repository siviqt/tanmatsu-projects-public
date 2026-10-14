// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nikolay Enev
#include "eyes.h"
#include <tanmatsu_plugin.h>
#include <asp/orientation.h>
#include <pthread.h>
#include <math.h>
#include <string.h>

static struct {
    pthread_mutex_t mutex;
    bool mutex_ready, accel_enabled, gyro_enabled;
    int widget_id;
    eyes_widget widget;
    eyes_engine engine;
    eyes_snapshot snapshot;
    eyes_snapshot render_snapshot; // Only accessed by the host render callback.
} state = {.widget_id = -1};

static int draw(pax_buf_t *buffer, int right, int y, int height, void *data) {
    (void)data;

    // Host must serialize callback entry against cleanup/ELF unload. The snapshot
    // mutex only serializes data with the worker; it cannot enforce code lifetime.
    if (pthread_mutex_trylock(&state.mutex) == 0) {
        state.render_snapshot = state.snapshot;
        pthread_mutex_unlock(&state.mutex);
    }
    // Reuse the previous pose on contention; never omit an entire visible frame.
    return eyes_widget_draw(&state.widget, buffer, right, y, height,
                            &state.render_snapshot, asp_plugin_get_tick_ms());
}

static int init(plugin_context_t *ctx) {
    if (state.mutex_ready) return -1;
    memset(&state.snapshot, 0, sizeof(state.snapshot));
    memset(&state.render_snapshot, 0, sizeof(state.render_snapshot));
    memset(&state.widget, 0, sizeof(state.widget));
#ifdef EYES_DETERMINISTIC_SEED
    uint32_t seed = EYES_DETERMINISTIC_SEED;
#else
    uint32_t seed = asp_plugin_get_tick_ms() ^ 0x65796573u;
#endif
    eyes_engine_init(&state.engine, seed, asp_plugin_get_tick_ms());
    asp_log_info("eyes", "Animation seed: %lu", (unsigned long)state.engine.rng);
    if (pthread_mutex_init(&state.mutex, NULL) != 0) return -1;
    state.mutex_ready = true;
    state.widget_id = asp_plugin_status_widget_register(ctx, draw, NULL);
    if (state.widget_id < 0) {
        pthread_mutex_destroy(&state.mutex);
        state.mutex_ready = false;
        return -1;
    }
    return 0;
}

static void cleanup(plugin_context_t *ctx) {
    (void)ctx;
    if (state.widget_id >= 0) {
        asp_plugin_status_widget_unregister(state.widget_id);
        state.widget_id = -1;
    }
    // Sole-orientation-client prototype: the SDK has no ownership/refcount API.
    if (state.accel_enabled) asp_orientation_disable_accelerometer();
    if (state.gyro_enabled) asp_orientation_disable_gyroscope();
    state.accel_enabled = state.gyro_enabled = false;
    if (state.mutex_ready) {
        pthread_mutex_destroy(&state.mutex);
        state.mutex_ready = false;
    }
}

static bool valid_vector(const float v[3], float limit) {
    for (int i=0;i<3;++i) if (!isfinite(v[i]) || fabsf(v[i])>limit) return false;
    return true;
}

static void run(plugin_context_t *ctx) {
    state.accel_enabled = asp_orientation_enable_accelerometer() == ASP_OK;
    state.gyro_enabled = asp_orientation_enable_gyroscope() == ASP_OK;
    unsigned failures=0;
    uint32_t last_read=asp_plugin_get_tick_ms()-1000;
    while (!asp_plugin_should_stop(ctx)) {
        uint32_t started=asp_plugin_get_tick_ms();
        eyes_snapshot next = {0};
        if ((state.accel_enabled || state.gyro_enabled) &&
            (uint32_t)(started-last_read)>=(failures>=5?1000u:20u)) {
            last_read=started;
            asp_err_t result = asp_orientation_get(&next.gyro_ready, &next.accel_ready,
                &next.gyro[0], &next.gyro[1], &next.gyro[2],
                &next.accel[0], &next.accel[1], &next.accel[2]);
            next.accel_ready = result == ASP_OK && state.accel_enabled && next.accel_ready && valid_vector(next.accel,40);
            next.gyro_ready = result == ASP_OK && state.gyro_enabled && next.gyro_ready && valid_vector(next.gyro,2200);
            if (next.accel_ready || next.gyro_ready) {
                if (failures>=5) asp_log_info("eyes", "IMU reads resumed");
                failures=0;
            } else if (failures<5 && ++failures==5) {
                asp_log_info("eyes", "IMU unavailable; retrying reads at 1 Hz");
            }
        }
        next.sampled_at = asp_plugin_get_tick_ms();
        eyes_engine_update(&state.engine, &next, next.sampled_at);
        next.animated = true;
        next.pose = state.engine.pose;
        pthread_mutex_lock(&state.mutex);
        state.snapshot = next;
        pthread_mutex_unlock(&state.mutex);
        uint32_t spent=asp_plugin_get_tick_ms()-started;
        asp_plugin_delay_ms(spent<20?20-spent:1);
    }
}

static const plugin_info_t *info(void) {
    static const plugin_info_t value = {
        .name = "Googly Eyes", .slug = "com.github.siviqt.eyes",
        .version = "0.5.4", .author = "Nikolay Enev",
        .description = "Autonomous moods and animated eye variants",
        .api_version = TANMATSU_PLUGIN_API_VERSION, .type = PLUGIN_TYPE_SERVICE,
    };
    return &value;
}
static const plugin_entry_t entry = {.get_info = info, .init = init, .cleanup = cleanup, .service_run = run};
TANMATSU_PLUGIN_REGISTER(entry);
