// SPDX-License-Identifier: MIT
#include "platform/tanmatsu.h"
#include "bsp/led.h"
// Tanmatsu's last two LEDs are the application A/B indicators. Automatic mode
// leaves system LEDs under coprocessor control (launcher plugin API convention).
static bool enabled=true;
static unsigned brightness=100;
static int current_player=-1;
static uint32_t color(uint32_t c) {
    if(!enabled) return 0;
    return ((((c>>16)&255)*brightness/100)<<16) | ((((c>>8)&255)*brightness/100)<<8) | ((c&255)*brightness/100);
}
esp_err_t sb_platform_indicator_settings(bool on, unsigned percent) {
    enabled=on; brightness=percent>100 ? 100 : percent;
    return sb_platform_turn_indicator(current_player);
}
esp_err_t sb_platform_turn_indicator(int player) {
    uint32_t count = 0;
    esp_err_t err = bsp_led_get_count(&count);
    if (err != ESP_OK) return err;
    if (count < 2 || player < -1 || player > 1) return ESP_ERR_INVALID_ARG;
    err = bsp_led_set_pixel(count - 2, player == 0 ? color(0x40c0ff) : 0);
    if (err == ESP_OK) err = bsp_led_set_pixel(count - 1, player == 1 ? color(0xffa040) : 0);
    if (err == ESP_OK) err = bsp_led_send();
    if(err==ESP_OK) current_player=player;
    return err;
}
