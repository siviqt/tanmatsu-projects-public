// SPDX-License-Identifier: MIT
#include "app/menu.h"
#include "release.h"
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
    sb_config_set(&c,"radio","region","EU868");
    sb_config_set(&c,"radio","manual_region","EU868");
    sb_config_set(&c,"gameplay","ai_difficulty","hard");
    sb_menu_init(&m,&c,false); assert(m.difficulty==SB_AI_HARD);
    char hard_saved[4097]; sb_config_t hard_loaded;
    assert(sb_config_serialize(&c,hard_saved,sizeof(hard_saved)) && sb_config_parse(&hard_loaded,hard_saved)==SB_CONFIG_VALID);
    sb_menu_init(&m,&hard_loaded,false); assert(m.difficulty==SB_AI_HARD);
    m.page=SB_MENU_SETUP; m.row=1;
    key(&m,&c,SB_ACTION_LEFT); assert(m.difficulty==SB_AI_NORMAL);
    key(&m,&c,SB_ACTION_RIGHT); assert(m.difficulty==SB_AI_HARD);
    sb_menu_view_t view; sb_menu_view(&m,&c,"",&view); assert(strstr(view.rows[1],"Hard"));
    key(&m,&c,SB_ACTION_RIGHT); assert(m.difficulty==SB_AI_EASY);
    m.page=SB_MENU_DIFFICULTY; m.row=2;
    sb_menu_view(&m,&c,"",&view); assert(view.count==3 && !strcmp(view.rows[2],"Hard"));
    assert(key(&m,&c,SB_ACTION_CONFIRM)==SB_MENU_START && m.difficulty==SB_AI_HARD);
    sb_config_defaults(&c);
    sb_config_set(&c,"radio","region","EU868");
    sb_config_set(&c,"radio","manual_region","EU868");
    c.dirty=false;
    // Six supported setup combinations; only local modes can start.
    for(unsigned type=0;type<3;++type) for(unsigned rule=0;rule<2;++rule) {
        sb_menu_init(&m,&c,false); m.row=type;
        sb_menu_result_t r=key(&m,&c,SB_ACTION_CONFIRM);
        assert(r==SB_MENU_STAY);
        if(type==2) {
            assert(m.page==SB_MENU_VERSUS); key(&m,&c,SB_ACTION_CONFIRM);
            assert(m.page==SB_MENU_RULES); m.row=rule; key(&m,&c,SB_ACTION_CONFIRM);
            assert(m.page==SB_MENU_NETWORK && key(&m,&c,SB_ACTION_CONFIRM)==SB_MENU_START);
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
    sb_config_defaults(&c); sb_menu_init(&m,&c,false);
    m.page=SB_MENU_CATEGORY; m.category=5; m.row=0;
    assert(!strcmp(c.settings.region,"NONE") && !sb_config_radio_confirmed(&c));
    key(&m,&c,SB_ACTION_RIGHT); assert(m.changed && !strcmp(c.settings.region,"EU868"));
    assert(!sb_config_radio_confirmed(&c));
    m.row=1; key(&m,&c,SB_ACTION_CONFIRM); assert(sb_config_radio_confirmed(&c));
    char saved[SB_CONFIG_TEXT_MAX]; sb_config_t loaded;
    assert(sb_config_serialize(&c,saved,sizeof(saved)));
    assert(sb_config_parse(&loaded,saved)==SB_CONFIG_VALID && sb_config_radio_confirmed(&loaded));
    m.row=0; key(&m,&c,SB_ACTION_LEFT);
    assert(!strcmp(c.settings.region,"US915") && !sb_config_radio_confirmed(&c));
    m.row=1; key(&m,&c,SB_ACTION_CONFIRM); assert(sb_config_radio_confirmed(&c));
    m.in_match=true; m.row=0; key(&m,&c,SB_ACTION_RIGHT);
    assert(!m.changed && !strcmp(c.settings.region,"US915") && sb_config_radio_confirmed(&c));
    m.page=SB_MENU_RESET; key(&m,&c,SB_ACTION_CONFIRM); assert(sb_config_radio_confirmed(&c));
    sb_config_defaults(&c); sb_menu_init(&m,&c,false); m.page=SB_MENU_VERSUS;
    key(&m,&c,SB_ACTION_CONFIRM); assert(m.page==SB_MENU_CATEGORY && m.category==5);
    sb_config_defaults(&c);
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
    sb_local_event(&s,(sb_input_event_t){SB_ACTION_SELECT,SB_INPUT_PRESS});
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
    sb_local_tick(&s,1001000);
    assert(sb_local_open_settings(&s)); sb_spec_t spec=s.match.spec;
    sb_local_tick(&s,2000000); assert(s.match.phase==SB_DEPLOYMENT && s.match.paused);
    assert(!s.match.spec.deployment_ms && !memcmp(&spec,&s.match.spec,sizeof(spec)));
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
static void navigation_regressions(void) {
    sb_config_t c; sb_config_defaults(&c);
    sb_menu_t m; sb_menu_init(&m,&c,false);
    // Both directions cycle all three implemented difficulties.
    m.page=SB_MENU_CATEGORY; m.category=3; m.row=1;
    sb_config_set(&c,"gameplay","ai_difficulty","hard");
    key(&m,&c,SB_ACTION_RIGHT); assert(!strcmp(c.settings.ai_difficulty,"easy"));
    key(&m,&c,SB_ACTION_LEFT); assert(!strcmp(c.settings.ai_difficulty,"hard"));
    key(&m,&c,SB_ACTION_LEFT); assert(!strcmp(c.settings.ai_difficulty,"normal"));
    for(unsigned row=0;row<7;++row) {
        sb_menu_init(&m,&c,false); m.row=row;
        key(&m,&c,SB_ACTION_CONFIRM); key(&m,&c,SB_ACTION_CANCEL);
        assert(m.page==SB_MENU_MAIN && m.row==row);
    }
    for(unsigned row=0;row<3;++row) {
        m.page=SB_MENU_CATEGORY; m.category=6; m.row=row;
        key(&m,&c,SB_ACTION_CONFIRM); key(&m,&c,SB_ACTION_CANCEL);
        assert(m.page==SB_MENU_CATEGORY && m.row==row);
    }
    m.page=SB_MENU_NETWORK; m.host=true; m.rules=SB_ESCALATED;
    key(&m,&c,SB_ACTION_CANCEL); assert(m.page==SB_MENU_RULES && m.row==1);
    key(&m,&c,SB_ACTION_CANCEL); assert(m.page==SB_MENU_VERSUS && m.row==0);
    m.page=SB_MENU_NETWORK; m.host=false;
    key(&m,&c,SB_ACTION_CANCEL); assert(m.page==SB_MENU_VERSUS && m.row==1);
    puts("GUI navigation: main branches, system pages, host/join Back and Hard cycling passed");
}
static void damage_markers(void) {
    pax_buf_t b; assert(pax_buf_init(&b,NULL,800,480,PAX_BUF_32_8888ARGB));
    sb_ui_tactical_t v={.ruleset=SB_CLASSIC,.active=true,.available=1,.cursor=99};
    v.fleet_cells[0]=v.target_cells[0]=SB_HIT;
    v.fleet_cells[1]=v.target_cells[1]=SB_SUNK;
    v.fleet_cells[2]=v.target_cells[2]=SB_MISS;
    assert(sb_ui_tactical(&b,SB_FONT_LARGE,&v)); pax_join();
    assert(pax_get_pixel(&b,69,139)!=pax_get_pixel(&b,469,139));
    assert(pax_get_pixel(&b,84,127)!=pax_get_pixel(&b,484,127));
    assert(pax_get_pixel(&b,123,139)!=pax_get_pixel(&b,523,139)); // Hollow incoming ring versus solid outgoing dot.
    pax_buf_destroy(&b);
    puts("GUI damage: incoming/outgoing hit and sunk colors differ; misses have distinct ring/dot silhouettes");
}
static void render(void) {
    pax_buf_t b; assert(pax_buf_init(&b,NULL,800,480,PAX_BUF_32_8888ARGB));
    pax_buf_t art; assert(pax_buf_init(&art,NULL,800,480,PAX_BUF_32_8888ARGB));
    FILE *source=fopen("menu-art.rgba","rb"); assert(source);
    for(unsigned y=0;y<480;++y) for(unsigned x=0;x<800;++x) {
        unsigned char rgba[4]; assert(fread(rgba,1,4,source)==4);
        pax_set_pixel(&art,0xff000000u | rgba[0]<<16 | rgba[1]<<8 | rgba[2],x,y);
    }
    fclose(source); sb_ui_set_menu_art(&art);
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
            if(page==SB_MENU_ABOUT) assert(!strcmp(v.rows[0],SB_RELEASE_LABEL));
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
    for(unsigned profile=0;profile<3;++profile) {
        sb_menu_view_t discovery={.count=3};
        strcpy(discovery.title,"Versus / Guest");
        strcpy(discovery.rows[0],"Peer found - exchanging keys");
        strcpy(discovery.rows[1],"EU868 | Radio packets: TX 4294967295 / RX 4294967295");
        strcpy(discovery.rows[2],"Receive recoveries: 4294967295");
        strcpy(discovery.footer,"Esc: Cancel");
        assert(sb_ui_menu(&b,profile,&discovery)); pax_join();
    }
    sb_ui_set_menu_art(NULL); pax_buf_destroy(&art);
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
        sb_local_event(&s,(sb_input_event_t){SB_ACTION_DEPLOY_ROTATE,SB_INPUT_PRESS}); assert(s.view_mode==0 && s.cursor==before);
        if(rules==SB_ESCALATED) {
            uint8_t outgoing=s.match.fleets[1].ships[0].cells[0],incoming=s.match.fleets[0].ships[0].cells[0];
            assert(sb_select(&s.match,0,s.match.sequence,&outgoing,1));
            sb_local_event(&s,(sb_input_event_t){SB_ACTION_DEPLOY_ROTATE,SB_INPUT_PRESS});
            assert(s.match.selected[0]==1 && s.match.selection[0][0]==outgoing);

            assert(sb_select(&s.match,1,s.match.sequence,&incoming,1));
            assert(sb_escalated_lock(&s.match,0,s.match.sequence,true));
            assert(sb_escalated_lock(&s.match,1,s.match.sequence,true));
            assert(sb_match_resolve(&s.match,s.match.sequence,&s.match.record));
            s.screen=SB_LOCAL_RESULT;
            assert(sb_local_project(&s,&first));
            assert(first.fleet_cells[incoming]==SB_HIT && first.target_cells[outgoing]==SB_HIT);
            assert(first.fleet_occupied[incoming]);
        }
        for(unsigned profile=0;profile<3;++profile) for(unsigned mode=0;mode<1;++mode) {
            assert(sb_local_project(&s,&first)); first.view_mode=mode;
            first.owner_name="WWWWWWWWWWWWWWWW";first.target_name="WWWWWWWWWWWWWWWW";
            first.phase_label="ROUND RESOLVED";first.action_label="Review results";
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
static void context_keys(void) {
    pax_buf_t b; assert(pax_buf_init(&b,NULL,800,480,PAX_BUF_32_8888ARGB));
    for(unsigned mode=0;mode<3;mode++) for(unsigned rules=SB_CLASSIC;rules<=SB_ESCALATED;rules++) {
        sb_ui_tactical_t v={.play_type=mode,.ruleset=rules,.own_board=true,.deployment=true,
            .can_surrender=true,.link_available=mode==SB_UI_VERSUS,.available=rules==SB_CLASSIC ? 1 : 5};
        unsigned common=1|32|(mode==SB_UI_VERSUS ? 16 : 0);
        assert(sb_ui_tactical_keys(&v)==(common|2|4));
        for(unsigned ship=0;ship<5;ship++) v.ships_placed[ship]=true;
        assert(sb_ui_tactical_keys(&v)==(common|2|4|8));
        v.deployment_locked=true; assert(sb_ui_tactical_keys(&v)==common);
        v.deployment_locked=false;
        v.deployment=false; v.own_board=false; v.dual=true; v.active=true;
        assert(sb_ui_tactical_keys(&v)==(common|4|(rules==SB_ESCALATED ? 8 : 0)));
        v.selected=v.available;
        assert(sb_ui_tactical_keys(&v)==(common|8|(rules==SB_CLASSIC ? 4 : 0)));
        v.targets[0]=true; assert(sb_ui_tactical_keys(&v)==(common|4|8));
        v.active=false; v.can_surrender=false;
        assert(sb_ui_tactical_keys(&v)==(1|(mode==SB_UI_VERSUS ? 16 : 0)));
        v.result=true; assert(sb_ui_tactical_keys(&v)==8);
        for(unsigned profile=0;profile<3;profile++) assert(sb_ui_tactical(&b,profile,&v));
    }
    for(unsigned mask=0;mask<64;mask++) {
        pax_background(&b,0xff080e20);
        assert(sb_ui_keybar(&b,SB_FONT_LARGE,mask)); pax_join();
        for(unsigned k=0;k<6;k++) assert((pax_get_pixel(&b,24+126*k,446)!=pax_get_pixel(&b,24+126*k+35,446))==!!(mask&(1u<<k)));
    }
    pax_buf_destroy(&b);
    puts("Context keys: all modes/rules, readiness, capacity, waiting, results and 64 visibility masks passed");
}
static void snapshot(pax_buf_t *b,const char *name) {
    pax_join();
    char path[96]; snprintf(path,sizeof(path),"cyberdeck-%s.ppm",name);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n800 480\n255\n");
    for(unsigned y=0;y<480;y++) for(unsigned x=0;x<800;x++) {
        pax_col_t c=pax_get_pixel(b,x,y); fputc(c>>16,f);fputc(c>>8,f);fputc(c,f);
    }
    fclose(f);
}
static void cyberdeck(void) {
    pax_buf_t art,cache;
    assert(pax_buf_init(&art,NULL,800,480,PAX_BUF_32_8888ARGB));
    FILE *source=fopen("menu-art.rgba","rb"); assert(source);
    for(unsigned y=0;y<480;y++) for(unsigned x=0;x<800;x++) {
        unsigned char c[4]; assert(fread(c,1,4,source)==4);
        pax_set_pixel(&art,0xff000000u | c[0]<<16 | c[1]<<8 | c[2],x,y);
    }
    fclose(source);
    assert(pax_buf_init(&cache,NULL,800,480,PAX_BUF_32_8888ARGB));
    sb_ui_set_menu_art(&art); sb_ui_prepare_menu_cache(&cache); pax_join();
    sb_ui_set_menu_cache(&cache);
    pax_buf_t b; assert(pax_buf_init(&b,NULL,800,480,PAX_BUF_32_8888ARGB));
    sb_ui_tactical_t v={.ruleset=SB_CLASSIC,.play_type=SB_UI_SINGLE_PLAYER,.own_board=true,
        .deployment=true,.ship_selected=2,.cursor=51,.placement_valid=true,.can_surrender=true,
        .owner_name="COMMANDER",.target_name="AI",.detail_label="Placed 5/5 | You start"};
    sb_config_t config; sb_config_defaults(&config);
    for(unsigned p=SB_FONT_MEDIUM;p<=SB_FONT_LARGE;p++) {
        const sb_menu_page_t pages[]={SB_MENU_MAIN,SB_MENU_SETUP,SB_MENU_SETUP,SB_MENU_RULES};
        const char *names[]={"main","single-setup","shared-setup","rules"};
        for(unsigned i=0;i<4;i++) {
            sb_menu_t state; sb_menu_init(&state,&config,false);
            state.page=pages[i]; state.host=true;
            state.play_type=i==2 ? SB_SHARED_DEVICE : SB_SINGLE_PLAYER;
            state.rules=i==2 ? SB_ESCALATED : SB_CLASSIC;
            state.row=i==1 ? 2 : i==2 ? 1 : 0;
            sb_menu_view_t view; sb_menu_view(&state,&config,"",&view);
            assert(sb_ui_menu(&b,p,&view));
            char name[64]; snprintf(name,sizeof(name),"v3-%s-%s",names[i],sb_font_profile_name(p));
            snapshot(&b,name);
        }
    }
    unsigned starts[]={10,17,51,65,88}, lengths[]={5,4,3,3,2};
    for(unsigned i=0;i<5;i++) {
        v.ships[i].length=lengths[i]; v.ships_placed[i]=true;
        for(unsigned j=0;j<lengths[i];j++) {
            unsigned cell=starts[i]+j*(i==1 || i==3 ? 10 : 1);
            v.ships[i].cells[j]=cell; v.occupied[cell]=v.fleet_occupied[cell]=true;
        }
    }
    for(unsigned j=0;j<3;j++) v.placement[51+j]=true;
    for(unsigned p=0;p<3;p++) {
        assert(sb_ui_tactical(&b,p,&v));
        if(p==SB_FONT_LARGE) snapshot(&b,"deployment");
        if(p==SB_FONT_MEDIUM) snapshot(&b,"v3-deployment-medium");
        v.placement_valid=false;
        assert(sb_ui_tactical(&b,p,&v));
        if(p==SB_FONT_LARGE) snapshot(&b,"deployment-invalid");
        v.placement_valid=true;
    }
    // Valid ghosts leave their interiors readable; invalid ghosts add a cross.
    assert(sb_ui_tactical(&b,SB_FONT_LARGE,&v)); pax_join();
    pax_col_t valid=pax_get_pixel(&b,56+32+16,96+5*32+16);
    v.placement_valid=false; assert(sb_ui_tactical(&b,SB_FONT_LARGE,&v)); pax_join();
    assert(valid!=pax_get_pixel(&b,56+32+16,96+5*32+16));
    // Reject malformed geometry before touching any fleet pixels.
    unsigned saved=v.ships[0].cells[2]; v.ships[0].cells[2]=99;
    assert(!sb_ui_tactical(&b,SB_FONT_LARGE,&v)); pax_join();
    assert(pax_get_pixel(&b,56,96)==0xff080e20); v.ships[0].cells[2]=saved;
    memset(v.placement,0,sizeof(v.placement));
    v.deployment=false;v.active=true;v.ruleset=SB_ESCALATED;v.sequence=6;
    v.available=5;v.selected=3;v.cursor=46;
    v.targets[44]=v.targets[45]=v.targets[46]=true;
    v.fleet_cells[10]=SB_HIT;v.target_cells[23]=SB_HIT;
    v.fleet_cells[22]=v.target_cells[22]=SB_MISS;
    v.target_cells[88]=v.target_cells[89]=SB_SUNK;
    for(unsigned p=0;p<3;p++) assert(sb_ui_tactical(&b,p,&v));
    snapshot(&b,"battle");
    // Shared hull geometry stays beneath damage in both orientations, including sunk ships.
    v.fleet_cells[17]=SB_HIT;
    v.fleet_cells[88]=v.fleet_cells[89]=SB_SUNK;
    assert(sb_ui_tactical(&b,SB_FONT_LARGE,&v)); pax_join();
    assert(pax_get_pixel(&b,56+13,126+27+13)==0xffff8b70);
    assert(pax_get_pixel(&b,56+7*27+13,126+27+13)==0xffff8b70);
    assert(pax_get_pixel(&b,56+8*27+13,126+8*27+13)==0xffff8b70);
    snapshot(&b,"battle-damage");
    // Owner descriptors must not change any pixel on the opponent's board.
    pax_col_t target_pixels[270*270];
    for(unsigned y=0;y<270;y++) for(unsigned x=0;x<270;x++)
        target_pixels[y*270+x]=pax_get_pixel(&b,456+x,126+y);
    sb_ui_ship_t owner_ships[SB_SHIPS]; memcpy(owner_ships,v.ships,sizeof(owner_ships));
    memset(v.ships,0,sizeof(v.ships));
    assert(sb_ui_tactical(&b,SB_FONT_LARGE,&v)); pax_join();
    for(unsigned y=0;y<270;y++) for(unsigned x=0;x<270;x++)
        assert(target_pixels[y*270+x]==pax_get_pixel(&b,456+x,126+y));
    memcpy(v.ships,owner_ships,sizeof(owner_ships));
    // Cursor corners and queued-target inset must both survive on the same cell.
    assert(pax_get_pixel(&b,456+6*27,126+4*27)==0xff65dafa);
    assert(pax_get_pixel(&b,456+6*27+3,126+4*27+3)==0xffffd166);
    v.active=false;v.notice="Salvo locked | Waiting for opponent";
    for(unsigned p=0;p<3;p++) assert(sb_ui_tactical(&b,p,&v));
    snapshot(&b,"waiting");
    v.result=true;v.phase_label="Round resolved";v.action_label="Review results";
    assert(sb_ui_tactical(&b,SB_FONT_LARGE,&v));snapshot(&b,"review");
    sb_menu_view_t menu={.title="Versus / Host",.subtitle="Network stays active",.surface=SB_MENU_SURFACE_MATCH,
        .selectable=true,.count=5,.key_mask=1,.rows={"Resume","Settings","Help","Surrender - opponent wins","Leave match - opponent wins"},
        .footer="Up/Down: Choose   Enter: Select   Esc: Resume"};
    for(unsigned p=0;p<3;p++) for(unsigned row=0;row<5;row++) {
        menu.selected=row; assert(sb_ui_menu(&b,p,&menu));
    }
    menu.selected=0;
    assert(sb_ui_menu(&b,SB_FONT_MEDIUM,&menu));snapshot(&b,"v3-match-menu-medium");
    assert(sb_ui_menu(&b,SB_FONT_LARGE,&menu));snapshot(&b,"match-menu");
    menu.selected=3;menu.disabled_rows=8;strcpy(menu.rows[3],"Surrender unavailable after submission");
    assert(sb_ui_menu(&b,SB_FONT_LARGE,&menu));snapshot(&b,"match-locked");
    sb_menu_view_t disconnected={.title="Opponent disconnected",.surface=SB_MENU_SURFACE_STATUS,
        .tone=SB_TONE_PENDING,.count=2,
        .rows={"The other player left the session.","Opponent abandoned - you win"},
        .footer="Returning to main menu..."};
    for(unsigned p=0;p<3;p++) assert(sb_ui_menu(&b,p,&disconnected));
    snapshot(&b,"disconnected");
    strcpy(disconnected.title,"Connection lost"); disconnected.tone=SB_TONE_ERROR;
    disconnected.count=1; strcpy(disconnected.rows[0],"Link ended - no winner");
    for(unsigned p=0;p<3;p++) assert(sb_ui_menu(&b,p,&disconnected));
    snapshot(&b,"connection-lost");
    // Final-screen peer consent is live text and fits the largest font.
    sb_menu_view_t rematch={.title="Versus / Guest",.surface=SB_MENU_SURFACE_RESULT,
        .headline="Victory",.tone=SB_TONE_SUCCESS,.count=1,
        .rows={"Opponent requested a rematch."},
        .footer="Enter: Rematch / Esc: Main menu"};
    for(unsigned p=0;p<3;p++) assert(sb_ui_menu(&b,p,&rematch));
    snapshot(&b,"peer-rematch");
    menu=(sb_menu_view_t){.title="Versus / Guest",.subtitle="Classic / starter: Host",
        .surface=SB_MENU_SURFACE_VERIFY,.tone=SB_TONE_PENDING,.headline="482719",.count=4,
        .rows={"Verification code: 482719","Host: COMMANDER / compare codes and rules.",
               "Enter ONLY if both codes and rules match.","Alternating turns / fleet 5,4,3,3,2"},.footer="Esc: Cancel"};
    for(unsigned p=0;p<3;p++) assert(sb_ui_menu(&b,p,&menu));
    snapshot(&b,"verification");
    for(unsigned result=SB_RESULT_VICTORY;result<=SB_RESULT_ABORTED;result++) {
        sb_ui_result_style(&menu,result);
        if(result==SB_RESULT_ABORTED || result==SB_RESULT_DRAW) assert(menu.tone==SB_TONE_NORMAL);
        menu.count=2;snprintf(menu.rows[0],96,"%s",sb_result_message(result));
        strcpy(menu.rows[1],"Both players must agree to a rematch.");
        if(result==SB_RESULT_VICTORY || result==SB_RESULT_DEFEAT || result==SB_RESULT_DRAW) {
            strcpy(menu.rows[0],menu.rows[1]); menu.count=1;
        }
        strcpy(menu.footer,"Enter: Rematch   Esc: Main menu");
        for(unsigned p=0;p<3;p++) assert(sb_ui_menu(&b,p,&menu));
        if(result==SB_RESULT_VICTORY) snapshot(&b,"victory");
    }
    assert(sb_ui_handover(&b,SB_FONT_LARGE,true,false,false));snapshot(&b,"handover");
    pax_buf_destroy(&b);
    sb_ui_set_menu_art(NULL); pax_buf_destroy(&cache); pax_buf_destroy(&art);
    puts("Cyberdeck: hull geometry, ghost shapes, cursor/selection stacking, state panels and all outcomes passed");
}

int main(void) { cyberdeck(); navigation_regressions(); damage_markers(); behavior(); render(); dual_boards(); context_keys(); }
