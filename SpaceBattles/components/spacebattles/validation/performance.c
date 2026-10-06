// SPDX-License-Identifier: MIT
#include "validation/performance.h"
#if SB_PERFORMANCE
#include "render/renderer.h"
#include "input/input.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define SAMPLES 300
#define WARMUP 30
// Fixed storage, no allocation or serial output in the measured frame loop.
static struct {
    sb_frame_sample_t render;
    uint32_t frame_us, input_us, internal_free, psram_free;
} samples[SAMPLES];

esp_err_t sb_performance_run(void) {
    printf("M7_META version=1 bits=%u samples=%u warmup=%u radio=off storage=idle buffers=1\n",
           SB_COLOR_BITS,SAMPLES,WARMUP);
    for (unsigned scenario=0; scenario<4; ++scenario) {
        sb_input_reset();
        uint64_t epoch = esp_timer_get_time();
        unsigned offset = 0;
        for (unsigned n=0; n<WARMUP+SAMPLES; ++n) {
            int64_t start = esp_timer_get_time(), input_at = 0;
            sb_input_event_t event;
            for (unsigned b=0; b<32 && sb_input_poll(&event); ++b) {
                if (event.phase == SB_INPUT_RELEASE) continue;
                if (event.action == SB_ACTION_CANCEL) {
                    printf("M7_ABORT scenario=%u frames=%u\n",scenario,n);
                    return ESP_OK;
                }
                if (event.action >= SB_ACTION_UP && event.action <= SB_ACTION_RIGHT) {
                    if (!input_at) input_at = esp_timer_get_time();
                    offset = (offset+1)%100;
                }
            }
            sb_frame_sample_t render;
            esp_err_t err = sb_renderer_benchmark(scenario,start-epoch,
                ((start-epoch)/80000+offset)%100,&render);
            if (err != ESP_OK) {
                printf("M7_ERROR scenario=%u frame=%u error=%d\n",scenario,n,err);
                return err;
            }
            int64_t completed = esp_timer_get_time();
            // Yield to background/system tasks. Include this in throughput; no
            // intentional 60 Hz limiter hides overload or tick quantization.
            vTaskDelay(1);
            if (n>=WARMUP) {
                unsigned i=n-WARMUP;
                samples[i].render=render;
                samples[i].input_us=input_at ? (uint32_t)(completed-input_at) : 0;
                samples[i].internal_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
                samples[i].psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
                samples[i].frame_us=(uint32_t)(esp_timer_get_time()-start);
            }
        }
        for (unsigned i=0; i<SAMPLES; ++i)
            printf("M7_FRAME scenario=%u i=%u draw_us=%lu transfer_us=%lu frame_us=%lu poll_to_complete_us=%lu internal_free=%lu psram_free=%lu\n",
                   scenario,i,(unsigned long)samples[i].render.draw_us,
                   (unsigned long)samples[i].render.transfer_us,(unsigned long)samples[i].frame_us,
                   (unsigned long)samples[i].input_us,(unsigned long)samples[i].internal_free,
                   (unsigned long)samples[i].psram_free);
        printf("M7_MEMORY scenario=%u internal_low=%u psram_low=%u internal_largest=%u psram_largest=%u stack_free=%u\n",
               scenario,(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
               (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    printf("M7_DONE scenarios=4\n");
    return ESP_OK;
}
#endif
