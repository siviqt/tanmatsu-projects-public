# SpaceBattles

A Battleships-style game for Tanmatsu (ESP32-P4), written in C with ESP-IDF,
Badge BSP and PAX graphics.

This source distribution is based on **Release #6**, including the runtime hygiene
update accepted on hardware on 2026-10-13. The public-source privacy update removes
the two-device MAC allowlist; that follow-up has not been hardware-tested.
See [release notes](RELEASES.md).

- Single Player against Easy, Normal or Hard AI.
- Shared Device play with private handovers.
- Encrypted, host-authoritative LoRa multiplayer with verified pairing and rematches.
- Classic and Escalated rulesets, manual deployment and untimed turns.
- SD-backed settings at `/sd/SpaceBattles/config.ini`; active games are not saved.

Audio settings are present; playback and mixing are not implemented.
LoRa stays disabled until a region is explicitly confirmed in Settings.
Install the same application version on both devices: wire v2 / setup v4 is
incompatible with older protocol versions.

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
alternate output directory. Radio defaults to `SB_NET_RADIO=ON` with built-in
EU868 and US915 profiles. Set `SB_NET_RADIO=OFF` for a radio-disabled build.
`SB_NET_PROFILE_HEADER` remains available to replace the built-in profile catalog
with a custom single-region profile. Custom builds may differ from the accepted release.

