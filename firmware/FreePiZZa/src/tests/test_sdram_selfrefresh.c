/* ==========================================================================
 * test_sdram_selfrefresh.c — Phase 2: SDRAM self-refresh PoC
 * ==========================================================================
 * Tests the ability to enter and exit SDRAM self-refresh mode without
 * losing data. This is the most critical test — if this fails, the
 * entire suspend-to-RAM approach is unviable.
 *
 * Test sequence:
 *   1. Write known pattern to multiple SDRAM locations
 *   2. Enter self-refresh
 *   3. Wait N milliseconds
 *   4. Exit self-refresh
 *   5. Read back and verify pattern
 *   6. Report pass/fail via UART
 *
 * Run on hardware via UART to observe results.
 * ========================================================================== */

#include "../state.h"

/* Test pattern addresses — spread across SDRAM to test all banks */
#define TEST_ADDR_1     0x00100000  /* 1MB mark */
#define TEST_ADDR_2     0x08000000  /* 128MB mark */
#define TEST_ADDR_3     0x10000000  /* 256MB mark */
#define TEST_ADDR_4     0x1F000000  /* 496MB mark */

#define TEST_PATTERN_1  0xDEADBEEF
#define TEST_PATTERN_2  0xCAFEBABE
#define TEST_PATTERN_3  0x12345678
#define TEST_PATTERN_4  0xA5A5A5A5

/* Duration to stay in self-refresh (in approximate cycle counts) */
#define SELFREFRESH_HOLD_CYCLES  1000000  /* ~100ms at 250MHz VPU */

/* From suspend.c */
extern void uart_print(const char *str);
extern void uart_print_hex(uint32_t val);
extern void sdram_enter_selfrefresh(void);
extern void sdram_exit_selfrefresh(const sdram_config_t *cfg);
extern void save_sdram_config(sdram_config_t *cfg);

static volatile uint32_t *addr1 = (volatile uint32_t *)TEST_ADDR_1;
static volatile uint32_t *addr2 = (volatile uint32_t *)TEST_ADDR_2;
static volatile uint32_t *addr3 = (volatile uint32_t *)TEST_ADDR_3;
static volatile uint32_t *addr4 = (volatile uint32_t *)TEST_ADDR_4;

void test_sdram_selfrefresh(void) {
    int pass = 1;

    uart_print("\n=== TEST: SDRAM Self-Refresh ===\n");

    /* Step 1: Write patterns */
    uart_print("Writing test patterns...\n");
    *addr1 = TEST_PATTERN_1;
    *addr2 = TEST_PATTERN_2;
    *addr3 = TEST_PATTERN_3;
    *addr4 = TEST_PATTERN_4;

    /* Verify write */
    uart_print("Verifying pre-selfrefresh...\n");
    if (*addr1 != TEST_PATTERN_1 || *addr2 != TEST_PATTERN_2 ||
        *addr3 != TEST_PATTERN_3 || *addr4 != TEST_PATTERN_4) {
        uart_print("FAIL: Pre-selfrefresh verification failed!\n");
        return;
    }
    uart_print("Pre-selfrefresh: OK\n");

    /* Step 2: Save SDRAM config, then enter self-refresh */
    static sdram_config_t saved_cfg;
    save_sdram_config(&saved_cfg);
    uart_print("Entering self-refresh...\n");
    sdram_enter_selfrefresh();
    uart_print("In self-refresh.\n");  /* Note: UART may stop working here */

    /* Step 3: Hold */
    /* Simple delay — can't use timers if clocks are gated */
    for (volatile uint32_t i = 0; i < SELFREFRESH_HOLD_CYCLES; i++) {
        /* spin */
    }

    /* Step 4: Exit self-refresh */
    uart_print("Exiting self-refresh...\n");  /* May not print if UART clock gated */
    sdram_exit_selfrefresh(&saved_cfg);
    uart_print("Self-refresh exited.\n");

    /* Step 5: Verify */
    uart_print("Verifying post-selfrefresh...\n");

    if (*addr1 != TEST_PATTERN_1) {
        uart_print("FAIL at 0x00100000: got ");
        uart_print_hex(*addr1);
        uart_print("\n");
        pass = 0;
    }
    if (*addr2 != TEST_PATTERN_2) {
        uart_print("FAIL at 0x08000000: got ");
        uart_print_hex(*addr2);
        uart_print("\n");
        pass = 0;
    }
    if (*addr3 != TEST_PATTERN_3) {
        uart_print("FAIL at 0x10000000: got ");
        uart_print_hex(*addr3);
        uart_print("\n");
        pass = 0;
    }
    if (*addr4 != TEST_PATTERN_4) {
        uart_print("FAIL at 0x1F000000: got ");
        uart_print_hex(*addr4);
        uart_print("\n");
        pass = 0;
    }

    /* Step 6: Report */
    if (pass) {
        uart_print("=== PASS: SDRAM survived self-refresh! ===\n");
    } else {
        uart_print("=== FAIL: SDRAM data corrupted after self-refresh ===\n");
    }
}
