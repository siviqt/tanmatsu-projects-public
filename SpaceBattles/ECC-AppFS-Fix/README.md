# ESP32-P4 ECC memory repair before returning to AppFS

This is the SpaceBattles Release 6 handoff fix, packaged as a reusable ESP-IDF
component. `launcher.c` is byte-for-byte identical to the application's
`components/spacebattles/platform/launcher.c`. It is not a complete game or a
launcher/SDK fork. Code is MIT licensed; see `LICENSE`.

## Problem and observed symptoms

After SpaceBattles used LoRa Host/Join pairing and exited to the Tanmatsu launcher,
Repository HTTPS downloads failed with `mbedtls_ssl_handshake returned -0x4E00`.
Wi-Fi still connected and obtained an IP address. Exiting without entering LoRa
was followed by successful Repository downloads in the original capture.
The failure concerns cryptographic state inherited **after** the AppFS return;
the capture does not establish that the launcher restart itself failed.

`-0x4E00` is `MBEDTLS_ERR_ECP_VERIFY_FAILED`, an elliptic-curve signature
verification error. It alone does not identify certificate expiry, a bad CA store,
or a Wi-Fi problem. SD teardown, radio retry and I2C diagnostics seen in the same
logs are separate issues. This fix does not establish the cause of an earlier
NTP timeout and does not bypass any certificate checks.

## Root cause and affected path

The examined combination is Tanmatsu/ESP32-P4, SpaceBattles on ESP-IDF **6.0.2**,
and launcher **v0.2.5** on ESP-IDF **5.5.2**, with hardware ECC/ECDSA verification.
Here ECC means elliptic-curve cryptography memory, not PSRAM error correction.

1. SpaceBattles `net/crypto.c` generates a P-256 key through PSA crypto during
   Host/Join pairing (`sb_net_keygen`, `psa_generate_key`).
2. IDF 6.0.2's P4 `ecc_ll_power_down()` clears `ECC_MEM_FORCE_PU` and sets
   `ECC_MEM_PD` when the accelerator is released.
3. The P4 CPU restart path resets crypto peripherals and clears
   `ECC_MEM_FORCE_PD`; it does not restore the other two memory-power bits.
4. IDF 5.5.2's P4 `ecc_ll_power_up()` and `ecc_ll_power_down()` are empty.
   Its ECDSA driver calls `esp_crypto_ecc_enable_periph_clk(true)`, which calls
   the empty power-up helper. It therefore inherits powered-down ECC memory.
5. Subsequent launcher TLS signature verification can fail even with working
   networking. AppFS restart is not equivalent to restoring all power-on state.

The control register is `HP_SYSTEM_ECC_PD_CTRL_REG`. For the examined P4 headers:

| Field | Bit | Required handoff | After IDF 6 ECC release |
| --- | ---: | ---: | ---: |
| `HP_SYSTEM_ECC_MEM_FORCE_PD` | 0 | 0 | 0 |
| `HP_SYSTEM_ECC_MEM_FORCE_PU` | 1 | 1 | 0 |
| `HP_SYSTEM_ECC_MEM_PD` | 2 | 0 | 1 |

Thus the observed low-bit transition is `0x4 -> 0x2`. Unrelated register fields
are preserved; other starting values are valid and are covered by the host test.

## Exact implementation

`sb_platform_exit()` in `launcher.c`:

- Compiles register access only under `CONFIG_IDF_TARGET_ESP32P4`.
- Reads the original register, clears `MEM_PD | MEM_FORCE_PD`, and sets `FORCE_PU`.
- Logs the before/after values only if they changed.
- Calls `bsp_device_restart_to_launcher()` using the normal Badge BSP API.
- If that API unexpectedly returns, waits indefinitely with `vTaskDelay` rather
  than returning into a partially stopped application or repeatedly restarting.

Badge BSP 1.4.0 clears the AppFS retained boot selection before `esp_restart()`:
with `CONFIG_APPFS_USE_RTC_REG` it clears the retained register, otherwise it
clears the magic in custom RTC retained memory. A plain `esp_restart()` is not
an equivalent replacement for that launcher-selection step.

The component needs `bsp/device.h`, ESP logging, FreeRTOS and the P4 `soc` headers.
Its minimal `include/platform/tanmatsu.h` contains only the no-return declaration.
The provided CMake file compiles exactly one `launcher.c`; the manifest pins the
examined IDF and BSP. No SDK source patch or cryptography disable switch is needed.

## Integration into another compatible application

1. Confirm the board, launcher, IDF versions, power-register definitions and
   restart behavior. This is a version-specific compatibility repair, not a
   general restart workaround for all ESP chips or future IDF releases.
2. Copy this directory to `your-project/components/ecc_appfs_fix/`. Keep its
   `CMakeLists.txt`, `idf_component.yml`, `launcher.c`, and `include/` directory.
   The README, license and host test can accompany the component.
3. In the **project's** `sdkconfig.defaults`, merge the supplied defaults. They
   select Tanmatsu and reserve 256 bytes of custom RTC space for the examined
   AppFS/BSP route. IDF does not automatically apply a component's defaults file.
   Retain the rest of your application's board/display/PSRAM configuration.
   Set the project target to `esp32p4`. If a generated `sdkconfig` already exists,
   ensure these settings take effect there too; defaults do not override it.
4. Add `ecc_appfs_fix` to the caller component's `PRIV_REQUIRES` (or `REQUIRES`
   when exposed through a public header). Include `platform/tanmatsu.h` and call
   `sb_platform_exit()` at your final launcher-return boundary.
5. Remove or replace any older definition of `sb_platform_exit()`. Do not link
   two implementations. If your project already has a different header with the
   same name, move the declaration into that header or rename both the supplied
   minimal header and its include in `launcher.c`.
