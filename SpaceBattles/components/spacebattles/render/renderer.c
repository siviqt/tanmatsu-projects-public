// SPDX-License-Identifier: MIT
#include "render/renderer.h"
#include "validation/performance.h"
#if SB_PERFORMANCE
#include "esp_timer.h"
#endif
#include "render/ui.h"
#include "platform/tanmatsu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pax_gfx.h"
#include "pax_fonts.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/jpeg_decode.h"

static sb_font_profile_t font_profile = SB_FONT_MEDIUM;
static pax_buf_t framebuffer;
static void *pixels;
// Cache only what was drawn into the retained framebuffer. Other screens
// invalidate it; display ownership must be checked before any partial update.
static bool input_valid;
static unsigned input_x, input_y, input_panel;
static char input_config[128], input_feedback[160], input_cursor[96];

// The source art stays at 800x480; PAX maps it to queried logical display dimensions.
extern const uint8_t loading_start[] asm("_binary_loading_jpg_start");
extern const uint8_t loading_end[] asm("_binary_loading_jpg_end");
extern const uint8_t logoff_start[] asm("_binary_logoff_jpg_start");
extern const uint8_t logoff_end[] asm("_binary_logoff_jpg_end");
static pax_buf_t artwork;
static void *art_pixels;
static const uint8_t *cached_art;
static bool art_disabled;

static bool load_art(const uint8_t *start, size_t length) {
    if (art_disabled) return false;
    if (cached_art == start) return true;
    cached_art = NULL;
    jpeg_decoder_handle_t decoder = NULL;
    const jpeg_decode_engine_cfg_t engine = {.timeout_ms = 500};
    esp_err_t err = jpeg_new_decoder_engine(&engine, &decoder);
    size_t capacity = 0;
    const jpeg_decode_memory_alloc_cfg_t output = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    const jpeg_decode_memory_alloc_cfg_t input = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    if (err == ESP_OK && !art_pixels) {
        art_pixels = jpeg_alloc_decoder_mem(800 * 480 * 2, &output, &capacity);
        if (!art_pixels) err = ESP_ERR_NO_MEM;
        else if (!pax_buf_init(&artwork, art_pixels, 800, 480, PAX_BUF_16_565RGB)) {
            heap_caps_free(art_pixels);
            art_pixels = NULL;
            err = ESP_ERR_NO_MEM;
        }
    }
    void *compressed = NULL;
    if (err == ESP_OK) {
        compressed = jpeg_alloc_decoder_mem(length, &input, &capacity);
        if (!compressed) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        memcpy(compressed, start, length);
        const jpeg_decode_cfg_t config = {
            .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
            .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
        };
        uint32_t written = 0;
        err = jpeg_decoder_process(decoder, &config, compressed, length,
                                   art_pixels, 800 * 480 * 2, &written);
        if (err == ESP_OK && written != 800 * 480 * 2) err = ESP_ERR_INVALID_SIZE;
        if (err != ESP_OK) {
            // Do not reclaim DMA sources on an uncertain decoder completion.
            // Keep these bounded allocations alive until the supported restart.
            art_disabled = true;
            ESP_LOGW("M1", "art decode failed: %s; using text fallback", esp_err_to_name(err));
            return false;
        }
    }
    heap_caps_free(compressed);
    if (decoder) (void)jpeg_del_decoder_engine(decoder);
    if (err != ESP_OK) {
        art_disabled = true;
        ESP_LOGW("M1", "art setup failed: %s; using text fallback", esp_err_to_name(err));
        return false;
    }
    cached_art = start;
    return true;
}

static void draw_art(const uint8_t *start, size_t length) {
    pax_background(&framebuffer, 0xff080e20);
    if (load_art(start, length)) {
        pax_draw_image_sized(&framebuffer, &artwork, 0, 0,
                             pax_buf_get_width(&framebuffer), pax_buf_get_height(&framebuffer));
    } else {
        pax_draw_text(&framebuffer, 0xff65dafa, pax_font_sky_mono, 36, 32, 40, "SPACEBATTLES");
    }
}

static esp_err_t present(void) {
    pax_join();
    return sb_platform_present(pixels);
}

