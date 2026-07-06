/* ==========================================================================
 * boot_suspend_hook.c -- Integration patch for upstream romstage.c
 * ==========================================================================
 * Provides two functions that plug into rpi-open-firmware's boot flow:
 *
 *   1. check_resume_from_suspend() -- called BEFORE sdram_init().
 *      If SDRAM holds a valid suspend state (magic + checksum), we skip
 *      the full re-init and jump directly to _resume_entry in resume.S.
 *
 *   2. enter_suspend_mode() -- called AFTER normal boot completes.
 *      Triggers suspend-to-RAM via suspend_enter().
 *
 * Usage: In upstream/firmware/romstage.c _main(), add:
 *
 *   #include "boot_suspend_hook.h"
 *
 *   int _main(...) {
 *       ...
 *       pl011_uart_init(115200);
 *       check_resume_from_suspend();   // <-- early, before sdram_init()
 *       ...
 *       sdram_init();
 *       ...
 *       // After platform init, instead of monitor_start():
 *       enter_suspend_mode();          // <-- triggers suspend
 *   }
 *
 * ========================================================================== */

#include <stdint.h>
#include "state.h"

extern void uart_print(const char *str);
extern void uart_print_hex(uint32_t val);

/* From suspend.c */
extern int suspend_enter(void);

/* From resume.S */
extern void _resume_entry(void);

/* ==========================================================================
 * check_resume_from_suspend -- early-boot resume path
 * ==========================================================================
 * Called before sdram_init(). At this point, SDRAM may still hold data
 * from the previous session if the self-refresh circuitry kept it alive.
 *
 * We can read SUSPEND_STATE_ADDR because:
 *   - The ROM bootloader maps SDRAM at 0x00000000 in the VPU address space
 *   - SDRAM in self-refresh still responds to reads once the controller
 *     is minimally re-enabled (which the ROM does before jumping to us)
 *   - If SDRAM lost power, reads return garbage and magic won't match
 *
 * If the state is valid, we:
 *   1. Skip sdram_init() (SDRAM is already initialized with data intact)
 *   2. Exit self-refresh (re-enable clocks, restart controller)
 *   3. Jump to _resume_entry which restores VPU registers
 *
 * If invalid (cold boot, SDRAM lost power, corruption), we return and
 * let the normal boot flow proceed with full sdram_init().
 * ========================================================================== */
void check_resume_from_suspend(void) {
    volatile suspend_state_t *state =
        (volatile suspend_state_t *)SUSPEND_STATE_ADDR;

    /* Quick check: does the magic value match? */
    if (state->magic != SUSPEND_MAGIC) {
        return;  /* Cold boot -- proceed normally */
    }

    /* Version check */
    if (state->version != SUSPEND_VERSION) {
        uart_print("[resume] State version mismatch, cold booting\n");
        return;
    }

    /* Validate checksum.
     * compute_checksum is static in suspend.c, so we inline the check. */
    {
        const uint32_t *words = (const uint32_t *)state;
        uint32_t num_words = sizeof(suspend_state_t) / sizeof(uint32_t);
        uint32_t cksum = 0;

        for (uint32_t i = 0; i < num_words; i++) {
            if (i == 2) continue;  /* Skip checksum field itself */
            cksum ^= words[i];
        }

        if (cksum != state->checksum) {
            uart_print("[resume] Checksum mismatch (");
            uart_print_hex(cksum);
            uart_print(" vs ");
            uart_print_hex(state->checksum);
            uart_print("), cold booting\n");
            return;
        }
    }

    uart_print("[resume] Valid suspend state found! Resuming...\n");

    /* Do NOT clear magic here -- resume.S re-validates it and clears it
     * after its own check. This prevents infinite resume loops: if we
     * crash mid-restore, next boot sees magic=0 and cold-boots. */

    /* Point of no return */
    void (*resume)(void) = (void (*)(void))state->resume_entry_addr;
    resume();

    /* Should never reach here */
    uart_print("[resume] ERROR: _resume_entry returned!\n");
}

/* ==========================================================================
 * enter_suspend_mode -- triggers suspend-to-RAM
 * ==========================================================================
 * Called after platform initialization is complete.
 * In a real deployment, this would be triggered by:
 *   - A GPIO input (button press)
 *   - A UART command
 *   - A timer (auto-suspend after N seconds of inactivity)
 *   - Immediately on boot (for pure sensor/wake use cases)
 *
 * For initial testing, we use a simple UART prompt.
 * ========================================================================== */
void enter_suspend_mode(void) {
    int ret;

    uart_print("\n[suspend] System ready. Entering suspend-to-RAM...\n");
    uart_print("[suspend] To wake: press GPIO17 button (pin 11 to GND)\n");

    ret = suspend_enter();

    if (ret == 0) {
        uart_print("[suspend] Resumed successfully!\n");
    } else {
        uart_print("[suspend] Resume failed with code: ");
        uart_print_hex((uint32_t)ret);
        uart_print("\n");
    }
}
