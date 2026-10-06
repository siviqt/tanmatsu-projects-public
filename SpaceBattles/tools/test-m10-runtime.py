#!/usr/bin/env python3
"""Exercise M10 Settings in the real app loop with hardware and storage mocks."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sb-m10-runtime-') as tmp:
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
    (p/'bsp/display.h').write_text('#include <stdint.h>\n#include "esp_err.h"\nesp_err_t bsp_display_set_backlight_brightness(uint8_t);\n')
    (p/'bsp/input.h').write_text('#include <stdint.h>\n#include "esp_err.h"\nesp_err_t bsp_input_set_backlight_brightness(uint8_t);\n')
    (p/'bsp/audio.h').write_text('#include "esp_err.h"\nesp_err_t bsp_audio_set_volume(float);\n')
    (p/'test.c').write_text((root/'tools/test-m10-runtime.c').read_text())
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+tmp,
        '-I'+str(root/'components/spacebattles'),str(p/'test.c'),
        *[str(root/'components/spacebattles'/f) for f in ('app/local.c','app/menu.c','services/config.c','services/preferences.c')],
        *map(str,sorted((root/'components/spacebattles/game').glob('*.c'))),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
    # Re-run the same flows with actual PAX metrics, including match dialogs.
    (p/'pax_gfx.h').unlink()
    files=[root/'components/spacebattles'/f for f in ('app/local.c','app/menu.c','services/config.c','services/preferences.c','render/ui.c','render/typography.c','render/font_assets.c')]
    files+=sorted((root/'components/spacebattles/game').glob('*.c'))
    sources=' '.join('"'+str(f)+'"' for f in files)
    (p/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(m10_runtime C CXX)
set(PAX_STATIC TRUE)
add_subdirectory("{root}/managed_components/robotman2412__pax-gfx" pax)
add_executable(m10_runtime test.c {sources})
target_include_directories(m10_runtime PRIVATE "{p}" "{root}/components/spacebattles")
target_compile_definitions(m10_runtime PRIVATE M10_NATIVE_RENDER=1)
target_compile_options(m10_runtime PRIVATE -Wall -Wextra -Werror)
target_link_libraries(m10_runtime PRIVATE pax_gfx)
''')
    for cmd in (['cmake','-S',str(p),'-B',str(p/'build')],['cmake','--build',str(p/'build'),'-j','4']):
        r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if r.returncode: print(r.stdout); raise SystemExit(r.returncode)
    out=root/'build/m10-preview'; out.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(p/'build/m10_runtime')],cwd=out,check=True)
