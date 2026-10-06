# SpaceBattles

A Battleships-style game for Tanmatsu (ESP32-P4), written in C using ESP-IDF,
Badge BSP and PAX graphics.

This source distribution represents the accepted local-play application (M10),
with device tests confirmed by the project owner on 2026-10-06. It includes
Single Player against Easy/Normal AI and Shared Device play, both with Classic
and Escalated rulesets. Settings are saved to `/sd/SpaceBattles/config.ini` when
writable SD storage is available; games themselves are not persisted.

Host/Join is displayed but unavailable. The application does not enable radio
traffic. Audio settings exist, but playback and mixing are not implemented.

## Build

Use ESP-IDF **6.0.2**, including its ESP32-P4 toolchain, Python environment,
CMake and Ninja. The component manager resolves Badge BSP **1.4.0** and
PAX graphics **2.2.2** from `components/spacebattles/idf_component.yml`;
`dependencies.lock` pins the dependency graph. First-time dependency resolution
requires network access. No separate Tanmatsu SDK checkout is required.

From an ESP-IDF 6.0.2 environment, run:

```sh
. /path/to/esp-idf/export.sh
bash tools/build.sh
```

Alternatively, select an ESP-IDF Installation Manager activation script:

```sh
SB_IDF_ACTIVATION=/path/to/activate_idf_v6.0.2.sh bash tools/build.sh
```

The target is fixed to `esp32p4` in `CMakeLists.txt`. `sdkconfig.defaults` provides
the Tanmatsu configuration; ESP-IDF generates local `sdkconfig` and `build/`.
The normal build uses `SB_LOCAL_GAME=ON`, `SB_COLOR_BITS=16`,
`SB_PERFORMANCE=OFF` and `SB_VALIDATION_FAULT=none`. `SB_BUILD_DIR` can select an
alternate output directory. Use these defaults for the accepted application.

Install **`build/spacebattles.bin` through the Tanmatsu launcher/AppFS**.
Do not use `idf.py flash` or install the generated bootloader/partition table
for this application. The build produces an AppFS image for the existing launcher;
its generated standalone-flash partition table does not define application capacity.

The previously accepted firmware SHA-256 was
`b79a1c84542485e53bd37e15f179c1ff955d30383f8ae75d2dc50906cd3ca8d8`.
Rebuilt firmware is not guaranteed to be byte-identical. Binaries and local
validation evidence are excluded from this source distribution.

## Controls

- Arrow keys navigate. Space places ships during deployment.
- Classic: Enter fires. Escalated: Space toggles targets; Enter submits the
  selected salvo, including a partial or empty salvo.
- Shared Device: a fresh Space press reveals the next player's private screen.
- F1 Help, F2 Fleet, F3 Targets, F4 Settings, F5 Link (unavailable), F6 Both.
- Esc opens the match menu or returns from a settings screen.

## Host checks

On a Linux host with Python 3, a C/C++ compiler, CMake and AddressSanitizer/
UndefinedBehaviorSanitizer support, run the retained regression suites:

```sh
python3 tools/test-m2.py
python3 tools/test-m4.py
python3 tools/test-m8.py
python3 tools/test-m8-runtime.py
python3 tools/test-m9.py
python3 tools/test-m9-runtime.py
python3 tools/test-m10.py
python3 tools/test-m10-runtime.py
```

The rendering tests require the managed PAX component fetched by the firmware
build. They write previews under `build/`. Test filenames retain their original
milestone identifiers; the tests exercise the current application.

## Source and assets

`main/` is the entry point. `components/spacebattles/` contains application flows,
game rules and AI, configuration/storage services, hardware integration, input,
rendering and required validation hooks. Embedded JPEGs and generated font/icon
C data are included; firmware builds need no asset conversion step.

Optional font/icon regeneration uses `tools/prepare-m5-fonts.py` and
`tools/prepare-key-icons.py`. See the asset READMEs for inputs and converter
versions. Experimental radio firmware, unused artwork, historical reports,
local SDK installations and build outputs are excluded by `.gitignore`.

Project code is licensed under [MIT](LICENSE). Bundled fonts and their bitmap
derivatives use the SIL Open Font License; notices are in
[assets/fonts](components/spacebattles/assets/fonts/). Tanmatsu key icons retain
their [MIT notice](components/spacebattles/assets/keys/LICENSE). Managed
third-party components carry their own licenses.
