#!/usr/bin/env python3
"""Exercise the production launcher handoff across IDF 6 -> IDF 5 ECC state."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    d = Path(tmp)
    (d / "stub.h").write_text(r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#define HP_SYSTEM_ECC_PD_CTRL_REG 0x500e51d4u
#define HP_SYSTEM_ECC_MEM_FORCE_PD (1u << 0)
#define HP_SYSTEM_ECC_MEM_FORCE_PU (1u << 1)
#define HP_SYSTEM_ECC_MEM_PD (1u << 2)
#define portMAX_DELAY UINT32_MAX
extern uint32_t ecc_power;
static inline uint32_t *reg(uint32_t address) {
    assert(address == HP_SYSTEM_ECC_PD_CTRL_REG);
    return &ecc_power;
}
#define REG_READ(addr) (*reg(addr))
#define REG_CLR_BIT(addr, mask) (*reg(addr) &= ~(mask))
#define REG_SET_BIT(addr, mask) (*reg(addr) |= (mask))
#define ESP_LOGI(...) test_log(__VA_ARGS__)
void test_log(const char *, const char *, ...);
void bsp_device_restart_to_launcher(void);
void vTaskDelay(uint32_t);
void sb_platform_exit(void) __attribute__((noreturn));
''')
    for name in ("platform/tanmatsu.h", "bsp/device.h", "esp_log.h",
                 "freertos/FreeRTOS.h", "freertos/task.h", "sdkconfig.h",
                 "soc/hp_system_reg.h", "soc/soc.h"):
        p = d / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "stub.h"\n')
    (d / "test.c").write_text(r'''
#include "stub.h"
uint32_t ecc_power;
static uint32_t unrelated;
static unsigned restarts;
static bool bsp_returns;
static jmp_buf handoff;
void test_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
static bool legacy_ecc_available(void) {
    /* IDF 5.5.2 ecc_ll_power_up() does nothing. */
    return !(ecc_power & (HP_SYSTEM_ECC_MEM_FORCE_PD | HP_SYSTEM_ECC_MEM_PD));
}
void bsp_device_restart_to_launcher(void) {
    ++restarts;
#if CONFIG_IDF_TARGET_ESP32P4
    assert((ecc_power & 7u) == HP_SYSTEM_ECC_MEM_FORCE_PU);
    /* Model IDF 6.0.2 CPU restart, which only clears FORCE_PD. */
    ecc_power &= ~HP_SYSTEM_ECC_MEM_FORCE_PD;
    assert(legacy_ecc_available());
#endif
    assert((ecc_power & ~7u) == unrelated);
    if (!bsp_returns) longjmp(handoff, 1);
}
void vTaskDelay(uint32_t ticks) {
    assert(bsp_returns && ticks == portMAX_DELAY && restarts == 1);
    longjmp(handoff, 2);
}
int main(void) {
    /* Reproduce the broken transition independently of the production fix. */
    ecc_power = HP_SYSTEM_ECC_MEM_PD;
    ecc_power &= ~HP_SYSTEM_ECC_MEM_FORCE_PD;
    assert(!legacy_ecc_available());
    for (unsigned bits = 0; bits < 8; ++bits) {
        for (unsigned returns = 0; returns < 2; ++returns) {
            unrelated = 0xa5a50000;
            ecc_power = unrelated | bits;
            restarts = 0;
            bsp_returns = returns;
            int result = setjmp(handoff);
            if (!result) sb_platform_exit();
            assert(result == (returns ? 2 : 1) && restarts == 1);
#if !CONFIG_IDF_TARGET_ESP32P4
            assert(ecc_power == (unrelated | bits));
#endif
        }
    }
    puts("Launcher handoff passed: ECC power, retained fields, BSP fallback");
}
''')
    for p4 in (1, 0):
        exe = d / f"test-{p4}"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-fsanitize=undefined", f"-DCONFIG_IDF_TARGET_ESP32P4={p4}",
            f"-I{d}", str(root / "launcher.c"),
            str(d / "test.c"), "-o", str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)
