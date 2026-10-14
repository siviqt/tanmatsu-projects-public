# Current release

Release **6** combines the latest accepted gameplay, GUI, SD ownership/settings,
reliable LoRa departure, menu rendering and runtime fixes with both the MAC unlock
and ECC/AppFS handoff repair. The release number remains unchanged.
`components/spacebattles/release.h` controls the About screen and firmware metadata;
settings schema and radio protocol versions are independent.

The MAC unlock removes only the hardware-address allowlist and lookup. Board,
SX1262, exact radio firmware version/hash, region confirmation, configuration
readback, encryption and airtime pacing remain in force.

The ECC repair in `platform/launcher.c` restores ECC memory power immediately
before the BSP launcher restart. See [technical details](LAUNCHER-HANDOFF.md).

## Verification scope

The owner accepted the gameplay/GUI fixes through Release 5, and accepted the
runtime fix and ECC exit fix on 2026-10-13. The inspected ECC capture contained
two runs of ELF prefix `575bb69ca`, both restoring `0x4 -> 0x2` and loading
Repository categories, projects and icons successfully. Host/Join role coverage
comes from the owner's report.

On 2026-10-14 the production and manual-upload sources were consolidated,
compared and built. Current host regression suites passed. No device was connected
for a new hardware test of the combined MAC-unlocked/ECC-fixed firmware.
Historical acceptance does not substitute for that test.
