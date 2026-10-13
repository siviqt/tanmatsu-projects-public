#!/usr/bin/env python3
"""Exercise the real M8 app loop and LED adapter with deterministic hardware mocks."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sb-m8-runtime-') as tmp:
    p=Path(tmp)
    (p/'freertos').mkdir(); (p/'bsp').mkdir()
    (p/'pax_gfx.h').write_text('#pragma once\ntypedef struct pax_font pax_font_t;\ntypedef struct pax_buf pax_buf_t;\n')
    (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG -2\nconst char *esp_err_to_name(int);\n')
    (p/'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
    (p/'esp_random.h').write_text('#include <stdint.h>\nuint32_t esp_random(void);\n')
    (p/'esp_log.h').write_text('#define ESP_LOGE(...) ((void)0)\n')
    (p/'freertos/FreeRTOS.h').write_text('#define pdMS_TO_TICKS(x) (x)\n')
    (p/'freertos/task.h').write_text('void vTaskDelay(unsigned);\n')
    (p/'bsp/led.h').write_text('#include <stdint.h>\n#include "esp_err.h"\nesp_err_t bsp_led_get_count(uint32_t*);\nesp_err_t bsp_led_set_pixel(uint32_t,uint32_t);\nesp_err_t bsp_led_send(void);\n')
    (p/'test.c').write_text(r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "app/local_run.c"
esp_err_t sb_versus_run(sb_config_t *c,bool h,sb_ruleset_t r,sb_storage_t *s) { (void)s; (void)c; (void)h; (void)r; return ESP_FAIL; }
#include "platform/turn_led.c"
void sb_renderer_set_font_profile(const char *s) { assert(s); }
esp_err_t sb_platform_apply_settings(const sb_settings_t *s) { assert(s); return ESP_OK; }
bool sb_storage_save(sb_storage_t *s,sb_config_t *c) { (void)s; c->dirty=false; return true; }
const char *sb_storage_status_text(const sb_storage_t *s) { (void)s; return "SD available"; }
static unsigned pos, resets, frames, covers, boards, a_frames, off_frames;
static unsigned pixels[6];
static bool fail_cover, fail_led, combat_test;
static sb_input_event_t combat_script[100];
static unsigned combat_count, pause_index, result_frames;
static int64_t now_us, result_visible_us;
static void add(sb_action_t action, sb_input_phase_t phase) {
    combat_script[combat_count++]=(sb_input_event_t){action,phase};
}
static void tap(sb_action_t action) { add(action,SB_INPUT_PRESS); }
static void handover(void) {
    for(unsigned n=0;n<1;++n) { tap(SB_ACTION_FIRE); add(SB_ACTION_FIRE,SB_INPUT_RELEASE); }
}
static void make_combat_script(void) {
    combat_count=0;
    tap(SB_ACTION_DOWN); tap(SB_ACTION_CONFIRM); tap(SB_ACTION_CONFIRM);
    for(unsigned p=0;p<2;++p) {
        handover();
        for(unsigned ship=0;ship<5;++ship) { tap(SB_ACTION_FIRE); if(ship<4) tap(SB_ACTION_DOWN); }
        tap(SB_ACTION_CONFIRM);
    }
    handover(); tap(SB_ACTION_SELECT); tap(SB_ACTION_COMMIT); // Select and fire.
    pause_index=combat_count; tap(SB_ACTION_COMMIT);
    tap(SB_ACTION_CANCEL); tap(SB_ACTION_UP); tap(SB_ACTION_CONFIRM); tap(SB_ACTION_CANCEL); tap(SB_ACTION_CONFIRM);
}
static const sb_input_event_t script[]={
    {SB_ACTION_DOWN,SB_INPUT_PRESS},
    {SB_ACTION_CONFIRM,SB_INPUT_PRESS}, // menu
    {SB_ACTION_CONFIRM,SB_INPUT_PRESS}, // shared setup
    {SB_ACTION_HELP,SB_INPUT_PRESS}, {SB_ACTION_CANCEL,SB_INPUT_PRESS},
    {SB_ACTION_FIRE,SB_INPUT_PRESS}, {SB_ACTION_FIRE,SB_INPUT_RELEASE},
    {SB_ACTION_FIRE,SB_INPUT_PRESS}, // place one ship
    {SB_ACTION_CANCEL,SB_INPUT_PRESS}, {SB_ACTION_UP,SB_INPUT_PRESS}, {SB_ACTION_CONFIRM,SB_INPUT_PRESS}, // abandon
    {SB_ACTION_CANCEL,SB_INPUT_PRESS}, // exit from main menu
    {SB_ACTION_CONFIRM,SB_INPUT_PRESS}, // confirm logoff
};
int64_t esp_timer_get_time(void) { return now_us; }
uint32_t esp_random(void) { return 0; }
void vTaskDelay(unsigned t) { now_us+=t*1000; }
void sb_input_set_text_mode(bool enabled) { (void)enabled; }
void sb_input_reset(void) { ++resets; }
bool sb_input_poll(sb_input_event_t *event) {
    if(combat_test) {
        if(pos==pause_index && (!result_frames || now_us<result_visible_us+3000000)) return false;
        assert(pos<combat_count); *event=combat_script[pos++]; return true;
    }
    assert(pos<sizeof(script)/sizeof(script[0])); *event=script[pos++]; return true;
}
esp_err_t bsp_led_get_count(uint32_t *count) { *count=6; return ESP_OK; }
esp_err_t bsp_led_set_pixel(uint32_t index,uint32_t color) {
    assert(index==4 || index==5); pixels[index]=color; return ESP_OK;
}
esp_err_t bsp_led_send(void) {
    assert(!(pixels[4] && pixels[5]));
    if(pixels[4]) ++a_frames; else if(!pixels[5]) ++off_frames;
    if(fail_led) { fail_led=false; return ESP_FAIL; } return ESP_OK;
}
esp_err_t sb_renderer_message(const char *s) { assert(s && *s); ++frames; return ESP_OK; }
esp_err_t sb_renderer_message(const char *s);
esp_err_t sb_renderer_menu(const sb_menu_view_t *v) {
    for(unsigned i=0;i<v->count;++i) assert(!strstr(v->rows[i],"Match abandoned"));
    char text[1024]; snprintf(text,sizeof(text),"%s\n",v->title);
    for(unsigned i=0;i<v->count;++i) { strcat(text,v->rows[i]); strcat(text,"\n"); }
    return sb_renderer_message(text);
}
esp_err_t sb_renderer_handover(bool b,bool deployment,bool first) {
    if(!combat_test) assert(!b && deployment);
    if(combat_test && !deployment && b) assert(result_frames==1 && now_us>=result_visible_us+3000000);
    (void)first; ++covers;
    return fail_cover ? ESP_FAIL : ESP_OK;
}
esp_err_t sb_renderer_handover_match(bool b,bool d,bool f,unsigned starter) {
    assert(starter<2); return sb_renderer_handover(b,d,f);
}
esp_err_t sb_renderer_tactical(const sb_ui_tactical_t *v) {
    if(!combat_test) assert(v->own_board && !v->player_b);
    if(combat_test && v->effects) {
        assert(!v->own_board && !strcmp(v->phase_label,"HIT"));
        ++result_frames;
        if(result_frames==1) { now_us+=250000; result_visible_us=now_us; }
        assert(!strstr(v->action_label,"..."));
    }
    ++boards; return ESP_OK;
}
static void fresh(void) { pos=resets=frames=covers=boards=a_frames=off_frames=0; fail_cover=fail_led=combat_test=false; now_us=result_visible_us=0; result_frames=0; }
int main(void) {
    assert(sb_platform_turn_indicator(0)==ESP_OK);
    assert(sb_platform_indicator_settings(true,50)==ESP_OK && pixels[4]==0x20607f && !pixels[5]);
    assert(sb_platform_indicator_settings(false,50)==ESP_OK && !pixels[4] && !pixels[5]);
    assert(sb_platform_turn_indicator(1)==ESP_OK && !pixels[5]);
    assert(sb_platform_indicator_settings(true,50)==ESP_OK && pixels[5]==0x7f5020 && !pixels[4]);
    assert(sb_platform_indicator_settings(true,100)==ESP_OK);
    assert(sb_platform_turn_indicator(-1)==ESP_OK);
    sb_config_t config={0}; strcpy(config.settings.preferred_ruleset,"classic");
    fresh(); assert(sb_local_run(&config,NULL)==ESP_OK);
    assert(boards && covers && a_frames && off_frames && resets>=8);
    assert(!pixels[4] && !pixels[5] && pos==sizeof(script)/sizeof(script[0]));
    fresh(); combat_test=true; make_combat_script();
    assert(sb_local_run(&config,NULL)==ESP_OK && result_frames==1 && pos==combat_count);
    assert(!pixels[4] && !pixels[5]);
    fresh(); fail_cover=true; assert(sb_local_run(&config,NULL)==ESP_FAIL);
    assert(!boards && covers==1 && !pixels[4] && !pixels[5]);
    fresh(); fail_led=true; assert(sb_local_run(&config,NULL)==ESP_FAIL);
    assert(!pos && !frames && !pixels[4] && !pixels[5]);
    puts("M8 runtime: menu, help/cover return, deployment, explicit result continuation, abandon, LED cleanup and display/LED failures passed");
}
''')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+tmp,
        '-I'+str(root/'components/spacebattles'),str(p/'test.c'),str(root/'components/spacebattles/app/local.c'),
        *[str(root/'components/spacebattles'/f) for f in ('app/menu.c','services/config.c','services/preferences.c')],
        *map(str,sorted((root/'components/spacebattles/game').glob('*.c'))),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
