#!/usr/bin/env python3
"""Portable M9 session tests with the actual M4 resolver and sanitizers."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sb-m9-') as tmp:
    p = Path(tmp)
    (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n')
    (p/'pax_gfx.h').write_text('#pragma once\ntypedef struct pax_font pax_font_t;\ntypedef struct pax_buf pax_buf_t;\n')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-g','-I'+tmp,'-I'+str(root/'components/spacebattles'),
        str(root/'tools/test-m9.c'),str(root/'components/spacebattles/app/local.c'),
        *map(str,sorted((root/'components/spacebattles/game').glob('*.c'))),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
