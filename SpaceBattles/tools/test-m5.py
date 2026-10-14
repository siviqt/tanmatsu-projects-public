#!/usr/bin/env python3
"""Exercise actual PAX metrics and opaque coverage; export every M5 profile/page."""
from pathlib import Path
import subprocess
import tempfile
from PIL import Image
root=Path(__file__).resolve().parents[1]
out=root/'build/m5-preview'; out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='sb-m5-') as tmp:
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
        for(unsigned page=0;page<SB_UI_PREVIEW_PAGES;++page) {
            assert(sb_ui_preview(&buf,p,page)); pax_join();
            char name[64]; snprintf(name,sizeof(name),"%s-%u.ppm",sb_font_profile_name(p),page);
            FILE *f=fopen(name,"wb"); assert(f); fprintf(f,"P6\n800 480\n255\n");
            for(int y=0;y<480;++y) for(int x=0;x<800;++x) {
                pax_col_t c=pax_get_pixel(&buf,x,y);
                fputc(c>>16,f); fputc(c>>8,f); fputc(c,f);
            } fclose(f);
        }
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
    pax_buf_destroy(&buf);
    assert(pax_buf_init(&buf,NULL,320,240,PAX_BUF_32_8888ARGB));
    assert(!sb_ui_preview(&buf,SB_FONT_LARGE,0));
    pax_buf_destroy(&buf);
    puts("PASS: all profiles/pages fit, worst-case HUD values, glyph coverage, monospace and opaque handovers");
}
''')
    (work/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(m5_test C CXX)
set(PAX_STATIC TRUE)
add_subdirectory("{root}/managed_components/robotman2412__pax-gfx" pax)
add_executable(m5_test test.c "{root}/components/spacebattles/render/ui.c" "{root}/components/spacebattles/render/typography.c" "{root}/components/spacebattles/render/font_assets.c")
target_include_directories(m5_test PRIVATE "{root}/components/spacebattles")
target_link_libraries(m5_test PRIVATE pax_gfx)
''')
    for cmd in (['cmake','-S',str(work),'-B',str(work/'build')],['cmake','--build',str(work/'build'),'-j','4']):
        result=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if result.returncode: print(result.stdout); raise SystemExit(result.returncode)
    subprocess.run([str(work/'build/m5_test')],cwd=out,check=True)
    for path in out.glob('*.ppm'):
        with Image.open(path) as im: im.save(path.with_suffix('.png'))
        path.unlink()
    print(f'Previews: {out}')