static void draw_status(const char *stage, float text_y) {
    float w = pax_buf_get_width(&framebuffer), h = pax_buf_get_height(&framebuffer);
    float sx = w / 800, sy = h / 480;
    // Feather the scrim to transparent on all four sides: no visible panel edge.
    // Small non-overlapping tiles keep blending independent of draw order.
    for (int y = 0; y < 72; y += 2) {
        float dy = (y + 1 - 36) / 36.0f;
        float fy = 1 - dy * dy;
        for (int x = 0; x < 360; x += 4) {
            float dx = (x + 2 - 180) / 180.0f;
            float fx = 1 - dx * dx;
            unsigned alpha = (unsigned)(160 * fx * fx * fy * fy);
            pax_draw_rect(&framebuffer, (alpha << 24) | 0x00050c14,
                          (220+x)*sx, (text_y-16+y)*sy, 4*sx, 2*sy);
        }
    }
    sb_font_t font = sb_font_get(font_profile, SB_FONT_CAPTION);
    float text_width = pax_text_size(font.font, font.size*sy, stage).x;
    pax_draw_text(&framebuffer, 0xffe2f4f6, font.font, font.size*sy,
                  (w-text_width)/2, text_y*sy, stage);
}

esp_err_t sb_renderer_loading(const char *stage, unsigned percent) {
    input_valid = false;
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    draw_art(loading_start, loading_end - loading_start);
    float w = pax_buf_get_width(&framebuffer), h = pax_buf_get_height(&framebuffer);
    float sx = w / 800, sy = h / 480;
    draw_status(stage, 390);
    pax_draw_rect(&framebuffer, 0x704d8b97, 260*sx, 419*sy, 280*sx, 3*sy);
    pax_draw_rect(&framebuffer, 0xff65dafa, 260*sx, 419*sy,
                  280*sx * (percent > 100 ? 100 : percent) / 100, 3*sy);
    return present();
}

static void draw_input_cell(unsigned x, unsigned y, bool focused) {
    pax_draw_rect(&framebuffer, focused ? 0xff65dafa : 0xff23364a,
                  24 + x*60, 128 + y*60, 54, 54);
    if (focused) pax_draw_text(&framebuffer, 0xff080e20, pax_font_sky_mono, 28,
                               40 + x*60, 140 + y*60, "+");
}

esp_err_t sb_renderer_input_test(const char *config_status, unsigned x, unsigned y,
                                 unsigned panel, const char *action, unsigned count, bool repeat) {
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    if (!input_valid) {
        pax_background(&framebuffer, 0xff080e20);
        pax_draw_text(&framebuffer, 0xff65dafa, pax_font_sky_mono, 28, 24, 16, "SPACEBATTLES / INPUT TEST");
        for (unsigned row = 0; row < 4; ++row) for (unsigned col = 0; col < 6; ++col)
            draw_input_cell(col, row, col == x && row == y);
        pax_draw_text(&framebuffer, 0xffdce8f8, pax_font_sky_mono, 16, 412, 86,
                      "Arrows   Move (hold to repeat)\nEnter    Ready / fire\nSpace    Place / select\nR        Rotate\nTab      Next panel\nF1       Help\nF2 / F3  Fleet / targets\nF4       Settings / pause\nF5       Radio status\nF6       Both boards\nEsc / Backspace  Log off");
        pax_draw_text(&framebuffer, 0xffdce8f8, pax_font_sky_mono, 14, 24, 451,
                      "M3 diagnostic: action feedback only; game and settings screens follow later.");
    } else if (x != input_x || y != input_y) {
        draw_input_cell(input_x, input_y, false);
        draw_input_cell(x, y, true);
    }
    char config[sizeof(input_config)];
    snprintf(config, sizeof(config), "%s", config_status);
    if (!input_valid || strcmp(config, input_config)) {
        pax_draw_rect(&framebuffer, 0xff080e20, 24, 54, 776, 20);
        pax_draw_text(&framebuffer, 0xffdce8f8, pax_font_sky_mono, 16, 24, 54, config);
        strcpy(input_config, config);
    }
    if (!input_valid || panel != input_panel) {
        const char *panels[] = {"OWN FLEET", "ENEMY VIEW"};
        for (unsigned i = 0; i < 2; ++i) {
            pax_draw_rect(&framebuffer, i == panel ? 0xff65dafa : 0xff23364a, 24 + i*186, 82, 178, 30);
            pax_draw_text(&framebuffer, i == panel ? 0xff080e20 : 0xffdce8f8,
                          pax_font_sky_mono, 18, 32 + i*186, 88, panels[i]);
        }
    }
    char feedback[sizeof(input_feedback)], cursor[sizeof(input_cursor)];
    snprintf(feedback, sizeof(feedback), "Last: %s (%s)   Events: %u",
             action, repeat ? "repeat" : "press", count);
    snprintf(cursor, sizeof(cursor), "Cursor: %u,%u   Repeat: 400 ms / 80 ms", x+1, y+1);
    if (!input_valid || strcmp(feedback, input_feedback)) {
        pax_draw_rect(&framebuffer, 0xff080e20, 24, 388, 776, 20);
        pax_draw_text(&framebuffer, 0xff65dafa, pax_font_sky_mono, 18, 24, 388, feedback);
        strcpy(input_feedback, feedback);
    }
    if (!input_valid || strcmp(cursor, input_cursor)) {
        pax_draw_rect(&framebuffer, 0xff080e20, 24, 408, 776, 20);
        pax_draw_text(&framebuffer, 0xff65dafa, pax_font_sky_mono, 18, 24, 408, cursor);
        strcpy(input_cursor, cursor);
    }
    input_x = x; input_y = y; input_panel = panel;
    esp_err_t err = present();
    input_valid = err == ESP_OK;
    return err;
}

