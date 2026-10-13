#!/usr/bin/env python3
"""Host regression tests for real application lifecycle code with hardware mocks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="spacebattles-m0a-") as tmp:
    path = Path(tmp)
    (path / "freertos").mkdir()
    (path / "pax_gfx.h").write_text("#pragma once\ntypedef struct pax_font pax_font_t;\ntypedef struct pax_buf pax_buf_t;\n")
    (path / "esp_err.h").write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 2
const char *esp_err_to_name(esp_err_t err);
""")
    (path / "esp_timer.h").write_text("#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
    (path / "esp_log.h").write_text("""#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
""")
    (path / "freertos/FreeRTOS.h").write_text("#define pdMS_TO_TICKS(x) (x)\n")
    (path / "freertos/task.h").write_text("void vTaskDelay(unsigned ticks);\n")
    (path / "test.c").write_text(r'''
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "app/application.c"
static jmp_buf exit_point;
static bool sd_pending, unavailable_message;
static int bsp_result, display_result, input_result, sd_result;
static int fail_frame, frame_calls, input_calls, sd_calls, waits, delay_ms;
static int sd_cleanup, frame_cleanup, exits, cleanup_result;
static int64_t now_us, exit_presented_us;
static int cleanup_ms, startup_ms;
static bool exit_ok, config_failure;
static int config_loads, config_saves;
static char trace[128];
static bool cover_test;
static bool scripted, test_frame_failure, pause_sent, burst;
static unsigned script_pos, renders, resets, last_page, profile_changes, last_cursor;
static bool last_enemy;
static const sb_input_event_t script[] = {
    {SB_ACTION_UP, SB_INPUT_PRESS}, {SB_ACTION_LEFT, SB_INPUT_PRESS},
    {SB_ACTION_RIGHT, SB_INPUT_PRESS}, {SB_ACTION_RIGHT, SB_INPUT_REPEAT},
    {SB_ACTION_RIGHT, SB_INPUT_REPEAT}, {SB_ACTION_RIGHT, SB_INPUT_REPEAT},
    {SB_ACTION_RIGHT, SB_INPUT_REPEAT}, {SB_ACTION_RIGHT, SB_INPUT_REPEAT},
    {SB_ACTION_DOWN, SB_INPUT_PRESS}, {SB_ACTION_DOWN, SB_INPUT_REPEAT},
    {SB_ACTION_DOWN, SB_INPUT_REPEAT}, {SB_ACTION_DOWN, SB_INPUT_REPEAT},
    {SB_ACTION_CONFIRM, SB_INPUT_RELEASE}, {SB_ACTION_NEXT_PANEL, SB_INPUT_PRESS},
    {SB_ACTION_DEPLOY_ROTATE, SB_INPUT_PRESS}, {SB_ACTION_SELECT, SB_INPUT_PRESS},
    {SB_ACTION_CONFIRM, SB_INPUT_PRESS}, {SB_ACTION_FIRE, SB_INPUT_PRESS},
    {SB_ACTION_ROTATE, SB_INPUT_PRESS}, {SB_ACTION_HELP, SB_INPUT_PRESS},
    {SB_ACTION_COMMIT, SB_INPUT_PRESS}, {SB_ACTION_RADIO_STATUS, SB_INPUT_PRESS},
    {SB_ACTION_SURRENDER, SB_INPUT_PRESS}, {SB_ACTION_CANCEL, SB_INPUT_PRESS},
};
static void record(char c) { size_t n = strlen(trace); trace[n] = c; trace[n+1] = 0; }
static int frame(char c) { record(c); return ++frame_calls == fail_frame ? ESP_ERR_TIMEOUT : ESP_OK; }
esp_err_t sb_platform_init(void) { record('B'); return bsp_result; }
esp_err_t sb_renderer_init(void) { record('D'); return display_result; }
esp_err_t sb_input_init(void) { record('I'); ++input_calls; return input_result; }
esp_err_t sb_renderer_loading(const char *stage, unsigned percent) {
    assert(stage); assert(percent <= 100); return frame('L');
}
sb_font_profile_t sb_font_profile_parse(const char *name) {
    if (!strcmp(name,"small")) return SB_FONT_SMALL;
    if (!strcmp(name,"large")) return SB_FONT_LARGE;
    return SB_FONT_MEDIUM;
}
const char *sb_font_profile_name(sb_font_profile_t p) {
    const char *names[]={"small","medium","large"}; assert(p<3); return names[p];
}
void sb_renderer_set_font_profile(const char *name) { assert(name); }
bool sb_config_set(sb_config_t *c,const char *section,const char *key,const char *value) {
    assert(!strcmp(section,"display") && !strcmp(key,"font_size"));
    strcpy(c->settings.font_size,value); c->dirty=true; ++profile_changes; return true;
}
esp_err_t sb_renderer_tactical_preview(unsigned page, unsigned cursor, bool enemy) {
    (void)enemy; assert(cursor < 100);
    assert(now_us >= 3000000); assert(config_loads==1); assert(page<SB_UI_PREVIEW_PAGES);
    if (frame_calls==3) return frame('R');
    ++renders; last_page=page; last_cursor=cursor; last_enemy=enemy;
    return test_frame_failure ? ESP_ERR_TIMEOUT : ESP_OK;
}
esp_err_t sb_renderer_exit(void) { ++exits; int err=frame('E'); exit_ok=err==ESP_OK; exit_presented_us=now_us; return err; }
esp_err_t sb_renderer_message(const char *message) { assert(message); return frame('F'); }
sb_sd_mount_result_t sb_platform_sd_mount(void) {
    record('S'); ++sd_calls; now_us += startup_ms * 1000LL; return (sb_sd_mount_result_t){sd_result, sd_pending};
}
void sb_config_defaults(sb_config_t *c) { memset(c,0,sizeof(*c)); }
void sb_storage_load(sb_storage_t *s,sb_config_t *c,bool mounted) {
    assert(sd_calls==1 && !sd_pending); ++config_loads;
    s->mounted=mounted; c->dirty=true; unavailable_message=!mounted || config_failure;
}
const char *sb_storage_status_text(const sb_storage_t *s) { return s->mounted && !config_failure ? "available" : "unavailable"; }
bool sb_storage_save(sb_storage_t *s,sb_config_t *c) {
    (void)s; assert(sd_cleanup==0); assert(!c->dirty || config_loads==1); ++config_saves; return !config_failure;
}
esp_err_t sb_platform_sd_shutdown(void) { assert(config_saves==1); record('C'); ++sd_cleanup; now_us += cleanup_ms * 1000LL; return cleanup_result; }
void sb_input_reset(void) { ++resets; }
const char *sb_input_action_name(sb_action_t a) { (void)a; return "mock"; }
bool sb_input_poll(sb_input_event_t *e) {
    record('W'); ++waits;
    if (cover_test) {
        static const sb_action_t actions[]={SB_ACTION_SURRENDER,SB_ACTION_SURRENDER,
            SB_ACTION_SURRENDER,SB_ACTION_SURRENDER,SB_ACTION_SELECT,SB_ACTION_DEPLOY_ROTATE,
            SB_ACTION_NEXT_PANEL,SB_ACTION_RIGHT,SB_ACTION_FIRE,SB_ACTION_CONFIRM,SB_ACTION_CANCEL};
        assert(script_pos < sizeof(actions)/sizeof(actions[0]));
        *e=(sb_input_event_t){actions[script_pos++],SB_INPUT_PRESS};
    } else if (burst) {
        if (script_pos < 70) {
            const sb_action_t actions[] = {SB_ACTION_CONFIRM, SB_ACTION_FIRE, SB_ACTION_ROTATE};
            *e=(sb_input_event_t){actions[script_pos++ % 3], SB_INPUT_PRESS};
        } else if (!pause_sent) { pause_sent=true; return false; }
        else { *e=(sb_input_event_t){SB_ACTION_CANCEL, SB_INPUT_PRESS}; ++script_pos; }
    } else if(scripted) {
        assert(script_pos < sizeof(script)/sizeof(script[0]));
        if (script_pos == 23 && !pause_sent) { pause_sent=true; return false; }
        *e=script[script_pos++];
    }
    else *e=(sb_input_event_t){SB_ACTION_CANCEL,SB_INPUT_PRESS};
    return true;
}
void sb_renderer_shutdown(void) { assert(sd_cleanup == 1); if(exit_ok) assert(now_us-exit_presented_us>=3000000); record('X'); ++frame_cleanup; }
void sb_platform_exit(void) { assert(frame_cleanup == 1); record('Q'); longjmp(exit_point, 1); }
int64_t esp_timer_get_time(void) { return now_us; }
void vTaskDelay(unsigned ticks) { delay_ms += ticks; now_us += ticks * 1000LL; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock"; }
static void run(int b, int d, int i, int s, int fail,
                const char *expected, bool ready) {
    bsp_result=b; display_result=d; input_result=i; sd_result=s; fail_frame=fail;
    input_calls=sd_calls=frame_calls=waits=delay_ms=sd_cleanup=frame_cleanup=exits=0;
    config_loads=config_saves=0;
    trace[0]=0; unavailable_message=false; now_us=0; exit_ok=false;
    if (!setjmp(exit_point)) spacebattles_run();
    if (strcmp(trace, expected)) fprintf(stderr, "expected %s, got %s\n", expected, trace);
    assert(!strcmp(trace, expected));
    assert(waits == (ready ? 1 : 0));
    int hold_ms = exit_ok && cleanup_ms < 3000 ? 3000-cleanup_ms+1 : 0;
    int splash_hold_ms = strchr(trace, 'R') && startup_ms < 3000 ? 3000-startup_ms+1 : 0;
    assert(delay_ms == (ready ? 0 : 2000) + hold_ms + splash_hold_ms);
    assert(exits == (d == ESP_OK ? 1 : 0));
}
int main(void) {
    run(0, 0, 0, 0, 0, "BDLILSLRWECXQ", true);
    run(0, 0, 0, -1, 0, "BDLILSLRWECXQ", true);
    assert(unavailable_message);
    run(-1, -1, 0, 0, 0, "BDCXQ", false);
    run(-1, 0, 0, 0, 0, "BDFECXQ", false);
    run(0, -1, 0, 0, 0, "BDCXQ", false);
    run(0, 0, -1, 0, 0, "BDLIFECXQ", false);
    run(0, 0, 0, ESP_ERR_TIMEOUT, 0, "BDLILSLRWECXQ", true);
    assert(unavailable_message);
    sd_pending=true;
    run(0, 0, 0, ESP_ERR_TIMEOUT, 0, "BDLILSFECXQ", false);
    run(0, 0, 0, ESP_OK, 0, "BDLILSFECXQ", false);
    sd_pending=false;
    run(0, 0, 0, 0, 1, "BDLFECXQ", false);
    run(0, 0, 0, 0, 2, "BDLILFECXQ", false);
    run(0, 0, 0, 0, 3, "BDLILSLFECXQ", false);
    run(0, 0, 0, 0, 4, "BDLILSLRFECXQ", false);
    run(0, 0, 0, 0, 5, "BDLILSLRWECXQ", true); // Exit transfer failure still cleans up.
    cleanup_result=ESP_ERR_TIMEOUT;
    run(0, 0, 0, 0, 0, "BDLILSLRWECXQ", true);
    cleanup_result=ESP_OK;
    cleanup_ms=1200;
    run(0, 0, 0, 0, 0, "BDLILSLRWECXQ", true);
    cleanup_ms=3500;
    run(0, 0, 0, 0, 0, "BDLILSLRWECXQ", true);
    cleanup_ms=0;
    startup_ms=2000;
    run(0, 0, 0, 0, 0, "BDLILSLRWECXQ", true);
    startup_ms=6000;
    run(0, 0, 0, 0, 0, "BDLILSLRWECXQ", true);
    config_failure=true;
    run(0, 0, 0, 0, 0, "BDLILSLRWECXQ", true);
    assert(unavailable_message);
    run(0, 0, 0, 0, 4, "BDLILSLRFECXQ", false);
    scripted=true; config_failure=false; startup_ms=0; fail_frame=0;
    renders=resets=script_pos=0; trace[0]=0; frame_cleanup=sd_cleanup=0; config_saves=config_loads=sd_calls=frame_calls=0;
    if (!setjmp(exit_point)) spacebattles_run();
    assert(script_pos==24 && renders==5 && resets==7);
    assert(last_page==1 && profile_changes==1);
    assert(last_cursor==46 && last_enemy);
    assert(frame_cleanup==1 && sd_cleanup==1);
    test_frame_failure=true; renders=script_pos=0; trace[0]=0;
    frame_cleanup=sd_cleanup=0; config_saves=config_loads=sd_calls=frame_calls=0;
    if (!setjmp(exit_point)) spacebattles_run();
    assert(script_pos==14 && renders==1 && frame_cleanup==1 && sd_cleanup==1);
    // A burst of gameplay keys is ignored by the preview; Cancel still exits.
    burst=true; test_frame_failure=false; pause_sent=false;
    renders=script_pos=0; trace[0]=0;
    frame_cleanup=sd_cleanup=0; config_saves=config_loads=sd_calls=frame_calls=0;
    if (!setjmp(exit_point)) spacebattles_run();
    assert(script_pos==71 && renders==0);
    assert(frame_cleanup==1 && sd_cleanup==1);
    cover_test=true; burst=false; renders=script_pos=0; trace[0]=0;
    frame_cleanup=sd_cleanup=0; config_saves=config_loads=sd_calls=frame_calls=0;
    if (!setjmp(exit_point)) spacebattles_run();
    assert(script_pos==11 && renders==4 && last_page==4 && last_cursor==0 && !last_enemy);
    assert(frame_cleanup==1 && sd_cleanup==1);
    puts("PASS: 21 lifecycle cases plus M6 cursor/view/profile/page controls, ignored fire keys, transition resets and display-failure cleanup (mock hardware)");
}

''')
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-variable", "-I", str(path), "-I",
                    str(root / "components/spacebattles"), str(path / "test.c"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
