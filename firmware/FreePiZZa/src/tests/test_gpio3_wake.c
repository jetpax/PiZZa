/* ==========================================================================
 * test_gpio3_wake.c — Phase 4: GPIO3 wake interrupt test
 * ==========================================================================
 * Tests that GPIO3 can be configured as a wake source and that we can
 * detect edge events on it.
 *
 * Test sequence:
 *   1. Configure GPIO3 as input with pull-down
 *   2. Enable rising-edge detect
 *   3. Print "waiting..." and poll for edge event
 *   4. When GPIO3 is pulled HIGH (wire to 3V3), detect and report
 *
 * Hardware setup:
 *   - Connect a jumper wire to GPIO3 (pin 5) and GND (pin 6)
 *   - To trigger wake: briefly touch GPIO3 wire to 3V3 (pin 1)
 *   - Or use a button between GPIO3 and 3V3
 *
 * CAUTION: Do NOT short 3V3 to GND. Use a momentary connection.
 * ========================================================================== */

#include "../state.h"

/* BCM2835 GPIO registers (VPU-side addresses) */
#define GPIO_BASE       0x7E200000
#define GPFSEL0         (*(volatile uint32_t *)(GPIO_BASE + 0x00))
#define GPLEV0          (*(volatile uint32_t *)(GPIO_BASE + 0x34))
#define GPEDS0          (*(volatile uint32_t *)(GPIO_BASE + 0x40))
#define GPREN0          (*(volatile uint32_t *)(GPIO_BASE + 0x4C))
#define GPPUD           (*(volatile uint32_t *)(GPIO_BASE + 0x94))
#define GPPUDCLK0       (*(volatile uint32_t *)(GPIO_BASE + 0x98))

/* System Timer for timestamps */
#define ST_CLO          (*(volatile uint32_t *)(0x7E003004))

extern void uart_print(const char *str);
extern void uart_print_hex(uint32_t val);

static void delay_cycles(uint32_t n) {
    for (volatile uint32_t i = 0; i < n; i++) { }
}

void test_gpio3_wake(void) {
    uart_print("\n=== TEST: GPIO3 Wake Interrupt ===\n");

    /* ---- Step 1: Configure GPIO3 as input ---- */
    uart_print("Configuring GPIO3 as input...\n");

    /* GPIO3 function select is in GPFSEL0 bits [11:9] */
    /* 000 = input */
    uint32_t fsel = GPFSEL0;
    fsel &= ~(0x7 << 9);   /* Clear bits [11:9] */
    GPFSEL0 = fsel;

    /* ---- Step 2: Enable pull-down on GPIO3 ---- */
    uart_print("Enabling pull-down on GPIO3...\n");

    GPPUD = 0x01;           /* Enable pull-down */
    delay_cycles(150);
    GPPUDCLK0 = (1 << 3);  /* Clock it into GPIO3 */
    delay_cycles(150);
    GPPUD = 0x00;
    GPPUDCLK0 = 0x00;

    /* ---- Step 3: Enable rising-edge detect ---- */
    uart_print("Enabling rising-edge detect on GPIO3...\n");
    GPREN0 |= (1 << 3);

    /* Clear any pending events */
    GPEDS0 = (1 << 3);     /* Write 1 to clear */

    /* ---- Step 4: Read current level ---- */
    uint32_t level = (GPLEV0 >> 3) & 1;
    uart_print("GPIO3 current level: ");
    uart_print_hex(level);
    uart_print("\n");

    /* ---- Step 5: Wait for rising edge ---- */
    uart_print("Waiting for GPIO3 rising edge...\n");
    uart_print("(Connect GPIO3/pin5 to 3V3/pin1 momentarily)\n");

    uint32_t start = ST_CLO;
    uint32_t count = 0;

    while (!(GPEDS0 & (1 << 3))) {
        count++;
        if (count % 10000000 == 0) {
            uint32_t elapsed = ST_CLO - start;
            uart_print("  Still waiting... ");
            uart_print_hex(elapsed / 1000000);
            uart_print("s elapsed\n");
        }
    }

    /* ---- Step 6: Edge detected! ---- */
    uint32_t wake_time = ST_CLO;
    uint32_t elapsed = wake_time - start;

    uart_print("\n!!! GPIO3 RISING EDGE DETECTED !!!\n");
    uart_print("Time waiting: ");
    uart_print_hex(elapsed);
    uart_print(" microseconds\n");

    /* Clear the event */
    GPEDS0 = (1 << 3);

    /* Read level to confirm */
    level = (GPLEV0 >> 3) & 1;
    uart_print("GPIO3 level after wake: ");
    uart_print_hex(level);
    uart_print("\n");

    /* ---- Cleanup ---- */
    /* Disable rising-edge detect */
    GPREN0 &= ~(1 << 3);

    uart_print("\n=== PASS: GPIO3 wake detection works! ===\n");
}
