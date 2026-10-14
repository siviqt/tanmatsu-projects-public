# Save persistence and SD validation

SpaceBattles persists **one settings profile**, not active games or multiple save
slots. The permanent namespace is `/sd/SpaceBattles/config.ini`, with
`config.bak` (previous committed profile) and `config.tmp` (pending replacement).
Other files on the card are not managed by the game.

## Findings and fixes (2026-10-09)

The supplied device log subsequently identified the startup save failure:
`sd_host_create_sdmmc_controller: no available sd host controller`, followed by
`SD mount result: ESP_ERR_NOT_FOUND`. This occurs before settings file I/O.
ESP-Hosted initializes the SDMMC controller and radio slot 1 in its constructor,
before `app_main()`. The SD adapter incorrectly used `sb_radio_host_ready()`
(which becomes true only after LoRa initialization) to decide whether the
controller already existed. Startup therefore called `sdmmc_host_init()` again.
In IDF 6.0.2 this allocates another controller and fails with ESP_ERR_NOT_FOUND;
it is not the idempotent init behavior shown by the IDF 5.5.2 launcher in the log.

The SD adapter now uses the configured ESP-Hosted SDIO ownership from startup,
calls its idempotent `esp_hosted_init()` instead of allocating a second controller,
and tears down only SD slot 0 on unmount or failed card initialization. Radio
slot 1 and the controller remain owned by ESP-Hosted. This also applies when
LoRa gameplay is disabled but the Hosted transport is still linked/configured.
It does not connect to the radio or change the SD path/format. The new host test
reproduced the startup failure before the fix and passes afterward.

The LoRa handoff also unmounted/remounted SD but never updated `sb_storage_t.mounted`.
If startup mounting failed, a subsequent successful remount still left saving
permanently disabled for that session. Conversely, failed remounts retained a
stale available state. The Versus runtime regression test reproduced the former
before the fix. The handoff now marks storage unavailable before releasing SD
and propagates the completed remount result, without replacing session edits.
Save retries also create the settings directory if startup could not create it.

A separate failure test reproduced loss of the sole complete `config.tmp`: after
an interrupted first commit, another save truncated that recovery file before a
replacement succeeded. Save now promotes the existing backup or sole complete
temporary file before opening a new temporary file.

Writes are flushed, synchronized, closed, and read back byte-for-byte before
rotating the committed file into the backup. Parsing alone was insufficient:
version 1 intentionally accepts missing optional keys, so an altered or truncated
file could still parse. Failures retain dirty session settings. Diagnostics emit
one warning per failed filesystem attempt with operation, path, and errno; they
never log settings contents. SD mount errors retain the platform's existing
ESP error diagnostic. Unavailable/protected storage remains visible in Settings.

The supplied log confirms the startup mount failure, and the controller conflict
was reproduced in the host driver model. The owner subsequently confirmed SD saves work on the device. The application's logged ELF hash was `b1c2467e3...`, while its
monitor used a different local ELF (`4644d70a...`); match the installed binary and
monitor ELF when retesting. No device was accessible directly during this work.
Git metadata was unavailable, so no introducing commit was identified.
The current and preserved GUI release configurations both enable long filenames;
this was not evidence of an LFN regression. The build now explicitly requires
LFN support for the unchanged `SpaceBattles` directory.

## Release compatibility contract

- Save paths are constants, independent of the project version, application title,
  AppFS slug, AppFS entry/revision, and firmware image filename. AppFS holds the
  executable; these settings live separately on SD. The inspected launcher AppFS
  replacement code replaces executable entries, not this settings directory.
- A normal launcher application upgrade that preserves the SD card/directory
  keeps the same settings. No mount failure formats the card. Do not rename this
  directory to match a release or introduce a release-specific namespace.
- The schema remains `[meta] version=1`. Previous version 1 files load without
  rewriting; absent optional keys receive current defaults. Existing supported
  values survive an update. Application release numbers must never set the save
  schema version. Any incompatible future schema needs a separately tested,
  explicit migration; none is needed for this change.
- Future schema versions and structurally invalid files are preserved. A valid
  primary, backup, or temporary candidate can still supply settings, in that order,
  but protected files prevent persistence. Back up the entire directory before
  manually repairing or moving a damaged/future file out of it; restart afterward.
- Unknown keys/comments in version 1 are ignored and are not preserved on a later
  settings write. Malformed individual values default; version 0/unversioned files
  are unsupported. Downgrading from a future schema does not migrate it backwards.
- FAT/card power-loss atomicity, failing media, formatting, or external deletion
  cannot be guaranteed. The protocol retains a committed file until the new file
  is written and verified, and keeps a backup for recovery. A partial temporary
  file that cannot be removed conservatively protects persistence on restart.
