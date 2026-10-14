#!/usr/bin/env python3
"""Render M6 with real PAX; verify privacy, HUD limits and full event capacity."""
from pathlib import Path
import subprocess
import tempfile
from PIL import Image
root=Path(__file__).resolve().parents[1]
out=root/'build/m6-preview'; out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='sb-m6-') as tmp:
    work=Path(tmp)
    (work/'test.c').write_text(r'''
#include "render/ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(void) {
    pax_buf_t buf;
    assert(pax_buf_init(&buf,NULL,800,480,PAX_BUF_32_8888ARGB));
    assert(sb_font_profile_parse(NULL)==SB_FONT_MEDIUM);
    assert(sb_font_profile_parse("invalid")==SB_FONT_MEDIUM);
    for (unsigned p=0;p<SB_FONT_PROFILE_COUNT;++p) {
        assert(sb_font_profile_parse(sb_font_profile_name(p))==p);
        for(unsigned role=0;role<SB_FONT_ROLE_COUNT;++role) {
            sb_font_t f=sb_font_get(p,role);
            for(unsigned c=32;c<127;++c) {
                char label[]={c,0}; assert(pax_text_size(f.font,f.size,label).x>0);
            }
        }
        sb_font_t mono=sb_font_get(p,SB_FONT_COORDINATE);
        assert(pax_text_size(mono.font,mono.size,"111").x==pax_text_size(mono.font,mono.size,"WWW").x);
        for(unsigned page=0;page<SB_TACTICAL_PREVIEW_PAGES;++page) {
            assert(sb_ui_tactical_preview(&buf,p,page,99,false)); pax_join();
            char name[64]; snprintf(name,sizeof(name),"%s-%u.ppm",sb_font_profile_name(p),page);
            FILE *f=fopen(name,"wb"); assert(f); fprintf(f,"P6\n800 480\n255\n");
            for(int y=0;y<480;++y) for(int x=0;x<800;++x) {
                pax_col_t c=pax_get_pixel(&buf,x,y);
                fputc(c>>16,f); fputc(c>>8,f); fputc(c,f);
            } fclose(f);
        }

        assert(sb_ui_tactical_preview(&buf,p,2,99,true)); pax_join();
        char enemy_name[64]; snprintf(enemy_name,sizeof(enemy_name),"%s-enemy.ppm",sb_font_profile_name(p));
        FILE *enemy_file=fopen(enemy_name,"wb"); assert(enemy_file);
        fprintf(enemy_file,"P6\n800 480\n255\n");
        for(int y=0;y<480;++y) for(int x=0;x<800;++x) {
            pax_col_t c=pax_get_pixel(&buf,x,y);
            fputc(c>>16,enemy_file); fputc(c>>8,enemy_file); fputc(c,enemy_file);
        }
        fclose(enemy_file);
        // Every handover combination overwrites all preceding pixels, regardless
        // of old fleet content. Compare complete frames after two backgrounds.
        void *expected=malloc(800*480*4); assert(expected);
        for(unsigned b=0;b<2;++b) for(unsigned d=0;d<2;++d) for(unsigned press=0;press<2;++press) {
            pax_background(&buf,0xffff0000);
            assert(sb_ui_handover(&buf,p,b,d,press)); pax_join();
            memcpy(expected,buf.buf,800*480*4);
            pax_background(&buf,0xff00ff00);
            assert(sb_ui_handover(&buf,p,b,d,press)); pax_join();
            assert(!memcmp(expected,buf.buf,800*480*4));
        }
        free(expected);
        for(unsigned type=0;type<3;++type) for(unsigned escalated=0;escalated<2;++escalated) {
            sb_ui_hud_t hud={.play_type=type,.escalated=escalated,.turn_or_round=UINT32_MAX,
                .shots_selected=25,.shots_available=25,.seconds_remaining=5999,.target_column='J',.target_row=10};
            assert(sb_ui_hud(&buf,p,&hud));
        }
    }

    sb_ui_tactical_t v={.ruleset=SB_ESCALATED,.play_type=SB_UI_VERSUS,
        .active=true,.sequence=UINT32_MAX,.seconds_remaining=5999,.available=5,.selected=5,.cursor=99};
    sb_record_t record={.count=SB_EVENTS};
    for(unsigned i=0;i<SB_EVENTS;++i) {
        record.events[i]=(sb_event_t){.type=i<10 ? SB_ATTACK_EVENT : SB_SUNK_EVENT,
            .attacker=i%2,.cell=i,.hit=true,.length=2,.cells={88,89}};
    }
    v.effects=&record;
    sb_record_t original=record;
    for(unsigned p=0;p<3;++p) for(unsigned own=0;own<2;++own) {
        v.own_board=own;
        assert(sb_ui_tactical(&buf,p,&v));
        assert(!memcmp(&record,&original,sizeof(record)));
    }
    v.own_board=false; v.occupied[0]=true;
    assert(!sb_ui_tactical(&buf,SB_FONT_LARGE,&v));
    pax_join(); assert(pax_get_pixel(&buf,56,96)==0xff080e20);
    v.occupied[0]=false;
    record.count=SB_EVENTS+1; assert(!sb_ui_tactical(&buf,SB_FONT_MEDIUM,&v));
    record.count=SB_EVENTS; record.events[19].cells[1]=100;
    assert(!sb_ui_tactical(&buf,SB_FONT_MEDIUM,&v));
    v.effects=NULL; v.cursor=100; assert(!sb_ui_tactical(&buf,SB_FONT_MEDIUM,&v));
    v.cursor=99; v.ruleset=SB_CLASSIC; v.available=v.selected=1;
    for(unsigned p=0;p<3;++p) {
        assert(sb_ui_tactical(&buf,p,&v)); pax_join();
        void *expected=malloc(800*480*4); assert(expected);
        memcpy(expected,buf.buf,800*480*4);
        v.seconds_remaining=0;
        assert(sb_ui_tactical(&buf,p,&v)); pax_join();
        assert(!memcmp(expected,buf.buf,800*480*4)); free(expected);
        for(unsigned page=4;page<7;++page) {
            assert(sb_ui_tactical_preview(&buf,p,0,0,false));
            assert(sb_ui_tactical_preview(&buf,p,page,0,false)); pax_join();
            expected=malloc(800*480*4); memcpy(expected,buf.buf,800*480*4);
            assert(sb_ui_tactical_preview(&buf,p,2,99,true));
            assert(sb_ui_tactical_preview(&buf,p,page,99,true)); pax_join();
            assert(!memcmp(expected,buf.buf,800*480*4)); free(expected);
        }
        for(unsigned cell=0;cell<100;++cell) assert(sb_ui_tactical_preview(&buf,p,2,cell,true));
    }
    // M8 live labels must fit all supported profiles as well as preview text.
    v=(sb_ui_tactical_t){.ruleset=SB_CLASSIC,.play_type=SB_UI_SHARED_DEVICE,
        .active=true,.own_board=true,.available=1,.cursor=99,.sequence=199};
    const char *phases[]={"Ship 1: size 5 H","SHIP DESTROYED","YOUR TURN"};
    const char *actions[]={"Space: Place","Next player in 3...","F3: Enemy waters"};
    const char *details[]={"Place all five ships first","Shot resolved","Fleet 5/5 | HP 17/17"};
    const char *footers[]={"R: Rotate  Tab: Ship  Enter: Ready  F1: Help",
        "Result display - controls locked",
        "Arrows: Aim  Enter: Fire  F2: Own fleet  F3: Enemy"};
    for(unsigned p=0;p<3;++p) for(unsigned phase=0;phase<3;++phase) {
        v.sequence_label=phase==0 ? "DEPLOYMENT" : NULL;
        v.deployment=phase==0;
        v.ship_selected=2;
        v.own_board=phase!=1;
        v.ships_placed[0]=v.ships_placed[1]=true;
        v.cursor=phase==0 ? 22 : 99;
        memset(v.placement,0,sizeof(v.placement));
        if(phase==0) for(unsigned j=0;j<3;++j) v.placement[22+j]=true;
        v.placement_valid=true;
        v.phase_label=phases[phase]; v.action_label=actions[phase];
        v.detail_label=details[phase]; v.footer_label=footers[phase];
        assert(sb_ui_tactical(&buf,p,&v)); pax_join();
        char name[64]; snprintf(name,sizeof(name),"m8-%u-%u.ppm",p,phase);
        FILE *f=fopen(name,"wb"); assert(f); fprintf(f,"P6\n800 480\n255\n");
        for(int y=0;y<480;++y) for(int x=0;x<800;++x) {
            pax_col_t c=pax_get_pixel(&buf,x,y);
            fputc(c>>16,f); fputc(c>>8,f); fputc(c,f);
        } fclose(f);
    }
    // M9 Single Player labels and timers at each supported font size.
    for(unsigned p=0;p<3;++p) for(unsigned phase=0;phase<3;++phase) {
        sb_ui_tactical_t ai={.ruleset=SB_ESCALATED,.play_type=SB_UI_SINGLE_PLAYER,
            .active=true,.own_board=phase==0,.available=5,.selected=5,.cursor=22,
            .sequence=100,.seconds_remaining=600,.deployment=phase==0,.ship_selected=2};
        ai.action_label=phase==2 ? "Next round in 3..." : "Enter: Complete turn";
        ai.detail_label=phase==0 ? "Placed 5/5 | 60s left" : phase==2 ? "Hits: You 5 | AI 5" : "Deadline: fleet replaced";
        ai.footer_label="Space: Toggle shot  Enter: Complete  F2/F3: View";
        assert(sb_ui_tactical(&buf,p,&ai)); pax_join();
        char name[64]; snprintf(name,sizeof(name),"m9-%u-%u.ppm",p,phase);
        FILE *f=fopen(name,"wb"); assert(f); fprintf(f,"P6\n800 480\n255\n");
        for(int y=0;y<480;++y) for(int x=0;x<800;++x) {
            pax_col_t c=pax_get_pixel(&buf,x,y);
            fputc(c>>16,f); fputc(c>>8,f); fputc(c,f);
        } fclose(f);
    }
    // Ghost color conveys legality and never modifies committed occupancy.
    v.deployment=true; // Previous loop left this fixture in combat.
    v.placement[22]=true; v.placement_valid=true; v.own_board=true;
    assert(sb_ui_tactical(&buf,SB_FONT_LARGE,&v)); pax_join();
    assert(pax_get_pixel(&buf,56+2*32+3,96+2*32+12)==0xff65dafa);
    v.placement_valid=false;
    assert(sb_ui_tactical(&buf,SB_FONT_LARGE,&v)); pax_join();
    assert(pax_get_pixel(&buf,56+2*32+3,96+2*32+12)==0xffff6b60);
    v.own_board=false; assert(!sb_ui_tactical(&buf,SB_FONT_LARGE,&v));
    pax_buf_destroy(&buf);
    assert(pax_buf_init(&buf,NULL,320,240,PAX_BUF_32_8888ARGB));
    assert(!sb_ui_tactical_preview(&buf,SB_FONT_LARGE,0,0,false));
    pax_buf_destroy(&buf);
    puts("PASS: M6 profiles/pages, all cursor cells, public-only enemy views, immutable 20-event records, HUD limits and opaque handovers");
}
''')
    (work/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(m6_test C CXX)
set(PAX_STATIC TRUE)
add_subdirectory("{root}/managed_components/robotman2412__pax-gfx" pax)
add_executable(m6_test test.c "{root}/components/spacebattles/render/ui.c" "{root}/components/spacebattles/render/typography.c" "{root}/components/spacebattles/render/font_assets.c")
target_include_directories(m6_test PRIVATE "{root}/components/spacebattles")
target_link_libraries(m6_test PRIVATE pax_gfx)
''')
    for cmd in (['cmake','-S',str(work),'-B',str(work/'build')],['cmake','--build',str(work/'build'),'-j','4']):
        result=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if result.returncode: print(result.stdout); raise SystemExit(result.returncode)
    subprocess.run([str(work/'build/m6_test')],cwd=out,check=True)
    for path in out.glob('*.ppm'):
        with Image.open(path) as im: im.save(path.with_suffix('.png'))
        path.unlink()
    print(f'Previews: {out}')
