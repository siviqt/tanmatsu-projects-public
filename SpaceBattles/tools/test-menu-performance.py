#!/usr/bin/env python3
"""Check native menu cache correctness and compare host rendering cost (not HW latency)."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sb-menu-performance-') as tmp:
    work = Path(tmp)
    sources = [root/'tools/test-menu-performance.c'] + [root/'components/spacebattles/render'/f for f in ('ui.c','typography.c','font_assets.c')]
    files = ' '.join('"'+str(f)+'"' for f in sources)
    (work/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(menu_performance C CXX)
set(PAX_STATIC TRUE)
add_subdirectory("{root}/managed_components/robotman2412__pax-gfx" pax)
add_executable(menu_performance {files})
target_include_directories(menu_performance PRIVATE "{root}/components/spacebattles")
target_compile_options(menu_performance PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(menu_performance PRIVATE pax_gfx)
''')
    for cmd in (['cmake','-S',str(work),'-B',str(work/'build'),'-DCMAKE_BUILD_TYPE=Release'],
                ['cmake','--build',str(work/'build'),'-j4']):
        result = subprocess.run(cmd,capture_output=True,text=True)
        if result.returncode:
            print(result.stdout,result.stderr)
            raise SystemExit(result.returncode)
    subprocess.run([str(work/'build/menu_performance')],check=True)