esp_err_t sb_renderer_ready(const char *config_status) {
    input_valid = false;
    return sb_renderer_input_test(config_status, 0, 0, 0, "NONE", 0, false);
}

esp_err_t sb_renderer_exit(void) {
    input_valid = false;
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    draw_art(logoff_start, logoff_end - logoff_start);
    draw_status("RETURNING TO LAUNCHER...", 402);
    return present();
}

esp_err_t sb_renderer_init(void) {
    input_valid = false;
    sb_display_t display;
    esp_err_t err = sb_platform_display_init(&display);
    if (err != ESP_OK) return err;
    if (!display.width || !display.height || display.width > SIZE_MAX / display.height / (SB_COLOR_BITS / 8))
        return ESP_ERR_INVALID_SIZE;
    pixels = heap_caps_aligned_alloc(64, display.width * display.height * (SB_COLOR_BITS / 8),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) return ESP_ERR_NO_MEM;
    if (!pax_buf_init(&framebuffer, pixels, display.width, display.height, SB_COLOR_BITS == 24 ? PAX_BUF_24_888RGB : PAX_BUF_16_565RGB)) {
        heap_caps_free(pixels);
        pixels = NULL;
        return ESP_FAIL;
    }
    pax_buf_reversed(&framebuffer, display.big_endian);
    pax_orientation_t rotation = PAX_O_UPRIGHT;
    if (display.rotation == 90) rotation = PAX_O_ROT_CCW;
    if (display.rotation == 180) rotation = PAX_O_ROT_HALF;
    if (display.rotation == 270) rotation = PAX_O_ROT_CW;
    pax_buf_set_orientation(&framebuffer, rotation);
    return ESP_OK;
}

esp_err_t sb_renderer_message(const char *message) {
    input_valid = false;
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    pax_background(&framebuffer, 0xff080e20);
    sb_font_t heading = sb_font_get(font_profile, SB_FONT_HEADING);
    sb_font_t body = sb_font_get(font_profile, SB_FONT_BODY);
    pax_draw_text(&framebuffer, 0xff65dafa, heading.font, heading.size, 32, 40, "SPACEBATTLES");
    pax_draw_text(&framebuffer, 0xffdce8f8, body.font, body.size, 32, 120, message);
    return present();
}

void sb_renderer_shutdown(void) {
    input_valid = false;
    pax_join();
    if (art_pixels && !art_disabled) {
        pax_buf_destroy(&artwork);
        heap_caps_free(art_pixels);
        art_pixels = NULL;
        cached_art = NULL;
    }
    if (pixels && sb_platform_can_release_frame()) {
        pax_buf_destroy(&framebuffer);
        heap_caps_free(pixels);
        pixels = NULL;
        ESP_LOGI("M0a", "application frame released after confirmed completion");
    }
    // On uncertain transfer completion keep source memory alive until reset.
    if (pixels) ESP_LOGW("M0a", "application frame retained until restart");
}

void sb_renderer_set_font_profile(const char *name) {
    font_profile = sb_font_profile_parse(name);
    input_valid = false;
}
esp_err_t sb_renderer_ui_preview(unsigned page) {
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    input_valid = false;
    if (!sb_ui_preview(&framebuffer,font_profile,page)) return ESP_ERR_INVALID_SIZE;
    return present();
}
esp_err_t sb_renderer_tactical_preview(unsigned page, unsigned cursor, bool enemy) {
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    input_valid = false;
    if (!sb_ui_tactical_preview(&framebuffer,font_profile,page,cursor,enemy)) return ESP_ERR_INVALID_SIZE;
    return present();
}

