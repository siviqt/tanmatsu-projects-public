# Repository TLS failure after LoRa pairing

Hardware-confirmed on 2026-10-13 after both Host and Join pairing exits.
The fix is included in the normal production build; release number remains 6.

## Issue

Exiting SpaceBattles after LoRa pairing could leave the launcher's Repository
unable to load categories even though Wi-Fi connected and obtained an IP address.
TLS handshakes failed with `-0x4E00` (`MBEDTLS_ERR_ECP_VERIFY_FAILED`). A full power
cycle restored Repository access. Exiting without using LoRa did not reproduce
the failure in the original capture.

## Cause

Host/Join generates a P-256 key using hardware ECC on the ESP32-P4. The application's
ESP-IDF 6.0.2 driver powers down ECC RAM after use: it clears
`HP_SYSTEM_ECC_MEM_FORCE_PU` and sets `HP_SYSTEM_ECC_MEM_PD`. Its CPU restart clears
`HP_SYSTEM_ECC_MEM_FORCE_PD` but leaves those other two power bits unchanged.

The launcher's ESP-IDF 5.5.2 P4 driver has empty ECC power-up/down helpers, so it
assumes the power-on memory state. It can therefore inherit powered-down memory
and fail TLS signature verification. Wi-Fi connectivity alone does not establish
that the main CPU's cryptographic hardware is usable.

References: [IDF 5.5.2 ECC HAL][ecc552], [ECDSA driver][ecdsa552], and
[Mbed TLS error definition][ecp].

## Fix

`components/spacebattles/platform/launcher.c` owns `sb_platform_exit()`. After the
owner task has completed its synchronous crypto work, it clears the two ECC RAM
power-down bits and restores `FORCE_PU`. It preserves unrelated register fields
and then calls the existing `bsp_device_restart_to_launcher()` API. All normal
and error exits use this boundary.

The repair logs `Launcher ECC RAM power: 0x4 -> 0x2` when changing the expected
post-pairing state. Wi-Fi teardown and certificate verification behavior are
unchanged. No launcher or SDK patch is needed.

## Validation

The owner tested Host and Join and reported that the fix works. Both device runs
identify the tested ELF as `575bb69ca…`, show the expected power-state repair,
and successfully load Repository categories, projects and all 64 icons after
returning to launcher v0.2.5. The previous TLS error is absent from both runs.
This hardware acceptance concerns the exit fix; the public-source removal of
the device MAC allowlist remains a separate change without new hardware testing.

The normal ESP-IDF 6.0.2 build and launcher, lifecycle, radio, multiplayer and SD
host regression checks passed for the accepted implementation. Run the focused
handoff test with:

```sh
python3 tools/test-launcher.py
```

It models the broken IDF 6 to IDF 5 transition, exercises the production exit
function with all combinations of the three power bits, verifies unrelated bits
are preserved, and checks the fallback if the BSP restart unexpectedly returns.
It also checks that non-P4 targets do not change the register. Host simulation
does not replace testing the rebuilt firmware on hardware.

Existing SD teardown, radio retry and I2C diagnostics remain outside this fix.
The successful tests do not establish a new extended multiplayer or endurance
result. Raw device logs and local build artifacts are not included here.

[ecc552]: https://github.com/espressif/esp-idf/blob/v5.5.2/components/hal/esp32p4/include/hal/ecc_ll.h
[ecdsa552]: https://github.com/espressif/esp-idf/blob/v5.5.2/components/mbedtls/port/ecdsa/ecdsa_alt.c
[ecp]: https://github.com/Mbed-TLS/mbedtls/blob/v3.6.4/include/mbedtls/ecp.h#L36