- Hot insertion does not trigger automatic mounting. Restart the application, or
  use the existing SD remount after leaving Versus. A retry only retries file I/O
  on an already mounted card. Card identity is not tracked across a card swap.

## Automated verification

Run `python3 tools/test-m2.py` for create/update/load, repeated updates, separate
process restarts, legacy version 1 loading/updating, all three managed files,
future/invalid protection, recovery boundaries, unavailable/remounted storage,
and 11 injected I/O/corruption cases. The rename wrapper enforces FatFs's refusal
to replace an existing destination, rather than Linux's replacement semantics.
Faults cover write-open/read-only, short writes/full media, flush, sync, close,
backup/commit rename, unlink, read failure, removal/unavailability, and altered
read-back data. These are host simulations, not physical FatFs/power-loss tests.

Run `python3 tools/test-sdcard.py` for the actual SD adapter with a model of
Hosted's pre-main controller ownership and IDF 6's non-idempotent controller
allocation. It tests startup, repeated mounts, absent-card cleanup, and retention
of radio slot 1 in radio-enabled, radio-disabled, and M0b build configurations.

Run `python3 tools/test-multiplayer-runtime.py` for successful and failed remount
state propagation across the real Versus loop. `test-m0a.py`,
`test-m8-runtime.py`, `test-m9-runtime.py`, `test-m10.py`, and
`test-m10-runtime.py` cover startup/exit and settings feedback/integration.
Build with `tools/build.sh` in the ESP-IDF 6.0.2 environment. Install the resulting
`build/spacebattles.bin` through the launcher AppFS workflow.

## Hardware confirmation and remaining checks

On 2026-10-10 the owner confirmed application HW tests passed after the LoRa
region-confirmation update. Historical device acceptance was recorded separately
from this source distribution.
Schema version 1 now also saves `radio.manual_region`, separately from the
selected `radio.region`. Legacy files require explicit confirmation; changing the
profile clears it. Separate-process save/reload passed host tests. The owner's
overall hardware report did not include individual case logs, so the broader
media-failure and power-loss qualification below is not marked complete.

On 2026-10-09 the owner confirmed that SD saves work with this fix. The historical
device log inspected before cleanup matched
ELF `f45e89087...`, shows `SD mount result: ESP_OK` and configuration loading on
two application launches, and a successful SD remount after radio use. This
confirms the reported startup save regression is resolved. The log does not
print callsign/font values, so their exact round trip is not independently
asserted from serial output. The broader upgrade, removal, filesystem-error,
and power-loss checklist below remains to be qualified explicitly.

Use a backed-up test card; retain serial output, firmware identity, and copies of
all three managed files for each failure.

1. **Startup controller ownership:** install the new binary through AppFS and
   use its matching ELF in the serial monitor. Confirm the SpaceBattles ELF hash,
   `SD mount result: ESP_OK`, and absence of the duplicate-controller error.
   Repeat a launcher warm launch and cold boot without entering Versus.
2. **Create/update/restart:** launch with a writable supported FAT card and no
   settings directory. Change callsign and brightness; leave Settings, change
   again, then log off and relaunch. Verify the latest values and the backup.
   Also power-cycle and verify them. Confirm unrelated card files are unchanged.
3. **Upgrade:** save non-default values with the previous accepted application.
   Replace only its AppFS executable with this build through the launcher,
   retaining the SD card. Relaunch and verify settings before making any changes,
   then update and restart. Repeat with an older v1 file missing optional keys.
4. **LoRa handoff:** verify saving before and after joining/hosting/leaving a
   match. Launch without SD, edit settings, insert the test card before leaving
   Versus, and verify successful remount permits saving session edits. Repeat
   with no card at remount; expect unavailable status and no saved-success claim.
5. **Removal:** remove the test card before a dirty save and during a write.
   Expect a bounded failure, retained session edits, and a useful diagnostic.
   Reinsert and restart; inspect the primary/backup/temporary files and confirm
   the last committed valid profile is recoverable. Hot insertion alone is not
   promised to recover an existing mount.
6. **Filesystem errors:** test a full card, read-only media where supported, a
   file occupying the settings-directory path, damaged settings, and a future
   schema in each managed filename. Verify original contents are preserved and
   failure/protection is visible. Record operation/path/errno for failures.
7. **Interrupted commit:** on a sacrificial backed-up card, interrupt power at
   write/rotation/commit boundaries. Confirm recovery order (primary, backup,
   temporary) and that a damaged file causes protection rather than silent reset.
   FAT/media damage itself may require offline repair; do not format automatically.