#if SB_PERFORMANCE
esp_err_t sb_renderer_benchmark(unsigned scenario, uint64_t elapsed_us, unsigned cursor,
                                sb_frame_sample_t *sample) {
    if (!pixels || !sample || scenario >= 4 || !sb_platform_can_release_frame())
        return ESP_ERR_INVALID_STATE;
    input_valid = false;
    int64_t start = esp_timer_get_time();
    // Immutable synthetic resolved record, one Classic shot or ten round attacks.
    // Both attackers are represented; only disclosed impacts on this board draw.
    sb_record_t record = {.count = scenario < 2 ? 1 : 20};
    for (unsigned i=0; i<record.count; ++i) {
        sb_event_t *e = &record.events[i];
        e->type = i < 10 ? SB_ATTACK_EVENT : SB_SUNK_EVENT;
        e->attacker = (i % 10) / 5; e->cell = (i % 10)*9; e->hit = true;
        e->length = 2; e->cells[0] = e->cell; e->cells[1] = e->cell+1;
    }
    sb_ui_tactical_t view = {.ruleset = scenario < 2 ? SB_CLASSIC : SB_ESCALATED,
        .play_type = SB_UI_SINGLE_PLAYER, .own_board = !(scenario & 1), .active = true,
        .sequence = 999, .seconds_remaining = 23, .selected = scenario < 2 ? 1 : 5,
        .available = scenario < 2 ? 1 : 5, .cursor = cursor, .effects = &record};
    if (view.own_board) {
        static const unsigned starts[] = {10,27,40,65,88}, lengths[] = {5,4,3,3,2};
        for (unsigned i=0; i<5; ++i) for (unsigned j=0; j<lengths[i]; ++j)
            view.occupied[starts[i]+j*(i==1 ? 10 : 1)] = true;
        if (scenario < 2) record.events[0].attacker = 1;
    }
    view.cells[88] = view.cells[89] = SB_SUNK; view.cells[22] = SB_MISS;
    bool ok = sb_ui_tactical(&framebuffer, SB_FONT_LARGE, &view);
    if (scenario & 1) {
        // M7 load fixture, not the M11 animation controller: elapsed-time pulses.
        float radius = 3 + (elapsed_us % 500000) * 11.0f / 500000;
        for (unsigned i=0; i<(scenario < 2 ? 1u : 10u); ++i)
            pax_draw_circle(&framebuffer, 0x80ff8b70,72+(i*9%10)*32,
                            112+(i*9/10)*32,radius);
    }
    pax_join();
    int64_t drawn = esp_timer_get_time();
    if (!ok) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = sb_platform_present(pixels);
    sample->draw_us = (uint32_t)(drawn-start);
    sample->transfer_us = (uint32_t)(esp_timer_get_time()-drawn);
    return err;
}
#endif

esp_err_t sb_renderer_tactical(const sb_ui_tactical_t *view) {
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    input_valid = false;
    if (!sb_ui_tactical(&framebuffer, font_profile, view)) return ESP_ERR_INVALID_SIZE;
    return present();
}
esp_err_t sb_renderer_handover(bool player_b, bool deployment, bool first_press) {
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    input_valid = false;
    if (!sb_ui_handover(&framebuffer, font_profile, player_b, deployment, first_press)) return ESP_ERR_INVALID_SIZE;
    return present();
}

esp_err_t sb_renderer_menu(const sb_menu_view_t *view) {
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    input_valid = false;
    if (!sb_ui_menu(&framebuffer,font_profile,view)) return ESP_ERR_INVALID_SIZE;
    return present();
}

esp_err_t sb_renderer_handover_match(bool player_b, bool deployment, bool first_press, unsigned starter) {
    if (!pixels || !sb_platform_can_release_frame()) return ESP_ERR_INVALID_STATE;
    input_valid=false;
    if(!sb_ui_handover(&framebuffer,font_profile,player_b,deployment,first_press)) return ESP_ERR_INVALID_SIZE;
    if(deployment) {
        char label[48]; snprintf(label,sizeof(label),"Player %c selects first.",'A'+(starter==1));
        sb_font_t font=sb_font_get(font_profile,SB_FONT_CAPTION);
        pax_draw_text(&framebuffer,0xffdce8f8,font.font,font.size,24,356,label);
    }
    return present();
}
