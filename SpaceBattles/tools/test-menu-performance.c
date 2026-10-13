// SPDX-License-Identifier: MIT
#include "render/ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec*1e6+t.tv_nsec/1000.;
}
static sb_menu_view_t menu={.surface=SB_MENU_SURFACE_COMMAND,.title="SPACEBATTLES",.subtitle="Welcome, COMMANDER",
    .count=7,.selectable=true,.key_mask=1,
    .footer="Up/Down: Choose   Enter: Select   Esc: Log off",
    .rows={"Single Player","Shared Device","Versus (LoRa)","Settings","Help","About","Log off"}};
static void draw(pax_buf_t *buf, unsigned profile, unsigned row) {
    menu.selected=row;
    assert(sb_ui_menu(buf,profile,&menu)); pax_join();
}
int main(void) {
    pax_buf_t art;
    assert(pax_buf_init(&art,NULL,800,480,PAX_BUF_16_565RGB));
    for(unsigned y=0;y<480;y++) for(unsigned x=0;x<800;x++)
        pax_set_pixel(&art,0xff000000u|(x%256)<<16|(y%256)<<8|128,x,y);
    // The actual board uses rotated, reversed storage. Exercise those along
    // with both supported firmware depths and all font profiles.
    for(unsigned bits=16;bits<=24;bits+=8) for(unsigned rotated=0;rotated<2;rotated++)
    for(unsigned reversed=0;reversed<2;reversed++) {
        pax_buf_t frame,cache;
        pax_buf_type_t type=bits==16 ? PAX_BUF_16_565RGB : PAX_BUF_24_888RGB;
        int w=rotated ? 480 : 800,h=rotated ? 800 : 480;
        assert(pax_buf_init(&frame,NULL,w,h,type)); assert(pax_buf_init(&cache,NULL,w,h,type));
        pax_buf_set_orientation(&frame,rotated ? PAX_O_ROT_CCW : PAX_O_UPRIGHT);
        pax_buf_set_orientation(&cache,pax_buf_get_orientation(&frame));
        pax_buf_reversed(&frame,reversed); pax_buf_reversed(&cache,reversed);
        sb_ui_set_menu_art(&art); sb_ui_prepare_menu_cache(&cache); pax_join();
        size_t size=pax_buf_get_size(&frame); void *expected=malloc(size); assert(expected);
        for(unsigned profile=0;profile<3;profile++) for(unsigned row=0;row<7;row++) {
            sb_ui_set_menu_cache(NULL); draw(&frame,profile,row);
            memcpy(expected,pax_buf_get_pixels(&frame),size);
            pax_background(&frame,0xffff0000); // Cached restore also covers private content.
            sb_ui_set_menu_cache(&cache); draw(&frame,profile,row);
            assert(!memcmp(expected,pax_buf_get_pixels(&frame),size));
            // Incremental focus changes must match a complete render exactly.
            unsigned previous=(row+1)%7;
            draw(&frame,profile,previous); sb_menu_view_t before=menu;
            menu.selected=row;
            assert(sb_ui_menu_selection(&frame,profile,&before,&menu)); pax_join();
            assert(!memcmp(expected,pax_buf_get_pixels(&frame),size));

        }
        // Settings, setup and match rows share the fast path; fonts, disabled
        // controls and aligned values must remain identical to full rendering.
        sb_menu_view_t saved=menu;
        const sb_menu_surface_t surfaces[]={SB_MENU_SURFACE_PLAIN,SB_MENU_SURFACE_SETUP,
            SB_MENU_SURFACE_RULES,SB_MENU_SURFACE_MATCH};
        for(unsigned k=0;k<4;k++) for(unsigned profile=0;profile<3;profile++) {
            menu=(sb_menu_view_t){.surface=surfaces[k],.title="Settings",.subtitle="Options",
                .count=k==0 ? 6 : k==1 ? 3 : k==2 ? 2 : 5,.selectable=true,
                .disabled_rows=2,.value_rows=k==0 ? 63 : 0,
                .rows={"Brightness: 75%","Keyboard: On","Volume: 50%","Help","Return","About"}};
            for(unsigned row=0;row<menu.count;row++) {
                draw(&frame,profile,row); memcpy(expected,pax_buf_get_pixels(&frame),size);
                draw(&frame,profile,(row+1)%menu.count); sb_menu_view_t before=menu;
                menu.selected=row; assert(sb_ui_menu_selection(&frame,profile,&before,&menu)); pax_join();
                assert(!memcmp(expected,pax_buf_get_pixels(&frame),size));
                before.title[0]='X'; assert(!sb_ui_menu_selection(&frame,profile,&before,&menu));
                assert(!memcmp(expected,pax_buf_get_pixels(&frame),size));
            }
        }
        menu=saved;
        // Incompatible cache must use normal rendering rather than corrupt pixels.
        sb_ui_set_menu_cache(NULL); draw(&frame,SB_FONT_LARGE,0);
        memcpy(expected,pax_buf_get_pixels(&frame),size);
        pax_buf_reversed(&cache,!reversed); sb_ui_set_menu_cache(&cache);
        draw(&frame,SB_FONT_LARGE,0); assert(!memcmp(expected,pax_buf_get_pixels(&frame),size));
        pax_buf_reversed(&cache,reversed);
        if(bits==16 && !rotated && !reversed) {
            for(unsigned mode=0;mode<4;mode++) {
                sb_ui_set_menu_art(mode ? &art : NULL);
                if(mode>=2) sb_ui_set_menu_cache(&cache);
                double start=now_us();
                draw(&frame,SB_FONT_LARGE,0);
                for(unsigned i=0;i<100;i++) {
                    sb_menu_view_t before=menu; menu.selected=i%7;
                    if(mode==3) { assert(sb_ui_menu_selection(&frame,SB_FONT_LARGE,&before,&menu)); pax_join(); }
                    else draw(&frame,SB_FONT_LARGE,i%7);
                }
                printf("menu %s mean_us=%.1f (host CPU only)\n",
                    mode==0 ? "solid" : mode==1 ? "scaled_scrim" : mode==2 ? "cached" : "selection_only",(now_us()-start)/100);
            }
        }
        sb_ui_set_menu_cache(NULL); free(expected); pax_buf_destroy(&cache); pax_buf_destroy(&frame);
    }
    sb_ui_set_menu_art(NULL); pax_buf_destroy(&art);
    puts("PASS: cached/uncached pixel equality, all fonts/rows, 16/24-bit, rotation, byte order, privacy and mismatch fallback");
}
