#!/usr/bin/env python3
"""Render the real M1 UI with native PAX; mock only hardware/JPEG/heap.
Requires Pillow, CMake, C and C++ compilers. Hardware decode remains a device check.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from PIL import Image

root = Path(__file__).resolve().parents[1]
out = root / 'build/m1-preview'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='sb-m1-render-') as temporary:
    work = Path(temporary)
    (work / 'driver').mkdir()
    (work / 'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_TIMEOUT 5
const char *esp_err_to_name(int err);
''')
    (work / 'esp_timer.h').write_text('#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void) { static int64_t t; return ++t; }\n')
    (work / 'esp_log.h').write_text('#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGD(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n')
    (work / 'esp_heap_caps.h').write_text('''#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_aligned_alloc(size_t align, size_t size, int flags);
void heap_caps_free(void *p);
''')
    (work / 'driver/jpeg_decode.h').write_text('''#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef void *jpeg_decoder_handle_t;
typedef struct { int timeout_ms; } jpeg_decode_engine_cfg_t;
typedef struct { int buffer_direction; } jpeg_decode_memory_alloc_cfg_t;
typedef struct { int output_format, rgb_order, conv_std; } jpeg_decode_cfg_t;
#define JPEG_DEC_ALLOC_OUTPUT_BUFFER 1
#define JPEG_DEC_ALLOC_INPUT_BUFFER 0
#define JPEG_DECODE_OUT_FORMAT_RGB565 0
#define JPEG_DEC_RGB_ELEMENT_ORDER_BGR 0
#define JPEG_YUV_RGB_CONV_STD_BT601 0
int jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *, jpeg_decoder_handle_t *);
int jpeg_del_decoder_engine(jpeg_decoder_handle_t);
void *jpeg_alloc_decoder_mem(size_t, const jpeg_decode_memory_alloc_cfg_t *, size_t *);
int jpeg_decoder_process(jpeg_decoder_handle_t, const jpeg_decode_cfg_t *, const uint8_t *, uint32_t, uint8_t *, uint32_t, uint32_t *);
''')
    for name in ('loading', 'logoff', 'menu'):
        with Image.open(root / f'components/spacebattles/assets/{name}.jpg') as im:
            rgb = im.convert('RGB').tobytes()
        raw = bytearray()
        for r, g, b in zip(rgb[::3], rgb[1::3], rgb[2::3]):
            raw.extend((((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)).to_bytes(2, 'little'))
        (work / f'{name}.raw').write_bytes(raw)
    (work / 'test.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render/renderer.c"
asm(".global _binary_loading_jpg_start\n_binary_loading_jpg_start: .byte 1\n"
    ".global _binary_loading_jpg_end\n_binary_loading_jpg_end:\n"
    ".global _binary_menu_jpg_start\n_binary_menu_jpg_start: .byte 3\n"
    ".global _binary_menu_jpg_end\n_binary_menu_jpg_end:\n"
    ".global _binary_logoff_jpg_start\n_binary_logoff_jpg_start: .byte 2\n"
    ".global _binary_logoff_jpg_end\n_binary_logoff_jpg_end:\n");
static bool uncertain, fail_decode, fail_alloc, fail_menu_alloc;
static int presents, decodes, frees;
static const char *filename;
void *heap_caps_aligned_alloc(size_t a, size_t n, int f) { (void)f; return fail_menu_alloc ? NULL : aligned_alloc(a,n); }
void heap_caps_free(void *p) { if(p) ++frees; free(p); }
const char *esp_err_to_name(int e) { (void)e; return "mock"; }
int jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *c, jpeg_decoder_handle_t *d) {
    assert(c->timeout_ms > 0 && c->timeout_ms <= 1000); *d=(void*)1; return 0;
}
int jpeg_del_decoder_engine(jpeg_decoder_handle_t d) { (void)d; return 0; }
void *jpeg_alloc_decoder_mem(size_t n, const jpeg_decode_memory_alloc_cfg_t *c, size_t *s) {
    (void)c; *s=n; return fail_alloc ? NULL : malloc(n);
}
int jpeg_decoder_process(jpeg_decoder_handle_t d, const jpeg_decode_cfg_t *c,
                         const uint8_t *in, uint32_t len, uint8_t *dst, uint32_t size, uint32_t *written) {
    (void)d; (void)c; (void)len; ++decodes;
    if(fail_decode) return ESP_ERR_TIMEOUT;
    FILE *f=fopen(*in == 1 ? "loading.raw" : *in == 3 ? "menu.raw" : "logoff.raw", "rb"); assert(f);
    assert(fread(dst,1,size,f)==size); fclose(f); *written=size; return 0;
}
esp_err_t sb_platform_display_init(sb_display_t *d) {
    *d=(sb_display_t){.width=480,.height=800,.rotation=90,.big_endian=true}; return 0;
}
bool sb_platform_can_release_frame(void) { return !uncertain; }
esp_err_t sb_platform_present(const void *p) {
    (void)p; ++presents;
    FILE *f=fopen(filename,"wb"); assert(f);
    assert(pax_buf_get_width(&framebuffer)==800 && pax_buf_get_height(&framebuffer)==480);
    fprintf(f,"P6\n800 480\n255\n");
    for(int y=0;y<480;++y) for(int x=0;x<800;++x) {
        pax_col_t c=pax_get_pixel(&framebuffer,x,y);
        fputc(c>>16,f); fputc(c>>8,f); fputc(c,f);
    }
    fclose(f); return 0;
}
int main(int argc, char **argv) {
    assert(argc==2);
    fail_decode=!strcmp(argv[1],"decode-failure"); fail_alloc=!strcmp(argv[1],"allocation-failure");
    assert(sb_renderer_init()==0);
    filename="loading.ppm"; assert(sb_renderer_loading("CHECKING SD CARD",40)==0);
    filename="ready.ppm"; assert(sb_renderer_ready("SD unavailable - settings not saved")==0);
    filename="input.ppm"; assert(sb_renderer_input_test("SD configuration loaded",5,3,1,"RADIO STATUS",1234567890,true)==0);
    // Incremental updates must exactly match a fresh full draw, including
    // long-to-short labels/counts, old cursor clearing and panel/config changes.
    size_t frame_size=480*800*(SB_COLOR_BITS/8);
    void *expected=malloc(frame_size); assert(expected);
    const struct { unsigned x,y,p,n; const char *a; bool repeat; } steps[] = {
        {0,0,0,1,"UP",false}, {1,0,0,2,"RIGHT",true},
        {1,0,1,3,"NEXT PANEL",false}, {1,0,1,4,"FIRE / PLACE",false},
        {5,3,1,1234567890,"RADIO STATUS",true}, {0,0,0,0,"NONE",false},
    };
    for (unsigned i=0;i<sizeof(steps)/sizeof(steps[0]);++i) {
        filename="incremental.ppm";
        assert(sb_renderer_input_test("SD loaded",steps[i].x,steps[i].y,steps[i].p,
                                      steps[i].a,steps[i].n,steps[i].repeat)==0);
        memcpy(expected,pixels,frame_size);
        input_valid=false;
        filename="full.ppm";
        assert(sb_renderer_input_test("SD loaded",steps[i].x,steps[i].y,steps[i].p,
                                      steps[i].a,steps[i].n,steps[i].repeat)==0);
        assert(!memcmp(expected,pixels,frame_size));
    }
    free(expected);
    filename="message.ppm"; assert(sb_renderer_message("Intervening screen")==0);
    assert(!input_valid);
    filename="reentry.ppm"; assert(sb_renderer_ready("SD loaded")==0);
    assert(input_valid);
    filename="exit.ppm"; assert(sb_renderer_exit()==0);
    assert(!input_valid);
    assert(presents==18);
    assert(decodes==(fail_alloc ? 0 : fail_decode ? 1 : 2));
    // Verify renderer profile changes, all UI pages, and lifecycle text at each size.
    for (unsigned p=0;p<SB_FONT_PROFILE_COUNT;++p) {
        sb_renderer_set_font_profile(sb_font_profile_name(p));
        assert(!input_valid);
        filename="loading.ppm"; assert(sb_renderer_loading("INTERFACE READY",100)==0);
        filename="exit.ppm"; assert(sb_renderer_exit()==0);
        for (unsigned page=0;page<SB_UI_PREVIEW_PAGES;++page) {
            filename="ui.ppm"; assert(sb_renderer_ui_preview(page)==0);
        }
    }
    for (unsigned p=0;p<SB_FONT_PROFILE_COUNT;++p) {
        sb_renderer_set_font_profile(sb_font_profile_name(p));
        for(unsigned page=0;page<SB_TACTICAL_PREVIEW_PAGES;++page) {
            filename="tactical.ppm"; assert(sb_renderer_tactical_preview(page,99,true)==0);
        }
    }
    sb_frame_sample_t sample;
    for(unsigned scenario=0;scenario<4;++scenario) {
        filename="benchmark.ppm";
        assert(sb_renderer_benchmark(scenario,125000,99,&sample)==0);
        assert(sample.draw_us>0 && sample.transfer_us>0);
    }
    assert(sb_renderer_benchmark(4,0,0,&sample)==ESP_ERR_INVALID_STATE);
    // Native menu background cache is prepared once and survives artwork changes.
    sb_menu_view_t menu={.surface=SB_MENU_SURFACE_COMMAND,.title="SPACEBATTLES",.count=2,.selectable=true,
        .rows={"Single Player","Settings"},.key_mask=1};
    unsigned before_decodes=decodes;
    filename="menu.ppm"; assert(sb_renderer_menu(&menu)==ESP_OK);
    assert(menu_cache_attempted);
    assert((menu_pixels!=NULL)==(!fail_decode && !fail_alloc));
    void *menu_expected=malloc(pax_buf_get_size(&framebuffer)); assert(menu_expected);
    memcpy(menu_expected,pixels,pax_buf_get_size(&framebuffer));
    assert(sb_renderer_message("Private screen replaced")==ESP_OK);
    assert(sb_renderer_menu(&menu)==ESP_OK);
    assert(!memcmp(menu_expected,pixels,pax_buf_get_size(&framebuffer)));
    assert(decodes==before_decodes); // Cache is independent of decoded lifecycle art.
    menu.selected=1; menu_valid=false; assert(sb_renderer_menu(&menu)==ESP_OK);
    memcpy(menu_expected,pixels,pax_buf_get_size(&framebuffer));
    menu.selected=0; assert(sb_renderer_menu(&menu)==ESP_OK);
    menu.selected=1; assert(sb_renderer_menu(&menu)==ESP_OK);
    assert(!memcmp(menu_expected,pixels,pax_buf_get_size(&framebuffer)));
    assert(sb_renderer_handover_match(false,true,false,0)==ESP_OK && !menu_valid);
    assert(sb_renderer_menu(&menu)==ESP_OK);
    assert(!memcmp(menu_expected,pixels,pax_buf_get_size(&framebuffer)));
    sb_renderer_set_font_profile("normal"); assert(!menu_valid);

    free(menu_expected);
    // Simulate cache allocation failure: solid fallback, no allocation/decode loop.
    pax_join(); sb_ui_set_menu_cache(NULL);
    if(menu_pixels) { pax_buf_destroy(&menu_backdrop); heap_caps_free(menu_pixels); menu_pixels=NULL; }
    menu_cache_attempted=false; fail_menu_alloc=true;
    assert(sb_renderer_menu(&menu)==ESP_OK && !menu_pixels);
    before_decodes=decodes;
    assert(sb_renderer_menu(&menu)==ESP_OK && decodes==before_decodes);
    fail_menu_alloc=false;
    uncertain=true;
    assert(sb_renderer_menu(&menu)==ESP_ERR_INVALID_STATE);
    assert(sb_renderer_benchmark(0,0,0,&sample)==ESP_ERR_INVALID_STATE);
    assert(sb_renderer_loading("BLOCKED",100)==ESP_ERR_INVALID_STATE);
    assert(sb_renderer_exit()==ESP_ERR_INVALID_STATE);
    assert(sb_renderer_input_test("blocked",0,0,0,"NONE",0,false)==ESP_ERR_INVALID_STATE);
    assert(sb_renderer_ui_preview(0)==ESP_ERR_INVALID_STATE);
    assert(sb_renderer_tactical_preview(0,0,false)==ESP_ERR_INVALID_STATE);
    assert(presents==80);
    int before=frees;
    sb_renderer_shutdown();
    assert(pixels!=NULL); // Never release a potentially live display source.
    if(fail_decode) assert(frees==before); // Nor uncertain JPEG DMA buffers.
    uncertain=false;
    sb_renderer_shutdown(); assert(pixels==NULL);
    return 0;
}
''')
    (work / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(m1_preview C CXX)
set(PAX_STATIC TRUE)
add_subdirectory("{root}/managed_components/robotman2412__pax-gfx" pax)
add_executable(m1_render test.c "{root}/components/spacebattles/render/typography.c" "{root}/components/spacebattles/render/font_assets.c" "{root}/components/spacebattles/render/ui.c")
target_include_directories(m1_render PRIVATE "{work}" "{root}/components/spacebattles")
target_compile_definitions(m1_render PRIVATE SB_PERFORMANCE=1 SB_COLOR_BITS={int(os.environ.get("SB_COLOR_BITS", "16"))})
target_link_libraries(m1_render PRIVATE pax_gfx)
''')
    for command in (['cmake', '-S', str(work), '-B', str(work/'build')],
                    ['cmake', '--build', str(work/'build'), '-j', '4']):
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
    for mode in ('normal','decode-failure','allocation-failure'):
        subprocess.run([str(work/'build/m1_render'),mode], cwd=work, check=True)
        for name in ('loading','ready','input','exit'):
            with Image.open(work/f'{name}.ppm') as image:
                image.save(out / f'{mode}-{name}.png')
    print(f'PASS: native PAX render, rotation/endianness, fallbacks, incremental/full pixel equivalence and transfer guards; previews: {out}')
