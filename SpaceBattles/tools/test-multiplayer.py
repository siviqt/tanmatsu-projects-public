#!/usr/bin/env python3
"""Authoritative host and projected guest over a paced, lossy half-duplex radio."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sb-net-') as tmp:
    exe = str(Path(tmp)/'test')
    subprocess.run(['cc','-std=c11','-DSB_NET_HOST','-Wall','-Wextra','-Werror','-Wpedantic',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
                    '-I'+str(root/'components/spacebattles'),str(root/'tools/test-multiplayer.c'),
                    *map(str,sorted((root/'components/spacebattles/net').glob('*.c'))),
                    *map(str,sorted((root/'components/spacebattles/game').glob('*.c'))),
                    '-lcrypto','-o',exe],check=True)
    subprocess.run([exe],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