Choose **Settings → Radio**, select the locally permitted profile, then select
**Confirm this region for my current location** and press Enter. Exit Settings to
save to SD. First launch and older saves require confirmation; a changed profile
requires confirmation again. Region changes are blocked during a match.
There is no reliable current-location detector: the factory country code, Wi-Fi
country, locale and timezone do not authorize transmission. After travelling,
review the saved region before playing. Without writable SD, confirmation lasts
only for the current run. The profiles
retain the exact board and radio-firmware compatibility checks. Device MAC
addresses do not restrict access. Pairing starts
as soon as radio initialization completes, with no startup transmit holdoff.
Per-packet airtime pacing remains active across matches and region changes
within the same process.
See [radio profile settings and test limits](components/spacebattles/net/README.md#radio-profiles-and-validation).

Install **`build/spacebattles.bin` through the Tanmatsu launcher/AppFS**.
Do not use `idf.py flash` or install the generated bootloader/partition table
for this application. The build produces an AppFS image for the existing launcher;
its generated standalone-flash partition table does not define application capacity.

Rebuilt firmware is not guaranteed to be byte-identical to the accepted image.

## AI and match results


Easy chooses random unknown cells. Normal prioritizes adjacent/aligned hits and
search stripes. Hard enumerates legal placements of each remaining ship length,
rejects placements crossing misses or sunk cells, and scores unknown cells by
placement density. Placements explaining unresolved hits receive priority;
longer hit runs carry more weight. Within a salvo, placements already covered by
selected shots are discounted. Equal scores use the existing random source.
All difficulties share random deployment and receive only public shot knowledge.
Hard uses marginal placements, not an exhaustive joint fleet search; touching
ships are legal and it never assumes a clear border around a sunk ship. AI does
not autonomously surrender. Saved `hard` settings now select Hard; per-match
setup choices continue to leave saved preferences unchanged.

Canonical results retain the absolute winner (`SB_ONGOING`, `SB_WIN_A`,
`SB_WIN_B`, `SB_DRAW`, `SB_ABANDONED`) plus `sb_end_reason_t`: none, combat,
surrender by A/B, deliberate departure by A/B, or aborted without a winner.
`sb_result_for` provides the shared player-relative interpretation: ongoing,
victory, defeat, draw, surrendered, opponent surrendered, left, opponent left,
or aborted. UI screens use its message helper rather than infer surrender.
LoRa carries the authoritative reason in its authenticated snapshot and rejects
inconsistent winner/reason pairs. A disconnect does not overwrite a finished
result. Setup v4 rejects older peers; update both devices together.

Regression tests cover 600 complete AI sessions, both rulesets, settings round
trips, both surrender perspectives, shared-device play, and host/guest LoRa
surrender. On 200 identical seeded random fleets, Normal averaged 50.88 shots
to sink the fleet and Hard 45.45. This measures targeting, not a guaranteed
win rate. Firmware and simulated-radio tests do not replace device testing.

## Controls

Combat always shows your fleet and opponent targets side by side in every mode.
Cyan function-key badges and labels appear only when their actions are available; their
positions remain fixed.

- Arrows move. F3 or Space places ships; R or F2 rotates; Tab selects the next ship.
- F1 opens Help. F4 or Enter marks the fleet Ready once all five ships are placed.
- F3 or Space selects/deselects the highlighted target. Classic keeps one selected
  target; F4 or Enter fires that selection, even if the cursor has moved elsewhere.
- Escalated: F4 or Enter submits the selected salvo, including partial or empty
  salvos. Selected targets remain marked until submission/resolution.
- Results wait for F4 or Enter to Continue. No automatic result countdown.
- F5 opens Link in LoRa. F6 opens Surrender when permitted; only a fresh Enter
  confirms the opponent's win. Esc cancels. Submission hides Surrender until the
  action is resolved; a finished match cannot be surrendered. An opponent's
  surrender immediately opens the result screen, dismissing pending review or
  overlays without F4/Enter, including while waiting after submitting a turn.
- Esc opens the match menu for Resume, Settings, Help, Surrender, or Abandon.
  Deliberately abandoning LoRa awards the other player a win immediately on
  receipt, including during result review or an open dialog. Connection failures
  remain distinct from an authenticated departure.
- Shared Device: a fresh Space press reveals the receiving player's private view.
- LoRa keeps processing radio traffic during Help, Link, Settings, and result
  review. Settings are saved after leaving the match and returning SD ownership.

Install the same accepted application image on both LoRa devices.

## Host checks

Use a Linux host with Python 3, a C/C++ compiler, CMake, OpenSSL development
headers/libcrypto, Pillow, and AddressSanitizer/UndefinedBehaviorSanitizer support.
Run the firmware build first to fetch the pinned managed components used by the
input and PAX rendering tests, then run:

```sh
python3 tools/test-m0a.py
python3 tools/test-m3.py
python3 tools/test-m1-renderer.py
python3 tools/test-menu-performance.py
python3 tools/test-sdcard.py
python3 tools/test-m2.py
python3 tools/test-m4.py
python3 tools/test-m8.py
python3 tools/test-m8-runtime.py
python3 tools/test-m9.py
python3 tools/test-m9-runtime.py
python3 tools/test-m10.py
python3 tools/test-m10-runtime.py
python3 tools/investigate-lora-countdown.py
python3 tools/test-radio.py
python3 tools/test-multiplayer.py
python3 tools/test-multiplayer-runtime.py
```

Rendering suites write previews under `build/`. Test names retain their original
milestone identifiers and exercise the current application. Automated host
simulations do not replace hardware tests.

## Source and assets

`main/` is the entry point. `components/spacebattles/` contains application flows,
game rules and AI, settings/storage services, hardware integration, input,
rendering and the build's validation hooks. Embedded JPEGs and generated font
and title data are included, so firmware builds need no asset conversion step.

- [Settings persistence and validation](SAVE-PERSISTENCE.md)
- [LoRa wire contract](components/spacebattles/net/README.md)
- [Historical latency simulation](components/spacebattles/net/LATENCY.md)
- [Embedded assets](components/spacebattles/assets/README.md)
- [Menu artwork source](assets/cyberdeck-v1/README.md)

Optional regeneration tools are `tools/prepare-m5-fonts.py` (fonts),
`tools/prepare-cyberdeck-assets.py` (menu artwork), and
`tools/prepare-multiplayer-vectors.py` (public deterministic protocol test fixtures;
requires the OpenSSL command-line tool). Their inputs are included.

Project code is licensed under [MIT](LICENSE). Bundled fonts and their bitmap
derivatives use the SIL Open Font License; notices are in
[assets/fonts](components/spacebattles/assets/fonts/). Historical Tanmatsu key
icons retain their [MIT notice](components/spacebattles/assets/keys/LICENSE).
Managed third-party components carry their own licenses.
