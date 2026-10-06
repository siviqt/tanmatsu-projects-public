// SPDX-License-Identifier: MIT
#include "app/menu.h"
#include "app/local.h"
#include "render/ui.h"
#include "services/preferences.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static sb_menu_result_t key(sb_menu_t *m,sb_config_t *c,sb_action_t a) {
    return sb_menu_event(m,c,(sb_input_event_t){a,SB_INPUT_PRESS});
}
static uint32_t rng(void *p) { uint32_t *n=p; return *n=*n*1664525+1013904223; }
static void behavior(void) {
    sb_config_t c; sb_config_defaults(&c);
    sb_menu_t m;
    sb_config_set(&c,"gameplay","ai_difficulty","hard");
    sb_menu_init(&m,&c,false); assert(m.difficulty==SB_AI_NORMAL);
    sb_config_defaults(&c);
    // Six supported setup combinations; only local modes can start.
    for(unsigned type=0;type<3;++type) for(unsigned rule=0;rule<2;++rule) {
        sb_menu_init(&m,&c,false); m.row=type;
        sb_menu_result_t r=key(&m,&c,SB_ACTION_CONFIRM);
        assert(r==SB_MENU_STAY);
        if(type==2) {
            assert(m.page==SB_MENU_VERSUS); key(&m,&c,SB_ACTION_CONFIRM);
            assert(m.page==SB_MENU_RULES); m.row=rule; key(&m,&c,SB_ACTION_CONFIRM);
            assert(m.page==SB_MENU_NETWORK && key(&m,&c,SB_ACTION_CONFIRM)==SB_MENU_STAY);
        } else {
            assert(m.page==SB_MENU_SETUP && m.row==(type==1 ? 1 : 2));
            m.row=0; if((m.rules==SB_ESCALATED)!=rule) key(&m,&c,SB_ACTION_RIGHT);
            m.row=type==1 ? 1 : 2; assert(key(&m,&c,SB_ACTION_CONFIRM)==SB_MENU_START);
        }
        assert(m.rules==(rule ? SB_ESCALATED : SB_CLASSIC));
        assert(!c.dirty && !strcmp(c.settings.preferred_ruleset,"escalated"));
    }
    sb_menu_init(&m,&c,false); m.row=2; key(&m,&c,SB_ACTION_CONFIRM); m.row=1;
    key(&m,&c,SB_ACTION_CONFIRM); assert(m.page==SB_MENU_NETWORK && !m.host);
    assert(key(&m,&c,SB_ACTION_CANCEL)==SB_MENU_STAY && m.page==SB_MENU_VERSUS);
    // Every editable setting uses the validated canonical path.
    for(unsigned cat=0;cat<4;++cat) {
        unsigned counts[]={8,2,4,2};
        for(unsigned row=0;row<counts[cat];++row) {
            sb_menu_init(&m,&c,false); m.page=SB_MENU_CATEGORY; m.category=cat; m.row=row;
            key(&m,&c,cat==0 && row==7 ? SB_ACTION_LEFT : SB_ACTION_RIGHT); assert(m.changed);
            char text[4097]; sb_config_t d; assert(sb_config_serialize(&c,text,sizeof(text)));
            assert(sb_config_parse(&d,text)==SB_CONFIG_VALID);
            assert(!memcmp(&c.settings,&d.settings,sizeof(c.settings)));
        }
    }
    sb_menu_init(&m,&c,false); m.page=SB_MENU_CATEGORY; m.category=0; m.row=0;
    unsigned brightness=c.settings.brightness;
    key(&m,&c,SB_ACTION_CONFIRM); assert(c.settings.brightness==brightness && !m.changed);
    for(unsigned i=0;i<30;++i) key(&m,&c,SB_ACTION_LEFT);
    assert(c.settings.brightness==10);
    m.row=5; brightness=c.settings.keyboard_brightness;
    key(&m,&c,SB_ACTION_CONFIRM); assert(c.settings.keyboard_brightness==brightness);
    m.row=7; brightness=c.settings.led_brightness;
    key(&m,&c,SB_ACTION_CONFIRM); assert(c.settings.led_brightness==brightness);
    sb_menu_view_t quiet; sb_menu_init(&m,&c,false);
    sb_menu_view(&m,&c,"SD configuration loaded",&quiet); assert(!quiet.status[0]);
    sb_config_defaults(&c); sb_menu_init(&m,&c,false); m.page=SB_MENU_CATEGORY; m.category=2;
    assert(sb_audio_gain(&c.settings,false)==64);
    key(&m,&c,SB_ACTION_CONFIRM); assert(!c.settings.sound_enabled && sb_audio_gain(&c.settings,false)==0 && sb_audio_gain(&c.settings,true)==0);
    key(&m,&c,SB_ACTION_CONFIRM); assert(c.settings.sound_enabled && c.settings.master_volume==80 && c.settings.effects_volume==80 && c.settings.music_volume==60);
    c.settings.effects_volume=0; assert(sb_audio_gain(&c.settings,false)==0);
    c.settings.master_volume=0; assert(sb_audio_gain(&c.settings,true)==0);
    c.settings.power_saving=true; assert(sb_frame_interval_ms(&c.settings)==34);
    c.settings.power_saving=false; strcpy(c.settings.mode,"60"); assert(sb_frame_interval_ms(&c.settings)==17);
    c.settings.reduced_animations=true; assert(sb_animation_level(&c.settings)==1);
    strcpy(c.settings.animation_quality,"off"); assert(sb_animation_level(&c.settings)==0);
    sb_menu_init(&m,&c,false); m.page=SB_MENU_RESET;
    key(&m,&c,SB_ACTION_CANCEL); assert(c.settings.master_volume==0);
    m.page=SB_MENU_RESET; assert(key(&m,&c,SB_ACTION_CONFIRM)==SB_MENU_SAVE && c.settings.master_volume==80 && c.dirty);
    m.page=SB_MENU_CATEGORY; m.category=4; key(&m,&c,SB_ACTION_CONFIRM);
    assert(m.page==SB_MENU_CALLSIGN && m.select_all);
    key(&m,&c,SB_ACTION_TEXT_BASE+'A'); key(&m,&c,SB_ACTION_TEXT_BASE+'9');
    key(&m,&c,SB_ACTION_HOME); key(&m,&c,SB_ACTION_DELETE);
    assert(!strcmp(m.callsign,"9")); key(&m,&c,SB_ACTION_END); key(&m,&c,SB_ACTION_TEXT_BASE+'Z');
    key(&m,&c,SB_ACTION_BACKSPACE); key(&m,&c,SB_ACTION_CONFIRM);
    assert(!strcmp(c.settings.callsign,"9"));
    key(&m,&c,SB_ACTION_CONFIRM); key(&m,&c,SB_ACTION_TEXT_BASE+'R'); key(&m,&c,SB_ACTION_CANCEL);
    assert(!strcmp(c.settings.callsign,"9"));
    m.page=SB_MENU_SETTINGS; assert(key(&m,&c,SB_ACTION_CANCEL)==SB_MENU_SAVE && m.page==SB_MENU_MAIN);
    key(&m,&c,SB_ACTION_CANCEL); assert(m.page==SB_MENU_LOGOFF);
    key(&m,&c,SB_ACTION_CANCEL); assert(m.page==SB_MENU_MAIN);
    m.page=SB_MENU_LOGOFF; assert(key(&m,&c,SB_ACTION_CONFIRM)==SB_MENU_EXIT);
    // Settings never expose covered boards and reset partial handover presses.
    sb_local_t s; uint32_t seed=9;
    assert(sb_local_begin(&s,false,rng,&seed));
    sb_local_event(&s,(sb_input_event_t){SB_ACTION_CONFIRM,SB_INPUT_PRESS});
    assert(sb_local_open_settings(&s));
    sb_ui_tactical_t v; memset(&v,0xff,sizeof(v)); assert(!sb_local_project(&s,&v));
    assert(!v.occupied[0] && s.presses==0 && !s.space_down && sb_local_indicator(&s)==0);
    sb_local_event(&s,(sb_input_event_t){SB_ACTION_ENEMY_VIEW,SB_INPUT_PRESS});
    assert(s.screen==SB_LOCAL_SETTINGS);
    sb_local_close_settings(&s); assert(s.screen==SB_LOCAL_COVER && s.presses==0 && s.reset_input);
    for(unsigned n=0;n<1;++n) {
        sb_local_event(&s,(sb_input_event_t){SB_ACTION_FIRE,SB_INPUT_PRESS});
        sb_local_event(&s,(sb_input_event_t){SB_ACTION_FIRE,SB_INPUT_RELEASE});
        assert(s.screen==SB_LOCAL_DEPLOY);
    }
    // Single Player clocks exclude time inside Settings, including the summary.
    assert(sb_local_begin_single(&s,SB_ESCALATED,SB_AI_NORMAL,false,rng,&seed,100));
    assert(sb_local_open_settings(&s)); sb_local_tick(&s,1000000); sb_local_close_settings(&s);
    assert(s.screen==SB_LOCAL_START && !s.match.paused);
    sb_local_event(&s,(sb_input_event_t){SB_ACTION_CONFIRM,SB_INPUT_PRESS});
    sb_local_tick(&s,1001000); uint64_t elapsed=s.match.now_ms-s.match.phase_started_ms;
    assert(sb_local_open_settings(&s)); sb_spec_t spec=s.match.spec;
    sb_local_tick(&s,2000000); assert(s.match.phase==SB_DEPLOYMENT && s.match.paused);
    assert(s.match.now_ms-s.match.phase_started_ms==elapsed && !memcmp(&spec,&s.match.spec,sizeof(spec)));
    sb_local_close_settings(&s); assert(s.screen==SB_LOCAL_DEPLOY && !s.match.paused);
    sb_local_event(&s,(sb_input_event_t){SB_ACTION_CANCEL,SB_INPUT_PRESS});
    assert(s.screen==SB_LOCAL_EXIT && s.match.paused);
    assert(sb_local_open_settings(&s)); sb_local_tick(&s,3000000); sb_local_close_settings(&s);
    assert(s.screen==SB_LOCAL_EXIT && s.match.paused);
    sb_local_event(&s,(sb_input_event_t){SB_ACTION_HELP,SB_INPUT_PRESS});
    assert(s.screen==SB_LOCAL_HELP && s.match.paused);
    sb_local_event(&s,(sb_input_event_t){SB_ACTION_CANCEL,SB_INPUT_PRESS});
    assert(s.screen==SB_LOCAL_EXIT && s.match.paused);
    sb_local_event(&s,(sb_input_event_t){SB_ACTION_CANCEL,SB_INPUT_PRESS});
    assert(s.screen==SB_LOCAL_DEPLOY && !s.match.paused);
    assert(!sb_local_open_settings(NULL));
    s.screen=SB_LOCAL_RESULT; assert(!sb_local_open_settings(&s));
    puts("M10 behavior: six setup combinations, settings, reset, callsign, mute, policies, privacy and pause passed");
}
static void render(void) {
    pax_buf_t b; assert(pax_buf_init(&b,NULL,800,480,PAX_BUF_32_8888ARGB));
    void *expected=malloc(800*480*4); assert(expected);
    sb_config_t c; sb_config_defaults(&c);
    const char *statuses[]={"SD configuration loaded","SD unavailable - settings not saved","SD config error - settings not saved","Config protected - session settings only"};
    for(unsigned profile=0;profile<3;++profile) for(unsigned page=0;page<=SB_MENU_LOGOFF;++page) {
        unsigned variations=page==SB_MENU_CATEGORY ? 7 : page==SB_MENU_HELP ? 5 : 2;
        for(unsigned variant=0;variant<variations;++variant) {
            sb_menu_t m; sb_menu_init(&m,&c,false); m.page=page; m.category=variant%7; m.help_page=variant%5; m.host=variant!=0; m.in_match=variant!=0;
            if(page==SB_MENU_SETUP && variant) m.play_type=SB_SHARED_DEVICE;
            m.rules=variant ? SB_ESCALATED : SB_CLASSIC; strcpy(m.callsign,"WWWWWWWWWWWWWWWW");
            strcpy(m.feedback,page==SB_MENU_SAVED ? "Save failed - session changes kept. Retry on exit." : "");
            sb_menu_view_t v; sb_menu_view(&m,&c,statuses[variant%4],&v);
            pax_background(&b,0xffff0000);
            if(!sb_ui_menu(&b,profile,&v)) { fprintf(stderr,"Layout failed: profile=%u page=%u variant=%u title=%s\n",profile,page,variant,v.title); exit(1); }
            pax_join(); memcpy(expected,b.buf,800*480*4);
            pax_background(&b,0xff00ff00); assert(sb_ui_menu(&b,profile,&v)); pax_join();
            assert(!memcmp(expected,b.buf,800*480*4));
            if(profile==SB_FONT_LARGE) {
                char file[64]; snprintf(file,sizeof(file),"menu-%u-%u.ppm",page,variant);
                FILE *f=fopen(file,"wb"); assert(f); fprintf(f,"P6\n800 480\n255\n");
                for(int y=0;y<480;++y) for(int x=0;x<800;++x) { pax_col_t p=pax_get_pixel(&b,x,y); fputc(p>>16,f);fputc(p>>8,f);fputc(p,f); }
                fclose(f);
            }
        }
    }
    for(unsigned profile=0;profile<3;++profile) {
        sb_menu_t menu; sb_menu_init(&menu,&c,false); menu.page=SB_MENU_CATEGORY; menu.category=0; menu.row=7;
        sb_menu_view_t view; sb_menu_view(&menu,&c,"SD configuration loaded",&view);
        assert(view.count==8 && view.selected==7 && !view.status[0]);
        assert(sb_ui_menu(&b,profile,&view)); pax_join();
        if(profile==SB_FONT_LARGE) {
            FILE *f=fopen("display-lighting.ppm","wb"); assert(f); fprintf(f,"P6\n800 480\n255\n");
            for(unsigned y=0;y<480;++y) for(unsigned x=0;x<800;++x) { pax_col_t pixel=pax_get_pixel(&b,x,y); fputc(pixel>>16,f); fputc(pixel>>8,f); fputc(pixel,f); }
            fclose(f);
        }
        assert(sb_ui_handover(&b,profile,false,false,false)); pax_join();
        memcpy(expected,b.buf,800*480*4);
        assert(sb_ui_handover(&b,profile,false,false,true)); pax_join();
        assert(!memcmp(expected,b.buf,800*480*4)); // No release-prompt frame remains.
    }
    free(expected); pax_buf_destroy(&b);
    puts("M10 rendering: menus, settings and help fit all font sizes; opaque overlays passed");
}
static void dual_boards(void) {
    pax_buf_t buf; assert(pax_buf_init(&buf,NULL,800,480,PAX_BUF_32_8888ARGB));
    uint32_t seed=123;
    for(unsigned rules=SB_CLASSIC;rules<=SB_ESCALATED;++rules) {
        sb_local_t s; assert(sb_local_begin_single(&s,rules,SB_AI_NORMAL,false,rng,&seed,0));
        sb_completion_t completion;
        for(unsigned player=0;player<2;++player) {
            assert(sb_complete(&s.match.fleets[player],SB_PLACEMENT_BUDGET,rng,&seed,&completion));
            assert(sb_match_ready(&s.match,player));
        }
        s.screen=SB_LOCAL_TARGET;
        sb_ui_tactical_t first,second; assert(sb_local_project(&s,&first));
        assert(first.dual && first.view_mode==0);
        // Modifying an undisclosed opponent fleet cannot change the projection.
        sb_fleet_t hidden=s.match.fleets[1]; memset(&s.match.fleets[1],0,sizeof(hidden));
        assert(sb_local_project(&s,&second)); assert(!memcmp(&first,&second,sizeof(first)));
        s.match.fleets[1]=hidden;
        unsigned before=s.cursor=47;
        sb_local_event(&s,(sb_input_event_t){SB_ACTION_OWN_FLEET,SB_INPUT_PRESS}); assert(s.view_mode==1 && s.cursor==before);
        sb_local_event(&s,(sb_input_event_t){SB_ACTION_ENEMY_VIEW,SB_INPUT_PRESS}); assert(s.view_mode==2 && s.cursor==before);
        sb_local_event(&s,(sb_input_event_t){SB_ACTION_CONTEXT,SB_INPUT_PRESS}); assert(s.view_mode==0 && s.cursor==before);
        if(rules==SB_ESCALATED) {
            uint8_t outgoing=s.match.fleets[1].ships[0].cells[0],incoming=s.match.fleets[0].ships[0].cells[0];
            assert(sb_select(&s.match,0,s.match.sequence,&outgoing,1));
            sb_local_event(&s,(sb_input_event_t){SB_ACTION_OWN_FLEET,SB_INPUT_PRESS});
            assert(s.match.selected[0]==1 && s.match.selection[0][0]==outgoing);
            sb_local_event(&s,(sb_input_event_t){SB_ACTION_CONTEXT,SB_INPUT_PRESS});
            assert(sb_select(&s.match,1,s.match.sequence,&incoming,1));
            assert(sb_escalated_lock(&s.match,0,s.match.sequence,true));
            assert(sb_escalated_lock(&s.match,1,s.match.sequence,true));
            assert(sb_match_resolve(&s.match,s.match.sequence,&s.match.record));
            s.screen=SB_LOCAL_RESULT;
            assert(sb_local_project(&s,&first));
            assert(first.fleet_cells[incoming]==SB_HIT && first.target_cells[outgoing]==SB_HIT);
            assert(first.fleet_occupied[incoming]);
        }
        for(unsigned profile=0;profile<3;++profile) for(unsigned mode=0;mode<3;++mode) {
            assert(sb_local_project(&s,&first)); first.view_mode=mode;
            first.owner_name="WWWWWWWWWWWWWWWW";first.target_name="WWWWWWWWWWWWWWWW";
            first.phase_label="ROUND RESOLVED";first.action_label="Next round in 3...";
            if(!sb_ui_tactical(&buf,profile,&first)) { fprintf(stderr,"Dual layout failed: rules=%u profile=%u view=%u\n",rules,profile,mode); assert(0); }
            bool result=first.result;
            first.result=false; first.sequence=100;
            assert(sb_ui_tactical(&buf,profile,&first));
            first.play_type=SB_UI_SHARED_DEVICE;
            assert(sb_ui_tactical(&buf,profile,&first));
            first.result=result;
            if(profile==SB_FONT_LARGE) {
                first.owner_name="Player A";first.target_name="Player B"; assert(sb_ui_tactical(&buf,profile,&first)); pax_join();
                char name[64];snprintf(name,sizeof(name),"dual-%u-%u.ppm",rules,mode);
                FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
                for(unsigned y=0;y<480;++y) for(unsigned x=0;x<800;++x) { pax_col_t c=pax_get_pixel(&buf,x,y);fputc(c>>16,f);fputc(c>>8,f);fputc(c,f); }
                fclose(f);
            }
        }
    }
    pax_buf_destroy(&buf);
    puts("M10 revision: private dual projection, both salvo directions, view changes, staged-shot retention and native layouts passed");
}
int main(void) { behavior(); render(); dual_boards(); }
