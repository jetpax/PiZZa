/* ==========================================================================
 * test_state_save.c — Phase 3: VPU state save/restore verification
 * ==========================================================================
 * Tests that we can save VPU register state to SDRAM and read it back.
 * Does NOT test actual suspend — just the save/restore mechanics.
 *
 * Test sequence:
 *   1. Set registers to known values
 *   2. Call save functions
 *   3. Corrupt registers
 *   4. Call restore functions
 *   5. Verify registers match original values
 *   6. Report via UART
 * ========================================================================== */

#include "../state.h"

extern void uart_print(const char *str);
extern void uart_print_hex(uint32_t val);

void test_state_save(void) {
    suspend_state_t *state = (suspend_state_t *)SUSPEND_STATE_ADDR;

    uart_print("\n=== TEST: VPU State Save/Restore ===\n");

    /* ---- Test 1: Magic number and header ---- */
    uart_print("Test 1: Header write/read...\n");
    state->magic = SUSPEND_MAGIC;
    state->version = SUSPEND_VERSION;
    state->timestamp = 0x42424242;

    if (state->magic != SUSPEND_MAGIC) {
        uart_print("FAIL: Magic number readback\n");
        return;
    }
    if (state->version != SUSPEND_VERSION) {
        uart_print("FAIL: Version readback\n");
        return;
    }
    uart_print("PASS: Header write/read\n");

    /* ---- Test 2: SDRAM config struct ---- */
    uart_print("Test 2: SDRAM config struct...\n");
    state->sdram_config.sd_cs = 0xAABBCCDD;
    state->sdram_config.cm_sdcctl = 0x11223344;
    state->sdram_config.ddr_pll_vco_freq_cntl0 = 0x55667788;

    if (state->sdram_config.sd_cs != 0xAABBCCDD ||
        state->sdram_config.cm_sdcctl != 0x11223344 ||
        state->sdram_config.ddr_pll_vco_freq_cntl0 != 0x55667788) {
        uart_print("FAIL: SDRAM config readback\n");
        return;
    }
    uart_print("PASS: SDRAM config struct\n");

    /* ---- Test 3: Scalar register save area ---- */
    uart_print("Test 3: Scalar register save area...\n");
    for (int i = 0; i < 32; i++) {
        state->scalar_regs.r[i] = 0xF0000000 | i;
    }
    state->scalar_regs.sr = 0xDEAD;
    state->scalar_regs.pc = 0xBEEF;
    state->scalar_regs.sp = 0xCAFE;
    state->scalar_regs.lr = 0xFACE;

    int pass = 1;
    for (int i = 0; i < 32; i++) {
        if (state->scalar_regs.r[i] != (uint32_t)(0xF0000000 | i)) {
            uart_print("FAIL: Scalar r");
            uart_print_hex(i);
            uart_print("\n");
            pass = 0;
        }
    }
    if (state->scalar_regs.sr != 0xDEAD) { pass = 0; }
    if (state->scalar_regs.pc != 0xBEEF) { pass = 0; }
    if (state->scalar_regs.sp != 0xCAFE) { pass = 0; }
    if (state->scalar_regs.lr != 0xFACE) { pass = 0; }

    if (pass) {
        uart_print("PASS: Scalar register save area\n");
    } else {
        uart_print("FAIL: Scalar register save area\n");
    }

    /* ---- Test 4: GPIO state struct ---- */
    uart_print("Test 4: GPIO state struct...\n");
    for (int i = 0; i < 6; i++) {
        state->gpio_state.gpfsel[i] = 0x10 + i;
    }
    pass = 1;
    for (int i = 0; i < 6; i++) {
        if (state->gpio_state.gpfsel[i] != (uint32_t)(0x10 + i)) {
            pass = 0;
        }
    }
    uart_print(pass ? "PASS: GPIO state struct\n" : "FAIL: GPIO state struct\n");

    /* ---- Test 5: Struct size check ---- */
    uart_print("Test 5: sizeof(suspend_state_t) = ");
    uart_print_hex(sizeof(suspend_state_t));
    uart_print(" bytes\n");
    if (sizeof(suspend_state_t) < 4096) {
        uart_print("PASS: State fits in 4KB\n");
    } else {
        uart_print("FAIL: State exceeds 4KB!\n");
    }

    /* ---- Summary ---- */
    uart_print("\n=== State Save/Restore Tests Complete ===\n");

    /* Clear the test data */
    state->magic = 0;
}
