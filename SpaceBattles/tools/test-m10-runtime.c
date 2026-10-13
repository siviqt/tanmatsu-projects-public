// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app/local_run.c"
esp_err_t sb_versus_run(sb_config_t *c,bool h,sb_ruleset_t r,sb_storage_t *s) { (void)s; (void)c; (void)h; (void)r; return ESP_FAIL; }
#include "platform/settings.c"
#ifdef M10_NATIVE_RENDER
static pax_buf_t framebuffer;
static void menu_layout(const sb_menu_view_t *v) {
    for(unsigned p=0;p<3;++p) {
        if(!sb_ui_menu(&framebuffer,p,v)) { fprintf(stderr,"Runtime layout failed: %s profile %u\n",v->title,p); assert(0); }
    }
}
#endif
static sb_input_event_t script[128];
static unsigned count,pos,saves,applies,boards,covers,resets,settings_frames;
static unsigned idle_polls, idle_frames, total_frames;
static int64_t idle_until;
static int64_t now;
static bool fail_save,fail_apply,fail_draw,saw_failure,saw_success,delay_settings,single_test;
static sb_config_t config;
static uint32_t seed;
static void tap(sb_action_t a) { script[count++]=(sb_input_event_t){a,SB_INPUT_PRESS}; }
static void space(void) { tap(SB_ACTION_FIRE); script[count++]=(sb_input_event_t){SB_ACTION_FIRE,SB_INPUT_RELEASE}; }
static void logoff(void) { tap(SB_ACTION_CANCEL);tap(SB_ACTION_CONFIRM); }
static void fresh(void) {
    idle_polls=idle_frames=total_frames=0;
    count=pos=saves=applies=boards=covers=resets=settings_frames=0; now=0;seed=9;
    fail_save=fail_apply=fail_draw=saw_failure=saw_success=delay_settings=single_test=false;
    sb_config_defaults(&config);
}
uint32_t esp_random(void) { return seed=seed*1664525+1013904223; }
int64_t esp_timer_get_time(void) { return now; }
const char *esp_err_to_name(int e) { (void)e; return "mock"; }
void vTaskDelay(unsigned t) { now+=(int64_t)t*1000; }
void sb_input_set_text_mode(bool enabled) { (void)enabled; }
void sb_input_reset(void) { ++resets; }
bool sb_input_poll(sb_input_event_t *e) {
    // Idle every screen for two seconds, then deliver the original controls.
    // No redraw or settings write is justified by the passage of time alone.
    if(!idle_polls) { idle_frames=total_frames; idle_until=now+2000000; }
    else assert(total_frames==idle_frames);
    ++idle_polls;
    if(now<idle_until) return false;
    idle_polls=0;
    assert(pos<count); *e=script[pos++]; return true;
}
void sb_renderer_set_font_profile(const char *s) { assert(s && *s); }
esp_err_t bsp_display_set_backlight_brightness(uint8_t v) { assert(v==(config.settings.brightness<10 ? 10 : config.settings.brightness)); ++applies; return fail_apply ? ESP_FAIL : ESP_OK; }
esp_err_t bsp_input_set_backlight_brightness(uint8_t v) { assert(v==(config.settings.keyboard_enabled ? config.settings.keyboard_brightness : 0)); return ESP_OK; }
esp_err_t sb_platform_indicator_settings(bool on,unsigned v) { assert(on==config.settings.leds_enabled && v==config.settings.led_brightness); return ESP_OK; }
esp_err_t bsp_audio_set_volume(float v) { assert(v==(config.settings.sound_enabled ? config.settings.master_volume : 0)); return ESP_OK; }
esp_err_t sb_platform_turn_indicator(int p) { assert(p>=-1 && p<=1); return ESP_OK; }
bool sb_storage_save(sb_storage_t *s,sb_config_t *c) {
    ++saves;
    if(delay_settings) now+=120000000;
    if(fail_save || s->status!=SB_STORAGE_READY) return false;
    c->dirty=false; return true;
}
const char *sb_storage_status_text(const sb_storage_t *s) {
    return s->status==SB_STORAGE_READY ? "SD configuration loaded" : s->status==SB_STORAGE_PROTECTED ? "Config protected - session settings only" : "SD unavailable - settings not saved";
}
esp_err_t sb_renderer_message(const char *s) { (void)s; assert(0); return ESP_FAIL; }
esp_err_t sb_renderer_menu(const sb_menu_view_t *v) {
    ++total_frames;
    assert(v && v->title[0]);
#ifdef M10_NATIVE_RENDER
    menu_layout(v);
#endif
    if(strstr(v->title,"Settings")) { ++settings_frames; if(delay_settings) now+=120000000; }
    if(strstr(v->status,"Save failed") || strstr(v->rows[0],"Save failed")) saw_failure=true;
    if(strstr(v->status,"Settings saved") || strstr(v->rows[0],"Settings saved")) saw_success=true;
    if(fail_apply && strstr(v->status,"could not be applied")) saw_failure=true;
    return fail_draw ? ESP_FAIL : ESP_OK;
}
esp_err_t sb_renderer_handover(bool b,bool deploy,bool first) {
    ++total_frames;
    assert(!b && deploy); (void)first; ++covers;
#ifdef M10_NATIVE_RENDER
    for(unsigned p=0;p<3;++p) assert(sb_ui_handover(&framebuffer,p,b,deploy,first));
#endif
    return ESP_OK;
}
esp_err_t sb_renderer_handover_match(bool b,bool d,bool f,unsigned starter) {
    assert(starter<2); return sb_renderer_handover(b,d,f);
}
esp_err_t sb_renderer_tactical(const sb_ui_tactical_t *v) {
    ++total_frames;
    ++boards; assert(v->deployment && v->own_board);
#ifdef M10_NATIVE_RENDER
    for(unsigned p=0;p<3;++p) assert(sb_ui_tactical(&framebuffer,p,v));
#endif
    if(single_test) { assert(v->ruleset==SB_ESCALATED); assert(v->seconds_remaining==0); }
    return ESP_OK;
}
static void run(sb_storage_status_t status) {
    sb_storage_t storage={.status=status};
    assert(sb_local_run(&config,&storage)==ESP_OK); assert(pos==count && resets);
}
static void home_settings(void) {
    tap(SB_ACTION_DOWN);tap(SB_ACTION_DOWN);tap(SB_ACTION_DOWN);tap(SB_ACTION_CONFIRM); tap(SB_ACTION_DOWN);tap(SB_ACTION_DOWN);tap(SB_ACTION_CONFIRM);
    tap(SB_ACTION_CONFIRM); // Mute
    tap(SB_ACTION_DOWN);tap(SB_ACTION_RIGHT); // Master 85
    tap(SB_ACTION_CANCEL);tap(SB_ACTION_CANCEL); // Save on exit
    logoff();
}
int main(void) {
#ifdef M10_NATIVE_RENDER
    assert(pax_buf_init(&framebuffer,NULL,800,480,PAX_BUF_32_8888ARGB));
#endif
    fresh(); config.settings.brightness=0;
    config.settings.keyboard_enabled=false; config.settings.leds_enabled=false;
    assert(sb_platform_apply_settings(&config.settings)==ESP_OK); // Hardware clamp and off-state routing.
    config.settings.keyboard_enabled=true; config.settings.keyboard_brightness=35;
    config.settings.leds_enabled=true; config.settings.led_brightness=25;
    assert(sb_platform_apply_settings(&config.settings)==ESP_OK);
    fresh(); home_settings();run(SB_STORAGE_READY);
    assert(saves==1 && applies==3 && !config.dirty && !config.settings.sound_enabled && config.settings.master_volume==85 && !saw_success);
    fresh();fail_save=true;home_settings();run(SB_STORAGE_READY);assert(saves==1 && config.dirty && saw_failure);
    fresh();home_settings();run(SB_STORAGE_PROTECTED);assert(config.dirty && saw_failure);
    fresh();home_settings();run(SB_STORAGE_UNAVAILABLE);assert(config.dirty && saw_failure);
    // Reset only saves after explicit confirmation, and save failure remains visible.
    fresh();tap(SB_ACTION_DOWN);tap(SB_ACTION_DOWN);tap(SB_ACTION_DOWN);tap(SB_ACTION_CONFIRM);tap(SB_ACTION_UP);tap(SB_ACTION_CONFIRM);tap(SB_ACTION_CONFIRM);tap(SB_ACTION_CANCEL);
    tap(SB_ACTION_CONFIRM);tap(SB_ACTION_CONFIRM);tap(SB_ACTION_CANCEL);tap(SB_ACTION_CANCEL);logoff();
    run(SB_STORAGE_READY);assert(saves==2 && !saw_success);
    // Enter Settings during a handover. A fresh press reveals the board immediately.
    fresh();tap(SB_ACTION_DOWN);tap(SB_ACTION_CONFIRM);tap(SB_ACTION_UP);tap(SB_ACTION_RIGHT);tap(SB_ACTION_DOWN);tap(SB_ACTION_CONFIRM);
    tap(SB_ACTION_CANCEL);tap(SB_ACTION_DOWN);tap(SB_ACTION_CONFIRM);tap(SB_ACTION_CANCEL);tap(SB_ACTION_CANCEL);space();
    tap(SB_ACTION_CANCEL);tap(SB_ACTION_UP);tap(SB_ACTION_CONFIRM);logoff();run(SB_STORAGE_READY);
    assert(boards==1 && covers>=2 && saves==1 && settings_frames);
    // During Escalated deployment, change future rules and confirmation, spend
    // minutes in Settings, return to the same board without deployment expiring.
    fresh();single_test=delay_settings=true;
    tap(SB_ACTION_CONFIRM);tap(SB_ACTION_CONFIRM);
    tap(SB_ACTION_CANCEL);tap(SB_ACTION_DOWN);tap(SB_ACTION_CONFIRM);for(unsigned i=0;i<3;++i)tap(SB_ACTION_DOWN);tap(SB_ACTION_CONFIRM);
    tap(SB_ACTION_RIGHT);
    tap(SB_ACTION_CANCEL);tap(SB_ACTION_CANCEL);tap(SB_ACTION_CANCEL);
    tap(SB_ACTION_CANCEL);tap(SB_ACTION_UP);tap(SB_ACTION_CONFIRM);logoff();run(SB_STORAGE_READY);
    assert(boards==2 && !strcmp(config.settings.preferred_ruleset,"classic"));
    fresh();fail_apply=true;logoff();run(SB_STORAGE_READY);assert(saw_failure);
    fresh();fail_draw=true;assert(sb_local_run(&config,NULL)==ESP_FAIL && !pos);
#ifdef M10_NATIVE_RENDER
    // Render the real match dialogs/results, including long worst-case labels.
    sb_local_t session; uint32_t random_seed=2;
    for(unsigned single=0;single<2;++single) for(unsigned rule=1;rule<=2;++rule) {
        if(single) assert(sb_local_begin_single(&session,rule,SB_AI_NORMAL,false,random_word,&random_seed,0));
        else assert(sb_local_begin_shared(&session,rule,false,random_word,&random_seed));
        const sb_local_screen_t screens[]={SB_LOCAL_START,SB_LOCAL_COVER,SB_LOCAL_EXIT,SB_LOCAL_PAUSED,SB_LOCAL_FINISHED};
        fail_draw=false;
        for(unsigned i=0;i<sizeof(screens)/sizeof(screens[0]);++i) {
            session.screen=screens[i];session.review_result=session.screen==SB_LOCAL_COVER;session.cursor=99;session.match.selected[0]=5;session.match.allowance[0]=5;
            for(unsigned outcome=SB_WIN_A;outcome<=SB_ABANDONED;++outcome) {
                session.match.outcome=outcome;session.match.clock_expired=rule==2;
                session.match.end_reason=outcome==SB_ABANDONED ? SB_END_ABORTED : SB_END_COMBAT;
                assert(draw(&session)==ESP_OK);
                if(session.screen==SB_LOCAL_FINISHED) {
                    pax_join();
                    char name[64]; snprintf(name,sizeof(name),"result-%u-%u-%u.ppm",single,rule,outcome);
                    FILE *f=fopen(name,"wb"); assert(f); fprintf(f,"P6\n800 480\n255\n");
                    for(int y=0;y<480;++y) for(int x=0;x<800;++x) {
                        pax_col_t p=pax_get_pixel(&framebuffer,x,y); fputc(p>>16,f); fputc(p>>8,f); fputc(p,f);
                    }
                    fclose(f);
                    // Render every valid canonical winner/reason pair, including forfeits.
                    for(unsigned reason=SB_END_COMBAT;reason<=SB_END_ABORTED;++reason) {
                        if(!sb_result_valid(outcome,reason)) continue;
                        session.match.end_reason=reason;
                        assert(draw(&session)==ESP_OK);
                    }
                }
            }
        }
    }
    pax_buf_destroy(&framebuffer);
#endif
    puts("M10 runtime: settings application, save/reset/failure feedback, protected/missing SD, private handover and paused deployment passed");
}
