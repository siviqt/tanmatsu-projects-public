#!/usr/bin/env python3
"""Exercise the production Versus loop with a simulated radio peer and UI."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sb-versus-runtime-') as tmp:
    p=Path(tmp); (p/'freertos').mkdir()
    (p/'pax_gfx.h').write_text('#pragma once\ntypedef struct pax_font pax_font_t;\ntypedef struct pax_buf pax_buf_t;\n')
    (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG -2\n')
    (p/'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
    (p/'freertos/FreeRTOS.h').write_text('#define pdMS_TO_TICKS(x) (x)\n')
    (p/'freertos/task.h').write_text('void vTaskDelay(unsigned);\n')
    sources=[root/'components/spacebattles/services/config.c',root/'components/spacebattles/app/menu.c',root/'components/spacebattles/services/preferences.c']
    for folder in ['net','game']:
        sources+=sorted((root/'components/spacebattles'/folder).glob('*.c'))
    sources+=[root/'components/spacebattles/render'/name for name in ['ui.c','typography.c','font_assets.c']]
    files=' '.join('"'+str(path)+'"' for path in sources)
    (p/'pax_gfx.h').unlink() # Use real PAX and fonts, not a renderer stub.
    (p/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(multiplayer_runtime C CXX)
set(PAX_STATIC TRUE)
add_subdirectory("{root}/managed_components/robotman2412__pax-gfx" pax)
add_executable(test "{root}/tools/test-multiplayer-runtime.c" {files})
target_include_directories(test PRIVATE "{p}" "{root}/components/spacebattles")
target_compile_definitions(test PRIVATE SB_NET_HOST)
target_compile_options(test PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g)
target_link_options(test PRIVATE -fsanitize=address,undefined)
target_link_libraries(test PRIVATE pax_gfx crypto)
''')
    for command in [['cmake','-S',str(p),'-B',str(p/'build')],['cmake','--build',str(p/'build'),'-j4']]:
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if result.returncode: print(result.stdout); raise SystemExit(result.returncode)
    subprocess.run([str(p/'build/test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
