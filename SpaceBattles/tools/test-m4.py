#!/usr/bin/env python3
"""Build and run the portable rule suite with memory/undefined-behavior checks."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="spacebattles-m4-") as tmp:
    exe = str(Path(tmp) / "test")
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-fsanitize=address,undefined", "-g", "-I" + str(root / "components/spacebattles"),
        str(root / "tools/test-m4.c"),
        *map(str, sorted((root / "components/spacebattles/game").glob("*.c"))),
        "-o", exe,
    ], check=True)
    subprocess.run([exe], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
