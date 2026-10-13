#!/usr/bin/env python3
"""Exercise M9 Single Player menu and app loop with deterministic hardware mocks."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sb-m9-runtime-') as tmp:
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
void sb_renderer_set_font_profile(const char *s) { assert(s); }
esp_err_t sb_platform_apply_settings(const sb_settings_t *s) { assert(s); return ESP_OK; }
bool sb_storage_save(sb_storage_t *s,sb_config_t *c) { (void)s; c->dirty=false; return true; }
const char *sb_storage_status_text(const sb_storage_t *s) { (void)s; return "SD available"; }
static unsigned prefs, selected, setup, mode, stage, deploy, moves, result_frames, menu_frames, boards;
static unsigned resets, rematches;
static uint32_t seed;
static int64_t now;
static bool done, in_result, saw_confirmation;
uint32_t esp_random(void) { seed=seed*1664525u+1013904223u; return seed; }
int64_t esp_timer_get_time(void) { return now; }
const char *esp_err_to_name(int e) { (void)e; return "mock"; }
void vTaskDelay(unsigned t) { now+=(int64_t)t*1000; }
void sb_input_set_text_mode(bool enabled) { (void)enabled; }
void sb_input_reset(void) { ++resets; }
esp_err_t sb_platform_turn_indicator(int p) { assert(p==-1); return ESP_OK; }
esp_err_t sb_renderer_message(const char *s);
esp_err_t sb_renderer_menu(const sb_menu_view_t *v) {
    for(unsigned i=0;i<v->count;++i) assert(!strstr(v->rows[i],"Match abandoned"));
    selected=v->selected;
    char text[1024]; snprintf(text,sizeof(text),"%s\n",v->title);
    for(unsigned i=0;i<v->count;++i) { strcat(text,v->rows[i]); strcat(text,"\n"); }
    return sb_renderer_message(text);
}
esp_err_t sb_renderer_handover(bool b,bool d,bool f) { (void)b;(void)d;(void)f; assert(0); return ESP_FAIL; }
esp_err_t sb_renderer_message(const char *s) {
    in_result=false;
    if(strstr(s,"SPACEBATTLES")) ++menu_frames;
    if(strstr(s,"Fire at") || strstr(s,"Fire 5")) saw_confirmation=true;
    return ESP_OK;
}
esp_err_t sb_renderer_handover_match(bool b,bool d,bool f,unsigned starter) {
    assert(starter<2); return sb_renderer_handover(b,d,f);
}
esp_err_t sb_renderer_tactical(const sb_ui_tactical_t *v) {
    assert(v->play_type==SB_UI_SINGLE_PLAYER && !v->player_b);
    assert(v->ruleset==((mode<3)!=!!rematches ? SB_CLASSIC : SB_ESCALATED));
    if(!v->own_board) for(unsigned c=0;c<100;++c) assert(!v->occupied[c]);
    ++boards;
    in_result=v->effects!=NULL;
    if(in_result) ++result_frames;
    if(mode>=3 && !v->deployment) assert(v->seconds_remaining<=600);
    return ESP_OK;
}
bool sb_input_poll(sb_input_event_t *e) {
    // The mocked display labels determine only whether a result is locked.
    // Advance wall time until the real app exits its presentation phase.
    if(in_result) { *e=(sb_input_event_t){SB_ACTION_COMMIT,SB_INPUT_PRESS}; return true; }
    sb_action_t action=SB_ACTION_NONE;
    if(stage==0) {
        if(setup==0) { action=SB_ACTION_CONFIRM; ++setup; }
        else if(setup==1) { assert(selected==2); action=SB_ACTION_UP; ++setup; }
        else if(setup==2) { action=SB_ACTION_UP; ++setup; }
        else if(setup==3) { action=SB_ACTION_RIGHT; ++setup; }
        else if(setup==4) { action=SB_ACTION_DOWN; ++setup; }
        else if(setup==5) { action=SB_ACTION_RIGHT; ++setup; }
        else if(setup==6) { action=SB_ACTION_DOWN; ++setup; }
        else { action=SB_ACTION_CONFIRM; stage=2; moves=mode+1; }
    }
    else if(stage==2) {
        if(deploy<9) action=(deploy++%2)==0 ? SB_ACTION_FIRE : SB_ACTION_DOWN;
        else { action=SB_ACTION_CONFIRM; stage=3; }
    } else if(stage==3) {
        if(mode<3) { action=SB_ACTION_SELECT; stage=13; }
        else if(moves<mode+10) { action=(moves++%2)==(mode%2) ? SB_ACTION_RIGHT : SB_ACTION_FIRE; }
        else { action=SB_ACTION_CONFIRM; stage=5; }
    } else if(stage==13) { action=SB_ACTION_COMMIT; stage=5; }
    else if(stage==4) { action=SB_ACTION_CONFIRM; stage=8; }
    else if(stage==5) { action=SB_ACTION_CANCEL; stage=6; }
    else if(stage==6) { if(selected!=4) action=SB_ACTION_DOWN; else { action=SB_ACTION_CONFIRM; stage=7; } }
    else if(stage==7 && !rematches++) { action=SB_ACTION_CONFIRM; stage=4; }
    else if(stage==8) { action=SB_ACTION_CANCEL; stage=9; }
    else if(stage==9) { if(selected!=4) action=SB_ACTION_UP; else { action=SB_ACTION_CONFIRM; stage=11; } }
    else if(stage==10) { action=SB_ACTION_CANCEL; stage=11; }
    else if(stage==11) { action=SB_ACTION_CANCEL; stage=12; }
    else { action=SB_ACTION_CONFIRM; done=true; }
    *e=(sb_input_event_t){action,SB_INPUT_PRESS}; return true;
}
int main(void) {
    sb_config_t config={.settings.targeting_confirmation=true};
    for(unsigned preferences=1;preferences<3;++preferences) for(mode=1;mode<=4;++mode) {
        prefs=preferences;
        setup=stage=deploy=moves=result_frames=menu_frames=boards=resets=rematches=0;
        if (preferences) {
            strcpy(config.settings.preferred_ruleset,mode<3 ? "escalated" : "classic");
            strcpy(config.settings.ai_difficulty,preferences==2 ? "hard" : mode%2 ? "normal" : "easy");
            moves=mode; // Saved preferences select the desired row without navigation.
        }
        done=in_result=saw_confirmation=false; seed=123; now=0;
        assert(sb_local_run(&config,NULL)==ESP_OK);
        if (preferences) {
            assert(!strcmp(config.settings.preferred_ruleset,mode<3 ? "escalated" : "classic"));
            assert(!strcmp(config.settings.ai_difficulty,preferences==2 ? "hard" : mode%2 ? "normal" : "easy"));
        }
        assert(done && boards && result_frames && menu_frames && resets && !saw_confirmation);
    }
    puts("M10 single-page setup ruleset/difficulty changes preserve saved preferences");
    puts("M9 runtime: all four Single Player menu choices, placement, immediate shots, AI results, direct abandonment, saved setup defaults and LED privacy passed");
}
''')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+tmp,
        '-I'+str(root/'components/spacebattles'),str(p/'test.c'),str(root/'components/spacebattles/app/local.c'),
        *[str(root/'components/spacebattles'/f) for f in ('app/menu.c','services/config.c','services/preferences.c')],
        *map(str,sorted((root/'components/spacebattles/game').glob('*.c'))),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