6. Complete all cryptographic work, prevent new ECC operations, and finish the
   application's orderly cleanup **before** calling the function. Call from a
   task, not an ISR. There must be no asynchronous crypto task, interrupt or
   shutdown handler that can power ECC RAM down again before restart.
7. Build with the normal ESP-IDF 6.0.2 workflow. Install the application image
   through the existing launcher/AppFS. Do not flash a standalone partition
   table or bootloader over the launcher to install this application fix.

For SpaceBattles itself, this repair is already compiled from its normal
component. **Do not also copy this package into its `components/` directory.**
The root-level `ECC-AppFS-Fix/` package is deliberately outside that discovery path.

### Usage and shutdown ordering

```c
#include "platform/tanmatsu.h"

/* At your application's final exit boundary, in task context:
 * - stop accepting work and finish/join all crypto operations;
 * - finish bounded protocol departure and worker shutdown;
 * - finish persistence and required peripheral cleanup;
 * - keep any memory still referenced by live callbacks valid.
 */
sb_platform_exit();  // Repairs ECC state, selects launcher, restarts; never returns.
```

No special ECC-fix initialization is required. Do not run the repair at startup
or before a final crypto call: that call could power the memory down again.

In the actual SpaceBattles flow, `app/versus_run.c` finishes bounded reliable
LEAVE, calls `sb_radio_close()`, restores SD ownership when possible, and clears
session/key material before returning to the application owner. Crypto calls are
synchronous on that owner; the radio worker handles transport, not ECC operations.
`app/application.c` presents the exit screen, saves settings, calls
`sb_platform_sd_shutdown()`, waits for the minimum display duration, calls
`sb_renderer_shutdown()`, then calls `sb_platform_exit()`. On a radio close timeout,
`platform/radio.c` uses this same final exit boundary. Live transport/display
callbacks and their storage must remain valid until reset; indiscriminately
freeing them or tearing down the shared SDMMC controller is not part of this fix.
An error exit may skip normal cleanup, but still must satisfy the crypto ordering.

## Constraints and things to avoid

- The register operations are read/modify/write, not an ECC-locking API. The
  caller must guarantee no concurrent ECC users. Review this again if moving
  crypto off the owner task or adding HTTPS/background crypto to the application.
- Preserve the BSP launcher selection and existing hardware ownership rules.
- Do not reset the entire register: unrelated bits must survive.
- Do not disable TLS verification, force certificate acceptance, or disable
  hardware crypto to hide the failure. Those changes are absent here.
- This is not a patch to AppFS storage, SD recovery, Wi-Fi initialization, time
  synchronization, or PSRAM ECC. It does not guarantee other diagnostics vanish.
- The non-P4 branch only calls the BSP restart. Its register-free behavior is
  model-tested; support for other boards is not established by this package.

## Verification

Run from this package directory on a Linux host with Python 3 and a C compiler
supporting UndefinedBehaviorSanitizer:

```sh
python3 test-launcher.py
```

The harness compiles **this package's** `launcher.c`. It first models the broken
IDF 6-to-5 state, then tests every combination of the three power bits, preservation
of unrelated bits, one BSP restart, the unexpected-return fallback, and the
non-P4 register-free path. This is a register model, not a hardware TLS test.
The application's equivalent suite is `python3 tools/test-launcher.py`.
On 2026-10-14 this component also compiled and linked in a separate minimal
ESP-IDF 6.0.2 application using its own manifest, header and CMake registration.

For device acceptance:

1. Start from a power cycle; verify launcher Repository category/project downloads.
2. Launch the application, exit without pairing, and repeat those downloads.
3. Launch again, perform Host pairing far enough to generate/use P-256 keys, then
   exit. Capture the exit log and reopen Repository without a power cycle.
4. Repeat as Join. Confirm category, project and icon downloads succeed, with no
   `-0x4E00` handshake failure. Where the register was powered down, expect:

   ```text
   Platform: Launcher ECC RAM power: 0x4 -> 0x2
   SpaceBattles: Returning to launcher
   ```

5. Repeat across rematches and available error exits, then verify normal settings,
   local play, radio traffic and subsequent launches. Use a matching application
   ELF for decoding its logs and the launcher's ELF for launcher addresses.

The owner accepted this exact exit implementation after Host and Join on
2026-10-13. The inspected capture shows two runs of ELF prefix `575bb69ca`, both
repairing `0x4 -> 0x2` and completing Repository downloads. The console does not
label the roles; those come from the owner's report. New builds and host tests
on 2026-10-14 do not constitute a fresh hardware test of the combined MAC-unlocked
application. No connected serial device was available during consolidation.

## Source references

The implementation and explanation were checked against installed IDF 6.0.2,
the saved 5.5.2 SDK source files from the original investigation, Badge BSP 1.4.0,
and the actual final application and device capture. Upstream reference paths:

- [IDF 6.0.2 P4 ECC HAL](https://github.com/espressif/esp-idf/blob/v6.0.2/components/esp_hal_security/esp32p4/include/hal/ecc_ll.h)
- [IDF 6.0.2 P4 restart](https://github.com/espressif/esp-idf/blob/v6.0.2/components/esp_system/port/soc/esp32p4/system_internal.c)
- [IDF 5.5.2 P4 ECC HAL](https://github.com/espressif/esp-idf/blob/v5.5.2/components/hal/esp32p4/include/hal/ecc_ll.h)
- [IDF 5.5.2 crypto clocks](https://github.com/espressif/esp-idf/blob/v5.5.2/components/esp_security/src/esp_crypto_periph_clk.c)
- [IDF 5.5.2 ECDSA driver](https://github.com/espressif/esp-idf/blob/v5.5.2/components/mbedtls/port/ecdsa/ecdsa_alt.c)
