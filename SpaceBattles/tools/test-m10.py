#!/usr/bin/env python3
"""Test M10 menu behavior and actual PAX layouts, exporting large-font previews."""
from pathlib import Path
import subprocess
import tempfile
from PIL import Image
root=Path(__file__).resolve().parents[1]
out=root/'build/m10-preview'; out.mkdir(parents=True,exist_ok=True)
# Exercise the same embedded artwork and UI composition on the host.
with Image.open(root/'components/spacebattles/assets/menu.jpg') as art:
    assert art.size == (800,480)
    (out/'menu-art.rgba').write_bytes(art.convert('RGBA').tobytes())
with tempfile.TemporaryDirectory(prefix='sb-m10-') as tmp:
    work=Path(tmp)
    (work/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n')
    sources=['app/menu.c','app/local.c','services/config.c','services/preferences.c','render/ui.c','render/typography.c','render/font_assets.c']
    sources += [str(p.relative_to(root/'components/spacebattles')) for p in sorted((root/'components/spacebattles/game').glob('*.c'))]
    files=' '.join('"'+str(root/'components/spacebattles'/s)+'"' for s in sources)
    (work/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(m10_test C CXX)
set(PAX_STATIC TRUE)
add_subdirectory("{root}/managed_components/robotman2412__pax-gfx" pax)
add_executable(m10_test "{root}/tools/test-m10.c" {files})
target_include_directories(m10_test PRIVATE "{work}" "{root}/components/spacebattles")
target_compile_options(m10_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(m10_test PRIVATE pax_gfx)
''')
    for cmd in (['cmake','-S',str(work),'-B',str(work/'build')],['cmake','--build',str(work/'build'),'-j','4']):
        r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if r.returncode: print(r.stdout); raise SystemExit(r.returncode)
    subprocess.run([str(work/'build/m10_test')],cwd=out,check=True)
