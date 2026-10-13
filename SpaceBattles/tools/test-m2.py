#!/usr/bin/env python3
"""Portable service tests; each run uses an isolated mock SD root."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="spacebattles-m2-") as tmp:
    exe = str(Path(tmp) / "test")
    subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g", '-DSB_STORAGE_DIRECTORY="sd/SpaceBattles"',
                    "-I" + str(root / "components/spacebattles"),
                    str(root / "tools/test-m2.c"),
                    str(root / "components/spacebattles/services/config.c"),
                    str(root / "components/spacebattles/services/storage.c"),
                    *["-Wl,--wrap=" + f for f in ("fopen", "fwrite", "fflush", "fsync", "fclose", "rename", "unlink")],
                    "-o", exe], check=True)
    subprocess.run([exe], cwd=tmp, check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})

    # Separate processes exercise persisted state beyond in-memory load/save.
    for mode in ("--write", "--read", "--write", "--read"):
        subprocess.run([exe, mode], cwd=tmp, check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
