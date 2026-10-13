#!/usr/bin/env python3
"""Verify the fixed LoRa deployment and tactical views using production projection and PAX fonts.
Exercises current layouts rather than the superseded fixed-width status strip. Does not modify firmware.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="sb-countdown-") as tmp:
    p = Path(tmp)
    (p / "freertos").mkdir()
    (p / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG -2\n")
    (p / "esp_timer.h").write_text("#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
    (p / "freertos/FreeRTOS.h").write_text("#define pdMS_TO_TICKS(x) (x)\n")
    (p / "freertos/task.h").write_text("void vTaskDelay(unsigned);\n")
    sources = [root / "components/spacebattles" / s for s in
               ["render/ui.c", "render/typography.c", "render/font_assets.c"]]
    for directory in ["net", "game"]:
        sources += sorted((root / "components/spacebattles" / directory).glob("*.c"))
    files = " ".join('"' + str(s) + '"' for s in sources)
    (p / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 3.16)
project(countdown_probe C CXX)
set(PAX_STATIC TRUE)
add_subdirectory("{root}/managed_components/robotman2412__pax-gfx" pax)
add_executable(probe "{root}/tools/investigate-lora-countdown.c" {files})
target_include_directories(probe PRIVATE "{p}" "{root}/components/spacebattles")
target_compile_definitions(probe PRIVATE SB_NET_HOST)
target_compile_options(probe PRIVATE -ffunction-sections -fdata-sections)
target_link_options(probe PRIVATE -Wl,--gc-sections)
target_link_libraries(probe PRIVATE pax_gfx crypto)
""")
    for command in [["cmake", "-S", str(p), "-B", str(p / "build")],
                    ["cmake", "--build", str(p / "build"), "-j4"]]:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
    subprocess.run([str(p / "build/probe")], check=True)
