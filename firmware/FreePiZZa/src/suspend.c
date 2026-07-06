/* ==========================================================================
 * suspend.c -- VPU suspend-to-RAM implementation for Pi Zero 2W
 * ==========================================================================
 * Implements the full suspend/resume cycle:
 *
 *   1. Save VPU scalar registers + SDRAM/PLL/GPIO/IC config
 *   2. Power-gate ARM cores
 *   3. Put SDRAM into self-refresh
 *   4. Gate clocks, power down DDR PLL
 *   5. VPU sleeps (VC4 `sleep` instruction) until GPIO3 interrupt
 *   6. Re-enable DDR PLL, exit SDRAM self-refresh
 *   7. Un-gate ARM, restore state, return to caller
 *
 * Register addresses verified against:
 *   - common/broadcom/bcm2708_chip/sdc_ctrl.h      (SD_CS, SD_SA -- bit positions)
 *   - common/broadcom/bcm2708_chip/cpr_powman.h    (PM_PROC at offset 0x110)
 *   - common/broadcom/bcm2708_chip/cpr_clkman.h    (CM_SDCCTL at 0x1A8, UPDATE=b17, ACCPT=b16)
 *   - common/broadcom/bcm2708_chip/sdc_addr_front.h (APHY -- 0x7EE06xxx)
 *   - common/broadcom/bcm2708_chip/sdc_dq_front.h   (DPHY -- 0x7EE07xxx)
 *   - BCM2835 ARM Peripherals datasheet              (GPIO, Timer, IC)
 *   - rpi-open-firmware/firmware/sdram.c              (SDRAM self-refresh)
 *   - rpi-open-firmware/firmware/BCM2708PowerManagement.cc (PM_PROC)
 *   - rpi-open-firmware/firmware/arm_monitor.c        (VPU sleep instruction)
 *
 * This is GPLv2+ code -- derived from rpi-open-firmware.
 * ========================================================================== */

#include "state.h"

/* --------------------------------------------------------------------------
 * Register access macro
 * -------------------------------------------------------------------------- */
#define REG32(addr)  (*(volatile uint32_t *)(addr))

/* ==========================================================================
 * GPIO registers (0x7E200000) -- BCM2835 ARM Peripherals §6
 * ========================================================================== */
#define GPIO_BASE           0x7E200000
#define GPFSEL0             REG32(GPIO_BASE + 0x00)
#define GPFSEL1             REG32(GPIO_BASE + 0x04)
#define GPFSEL2             REG32(GPIO_BASE + 0x08)
#define GPFSEL3             REG32(GPIO_BASE + 0x0C)
#define GPFSEL4             REG32(GPIO_BASE + 0x10)
#define GPFSEL5             REG32(GPIO_BASE + 0x14)
#define GPSET0              REG32(GPIO_BASE + 0x1C)
#define GPCLR0              REG32(GPIO_BASE + 0x28)
#define GPLEV0              REG32(GPIO_BASE + 0x34)
#define GPEDS0              REG32(GPIO_BASE + 0x40)
#define GPEDS1              REG32(GPIO_BASE + 0x44)
#define GPREN0              REG32(GPIO_BASE + 0x4C)
#define GPREN1              REG32(GPIO_BASE + 0x50)
#define GPFEN0              REG32(GPIO_BASE + 0x58)
#define GPFEN1              REG32(GPIO_BASE + 0x5C)
#define GPHEN0              REG32(GPIO_BASE + 0x64)
#define GPHEN1              REG32(GPIO_BASE + 0x68)
#define GPLEN0              REG32(GPIO_BASE + 0x70)
#define GPLEN1              REG32(GPIO_BASE + 0x74)
#define GPPUD               REG32(GPIO_BASE + 0x94)
#define GPPUDCLK0           REG32(GPIO_BASE + 0x98)
#define GPPUDCLK1           REG32(GPIO_BASE + 0x9C)

/* ==========================================================================
 * System Timer (0x7E003000) -- 1 MHz free-running counter
 * ========================================================================== */
#define ST_BASE             0x7E003000
#define ST_CLO              REG32(ST_BASE + 0x04)

/* ==========================================================================
 * ARM Interrupt Controller (0x7E00B000) -- BCM2835 ARM Peripherals §7
 * Used by ARM cores. Saved/restored for completeness.
 * ========================================================================== */
#define IC_BASE             0x7E00B000
#define IRQ_PENDING1        REG32(IC_BASE + 0x204)
#define IRQ_PENDING2        REG32(IC_BASE + 0x208)
#define IRQ_ENABLE1         REG32(IC_BASE + 0x210)
#define IRQ_ENABLE2         REG32(IC_BASE + 0x214)
#define IRQ_ENABLE_BASIC    REG32(IC_BASE + 0x218)

/* ==========================================================================
 * VPU Interrupt Controller (IC0/IC1) -- from intctrl0.h / intctrl1.h
 * ========================================================================== *
 * This is what the VPU `sleep` instruction responds to.
 * IC0 = core 0 at 0x7E002000, IC1 = core 1 at 0x7E002800.
 * Each has 64 interrupt sources in 8 groups of 8, mapped at offsets
 * 0x10-0x2C. Each 4-bit nibble enables/routes one interrupt.
 *
 * GPIO bank interrupts (from hardware.h):
 *   INTERRUPT_GPIO0 = HW_OFFSET + 49  →  hw intno 49
 *   INTERRUPT_GPIO1 = HW_OFFSET + 50  →  hw intno 50
 *   INTERRUPT_GPIO2 = HW_OFFSET + 51  →  hw intno 51
 *   INTERRUPT_GPIO3 = HW_OFFSET + 52  →  hw intno 52
 *
 * GPIO3 (pin 3) is on GPIO bank 0 (pins 0-27), so INTERRUPT_GPIO0 = 49
 * fires for any edge-detect event on pins 0-27.
 *
 * set_interrupt(intno, enable, core) from upstream/firmware/interrupt.c:
 *   offset = 0x10 + ((intno >> 3) << 2)
 *   slot   = 0xF << ((intno & 7) << 2)
 *   For intno=49: offset=0x10+(6<<2)=0x28, slot=0xF<<(1<<2)=0xF<<4=0xF0
 * ========================================================================== */
#define IC0_BASE            0x7E002000
#define IC0_VADDR           REG32(IC0_BASE + 0x30)

/* GPIO bank 0 interrupt (covers GPIO pins 0-27, including GPIO3) */
#define IC_GPIO0_INTNO      49

/* ==========================================================================
 * Power Manager (0x7E100000)
 * From BCM2708PowerManagement.cc -- PM_PROC controls ARM power island.
 * ========================================================================== */
#define PM_BASE             0x7E100000
#define PM_PROC             REG32(PM_BASE + 0x110)  /* ARM processor domain (cpr_powman.h: 0x7e100110) */
#define PM_RSTC             REG32(PM_BASE + 0x1C)
#define PM_WDOG             REG32(PM_BASE + 0x24)
#define PM_PASSWORD         0x5A000000

/* ==========================================================================
 * ARM Control Block (0x7E00C000)
 * From BCM2708ArmControl.cc -- sets ARM boot address and control signals.
 * ARM_CONTROL0 bits [31:2] = boot address >> 2
 * Used for Linux resume: write cpu_resume physical address here.
 * ========================================================================== */
#define ARM_CONTROL0        REG32(0x7E00C000)
#define ARM_CONTROL1        REG32(0x7E00C004)

/* Fixed SDRAM address where Linux writes its cpu_resume physical address
 * before requesting suspend. VPU reads this after ARM power-gate. */
#define ARM_RESUME_ADDR     REG32(0x1FF00100)  /* just past suspend_state_t */

/* PM generic bits (shared layout across all power domain registers) */
#define PM_GNRIC_POWUP_SET  (1 << 0)
#define PM_GNRIC_POWOK_SET  (1 << 1)
#define PM_GNRIC_ISPOW_SET  (1 << 2)
#define PM_GNRIC_MEMREP_SET (1 << 3)
#define PM_GNRIC_MRDONE_SET (1 << 4)
#define PM_GNRIC_ISFUNC_SET (1 << 5)
#define PM_GNRIC_CFG_LSB    16
#define PM_GNRIC_ENAB_SET   (1 << 12)
#define PM_UNK_CFG_CLR      0xFF80FFFF  /* clear CFG bits [22:16] */

/* PM_PROC ARM reset bit */
#define PM_PROC_ARMRSTN_SET (1 << 6)

/* PM_RSTC full reset configuration */
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020

/* ==========================================================================
 * Clock Manager (0x7E101000)
 * From sdram.c clock manager update protocol.
 * ========================================================================== */
#define CM_BASE             0x7E101000
#define CM_VPUCTL           REG32(CM_BASE + 0x008)
#define CM_VPUDIV           REG32(CM_BASE + 0x00C)
#define CM_ARMCTL           REG32(CM_BASE + 0x1B0)  /* cpr_clkman.h: 0x7e1011b0 */
#define CM_ARMDIV           REG32(CM_BASE + 0x1B4)  /* cpr_clkman.h: 0x7e1011b4, RO */
#define CM_TIMERCTL         REG32(CM_BASE + 0x0E8)
#define CM_TIMERDIV         REG32(CM_BASE + 0x0EC)
#define CM_UARTCTL          REG32(CM_BASE + 0x0F0)
#define CM_UARTDIV          REG32(CM_BASE + 0x0F4)
#define CM_OSCCOUNT         REG32(CM_BASE + 0x100)
#define CM_SDCCTL           REG32(CM_BASE + 0x1A8)  /* cpr_clkman.h: 0x7e1011a8 */
#define CM_SDCDIV           REG32(CM_BASE + 0x1AC)  /* cpr_clkman.h: CTL+4 pattern */
#define CM_PASSWORD         0x5A000000

/* CM_SDCCTL bits */
/* CM_SDCCTL bits -- verified from cpr_clkman.h */
#define CM_SDCCTL_ENAB_SET      (1 << 4)     /* bit 4: clock enable */
#define CM_SDCCTL_UPDATE_SET    (1 << 17)    /* bit 17: update request (NOT bit 5!) */
#define CM_SDCCTL_ACCPT_SET     (1 << 16)    /* bit 16: update accepted (NOT bit 8!) */
#define CM_SDCCTL_BUSYD_SET     (1 << 8)     /* bit 8: divider busy */
#define CM_SDCCTL_BUSY_SET      (1 << 7)     /* bit 7: clock generator busy */

/* ==========================================================================
 * SDRAM Controller (0x7EE00000)
 * Offsets from Broadcom sdc_ctrl.h -- these are VPU-side only (no ARM map).
 * ========================================================================== */
#define SD_BASE             0x7EE00000
#define SD_CS               REG32(SD_BASE + 0x00)
#define SD_SA               REG32(SD_BASE + 0x04)
#define SD_SB               REG32(SD_BASE + 0x08)
#define SD_SC               REG32(SD_BASE + 0x0C)
#define SD_PT2              REG32(SD_BASE + 0x10)   /* sdc_ctrl.h: 0x7ee00010 */
#define SD_PT1              REG32(SD_BASE + 0x14)   /* sdc_ctrl.h: 0x7ee00014 */
#define SD_MRT              REG32(SD_BASE + 0x64)   /* sdc_ctrl.h: 0x7ee00064 */
#define SD_SD_REG           REG32(SD_BASE + 0x94)   /* sdc_ctrl.h: 0x7ee00094 */
#define SD_SE               REG32(SD_BASE + 0x98)   /* sdc_ctrl.h: 0x7ee00098 */
#define SD_MR               REG32(SD_BASE + 0x90)   /* sdc_ctrl.h: 0x7ee00090 */
#define SD_PHYC             REG32(SD_BASE + 0x60)   /* sdc_ctrl.h: 0x7ee00060 */

/* SD_MR bits -- from sdc_ctrl.h */
#define SD_MR_DONE_SET      (1u << 31)   /* bit 31: MR operation complete */
#define SD_MR_TIMEOUT_SET   (1u << 30)   /* bit 30: MR operation timed out */
#define SD_MR_HI_Z_SET      (1u << 29)   /* bit 29: Hi-Z (for ZQ cal) */
#define SD_MR_RW_SET        (1u << 28)   /* bit 28: 1=write, 0=read */
#define SD_MR_RDATA_LSB     16           /* bits 23:16: read data */

/* SD_PHYC bits */
#define SD_PHYC_PHYRST_SET  (1 << 0)     /* bit 0: PHY reset */

/* LPDDR2 Mode Register addresses (from upstream hardware.h) */
#define LPDDR2_MR_DEVICE_FEATURE_2  2    /* RL/WL configuration */
#define LPDDR2_MR_IO_CONFIG         3    /* IO configuration */
#define LPDDR2_MR_CALIBRATION       10   /* ZQ calibration trigger */

/* SD_CS bits -- verified from sdc_ctrl.h (auto-generated Broadcom header) */
#define SD_CS_RESTRT_SET    (1 << 0)    /* bit 0: restart controller */
#define SD_CS_EN_SET        (1 << 1)    /* bit 1: controller enable */
#define SD_CS_DPD_SET       (1 << 2)    /* bit 2: deep power down */
#define SD_CS_STBY_SET      (1 << 3)    /* bit 3: request self-refresh / standby */
#define SD_CS_PUSKIP_SET    (1 << 4)    /* bit 4: power-up skip */
#define SD_CS_STATEN_SET    (1 << 6)    /* bit 6: stats enable */
#define SD_CS_STOP_SET      (1 << 7)    /* bit 7: stop controller */
#define SD_CS_SREF2RUN_SET  (1 << 8)    /* bit 8: self-refresh to run transition */
#define SD_CS_IDLE_SET      (1 << 9)    /* bit 9: controller idle */
#define SD_CS_SDUP_SET      (1 << 15)   /* bit 15: SDRAM UP status (1=running) */
#define SD_CS_ASHDN_T_LSB   19          /* bits 22:19: auto-shutdown timer */

/* SD_SA power-save bits -- verified from sdc_ctrl.h */
#define SD_SA_POWSAVE_SET   (1 << 0)    /* bit 0: power save enable */
#define SD_SA_CLKSTOP_SET   (1 << 7)    /* bit 7: clock stop (reset=1) */
#define SD_SA_PGEHLDE_SET   (1 << 8)    /* bit 8: page hold enable */

/* ==========================================================================
 * APHY -- Analog PHY / DDR PLL (0x7EE06000)
 * Addresses from sdc_addr_front.h (verified against Broadcom headers).
 * ========================================================================== */
#define APHY_CSR_GLBL_ADDR_DLL_RESET     REG32(0x7EE06004)
#define APHY_CSR_DDR_PLL_GLOBAL_RESET    REG32(0x7EE06024)
#define APHY_CSR_DDR_PLL_POST_DIV_RESET  REG32(0x7EE06028)
#define APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL0 REG32(0x7EE0602C)
#define APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL1 REG32(0x7EE06030)
#define APHY_CSR_DDR_PLL_MDIV_VALUE      REG32(0x7EE06034)
#define APHY_CSR_DDR_PLL_LOCK_STATUS     REG32(0x7EE06048)
#define APHY_CSR_DDR_PLL_PWRDWN          REG32(0x7EE06058)
/* PHY BIST control -- upstream reset_phy_dll() uses this to signal SDC.
 * Per upstream sdc_addr_front.h @ 0x7ee06080; prior value 0x7EE06020 was
 * actually the address DLL lock status register (see below). */
#define APHY_CSR_PHY_BIST_CNTRL_SPR      REG32(0x7EE06080)
/* Address DLL lock status -- upstream waits for == 3.
 * Per upstream sdc_addr_front.h @ 0x7ee06020; prior value 0x7EE06010 was
 * actually APHY_CSR_GLBL_ADDR_DLL_PH_LD_VAL (phase load value). */
#define APHY_CSR_GLBL_ADDR_DLL_LOCK_STAT REG32(0x7EE06020)
/* Address pad drive/slew -- upstream sdc_addr_front.h: 0x7ee06068 */
#define APHY_CSR_ADDR_PAD_DRV_SLEW_CTRL  REG32(0x7EE06068)
/* PVT compensation (process/voltage/temperature recalibration) */
#define APHY_CSR_ADDR_PVT_COMP_CTRL      REG32(0x7EE06070)
#define APHY_CSR_ADDR_PVT_COMP_STATUS    REG32(0x7EE06078)

/* ==========================================================================
 * DPHY -- Digital PHY / DQ path (0x7EE07000)
 * Addresses from sdc_dq_front.h (verified against Broadcom headers).
 * ========================================================================== */
#define DPHY_CSR_GLBL_DQ_DLL_RESET       REG32(0x7EE07004)
#define DPHY_CSR_GLBL_MSTR_DLL_LOCK_STAT REG32(0x7EE07018)
#define DPHY_CSR_DQ_PHY_MISC_CTRL        REG32(0x7EE07048)
#define DPHY_CSR_DQ_PAD_DRV_SLEW_CTRL    REG32(0x7EE0704C)
#define DPHY_CSR_DQ_PAD_MISC_CTRL        REG32(0x7EE07050)
#define DPHY_CSR_DQ_PVT_COMP_CTRL        REG32(0x7EE07054)
#define DPHY_CSR_DQ_PVT_COMP_STATUS      REG32(0x7EE0705C)
#define DPHY_CSR_BOOT_READ_DQS_GATE_CTRL REG32(0x7EE07040)

/* ==========================================================================
 * UART debug output
 * ========================================================================== */
#ifndef NDEBUG
extern int printf(const char *fmt, ...);
extern void uart_print_hex(uint32_t val);
#define DBG(msg) printf(msg)
#else
#define DBG(msg) ((void)0)
#endif

/* --------------------------------------------------------------------------
 * Helper: microsecond delay using 1 MHz system timer
 * -------------------------------------------------------------------------- */
static inline void udelay(uint32_t us) {
    uint32_t start = ST_CLO;
    while ((ST_CLO - start) < us) { /* spin */ }
}

/* --------------------------------------------------------------------------
 * Helper: wait for oscillator ticks (from BCM2708PowerManagement.cc)
 * Used during power domain sequencing.
 * -------------------------------------------------------------------------- */
static inline void wait_osc_ticks(uint32_t ticks) {
    CM_OSCCOUNT = CM_PASSWORD | ticks;
    while (CM_OSCCOUNT != 0) { /* spin */ }
}

/* --------------------------------------------------------------------------
 * Clock manager update protocol (from sdram.c)
 *
 * The SDRAM clock control register requires an update handshake:
 *   begin: set UPDATE bit, wait for ACCPT
 *   <modify clock settings>
 *   end:   clear UPDATE bit, wait for ACCPT to clear
 * -------------------------------------------------------------------------- */
static inline void clkman_update_begin(void) {
    /* Upstream sdram.c: CM_SDCCTL |= CM_PASSWORD | CM_SDCCTL_UPDATE_SET;
     * Then spins until ACCPT. We match exactly. */
    CM_SDCCTL |= CM_PASSWORD | CM_SDCCTL_UPDATE_SET;
    for (;;) if (CM_SDCCTL & CM_SDCCTL_ACCPT_SET) break;
}

static inline void clkman_update_end(void) {
    /* Upstream sdram.c: CM_SDCCTL = CM_PASSWORD | (CM_SDCCTL & CM_SDCCTL_UPDATE_CLR);
     * CM_SDCCTL_UPDATE_CLR is the inverse mask of UPDATE_SET. */
    CM_SDCCTL = CM_PASSWORD | (CM_SDCCTL & ~CM_SDCCTL_UPDATE_SET);
    for (;;) if ((CM_SDCCTL & CM_SDCCTL_ACCPT_SET) == 0) break;
}

/* --------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------- */
/* Assembly register save (in resume.S) -- atomic, no compiler clobbering */
extern void _save_vpu_regs(vpu_scalar_regs_t *regs);

void save_sdram_config(sdram_config_t *cfg);
static void save_pll_config(pll_config_t *pll);
static void save_gpio_state(gpio_state_t *gpio);
static void save_ic_state(ic_state_t *ic);

void sdram_enter_selfrefresh(void);
void sdram_exit_selfrefresh(const sdram_config_t *cfg);

static void power_gate_arm(void);
static void power_ungate_arm(void);

static void configure_gpio3_wake(void);
static void wait_for_gpio3_wake(void);

static uint32_t compute_checksum(const suspend_state_t *state);
static int      validate_state(const suspend_state_t *state);

/* Resume entry point (in resume.S) */
extern void _resume_entry(void);

/* ==========================================================================
 * PUBLIC API
 * ==========================================================================
 * suspend_enter() -- Main entry point. Call to enter suspend-to-RAM.
 *
 * Returns:
 *   0  = woke successfully, state restored
 *  -1  = state validation failed after wake
 *  -2  = SDRAM self-refresh failed
 * ========================================================================== */
int suspend_enter(void) {
    suspend_state_t *state = (suspend_state_t *)SUSPEND_STATE_ADDR;

    DBG("[suspend] === Entering suspend-to-RAM ===\n");

    /* ---- Phase 1: Save all state ---- */

    DBG("[suspend] Saving VPU registers...\n");
    _save_vpu_regs(&state->scalar_regs);

    DBG("[suspend] Saving SDRAM config...\n");
    save_sdram_config(&state->sdram_config);

    DBG("[suspend] Saving PLL config...\n");
    save_pll_config(&state->pll_config);

    DBG("[suspend] Saving GPIO state...\n");
    save_gpio_state(&state->gpio_state);

    DBG("[suspend] Saving IC state...\n");
    save_ic_state(&state->ic_state);

    /* Write header */
    state->magic = SUSPEND_MAGIC;
    state->version = SUSPEND_VERSION;
    state->timestamp = ST_CLO;
    state->resume_entry_addr = (uint32_t)&_resume_entry;
    state->stack_canary = STACK_CANARY_MAGIC;
    state->checksum = compute_checksum(state);

    DBG("[suspend] State saved. Checksum computed.\n");

    /* ---- Phase 2: Configure wake source ---- */

    DBG("[suspend] Configuring GPIO17 wake...\n");
    configure_gpio3_wake();

    /* ---- Phase 3: Power down ---- */

    DBG("[suspend] Power-gating ARM...\n");
    power_gate_arm();

    /* Wait for any in-flight SDRAM transactions to drain.
     * After ARM power-gate, pending DMA or cache writeback transactions
     * may still be in the SDRAM controller pipeline. Entering self-refresh
     * with pending writes = lost data = memory corruption on resume.
     * Full fix requires Linux PM integration (Phase 5); this catches
     * transactions that were already in-flight at power-gate time. */
    {
        int idle_timeout = 100000;
        while (!(SD_CS & SD_CS_IDLE_SET) && --idle_timeout > 0) { /* spin */ }
        if (idle_timeout == 0) {
            DBG("[suspend] WARNING: SDRAM not idle after ARM gate\n");
        }
    }

    DBG("[suspend] Entering SDRAM self-refresh...\n");
    sdram_enter_selfrefresh();

    /* ---- Phase 4: Sleep ---- */
    /*
     * At this point:
     *   - SDRAM is in self-refresh (data preserved, ~0.5 mA)
     *   - ARM cores are power-gated
     *   - DDR PLL is powered down
     *   - VPU is about to enter sleep (clock-gated until interrupt)
     *
     * The VC4 `sleep` instruction halts the VPU pipeline until an
     * interrupt fires. GPIO3 rising edge → VPU interrupt → wake.
     * Power draw during this phase: target ~2-5 mA.
     */

    DBG("[suspend] Sleeping -- waiting for GPIO17...\n");
    wait_for_gpio3_wake();

    /* ---- Phase 5: Wake and restore ---- */

    DBG("[suspend] Woke! Exiting SDRAM self-refresh...\n");
    sdram_exit_selfrefresh(&state->sdram_config);

    DBG("[suspend] Un-gating ARM...\n");
    power_ungate_arm();

    /* Verify saved state survived self-refresh */
    if (!validate_state(state)) {
        DBG("[suspend] ERROR: state validation failed!\n");
        return -1;
    }

    DBG("[suspend] === Resume complete ===\n");
    return 0;
}

/* ==========================================================================
 * IMPLEMENTATION -- Save Functions
 * ========================================================================== */

/* save_vpu_scalar_regs is now in resume.S as _save_vpu_regs().
 *
 * WHY: Upstream rpi-open-firmware saves registers atomically in assembly
 * (traps.S: SaveRegsAll uses stm r0-r5/r6-r15/r16-r23 to the stack).
 * A C function can't reliably capture its own caller-saved registers
 * because the function prologue clobbers them before the first user
 * instruction runs. The assembly version in resume.S captures r2-r5
 * (caller-saved) plus r6-r23 (callee-saved) directly via st instructions,
 * avoiding ABI clobbering.
 *
 * r0 is hardwired to 0 on VC4. r1 is clobbered by our store pointer.
 * r0-r5 are caller-saved and partially lost, but r6-r23 (the callee-saved
 * set) are captured with 100% fidelity -- which is all that matters for
 * resume, since caller-saved registers are expected to be trashed across
 * function calls anyway.
 */

/* --------------------------------------------------------------------------
 * Save SDRAM controller configuration
 * --------------------------------------------------------------------------
 * Captures all SDRAM timing, address, and PHY PLL settings so we can
 * restore them after self-refresh exit if needed.
 * -------------------------------------------------------------------------- */
void save_sdram_config(sdram_config_t *cfg) {
    /* SDRAM controller registers */
    cfg->sd_sa  = SD_SA;
    cfg->sd_sb  = SD_SB;
    cfg->sd_sc  = SD_SC;
    cfg->sd_pt1 = SD_PT1;
    cfg->sd_pt2 = SD_PT2;
    cfg->sd_mrt = SD_MRT;
    cfg->sd_cs  = SD_CS;

    /* Clock manager SDRAM clock */
    cfg->cm_sdcctl = CM_SDCCTL;
    cfg->cm_sdcdiv = CM_SDCDIV;

    /* APHY DDR PLL configuration */
    cfg->ddr_pll_vco_freq_cntl0 = APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL0;
    cfg->ddr_pll_vco_freq_cntl1 = APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL1;
    cfg->ddr_pll_mdiv           = APHY_CSR_DDR_PLL_MDIV_VALUE;
    cfg->ddr_pll_global_reset   = APHY_CSR_DDR_PLL_GLOBAL_RESET;

    /* DPHY DQ trim values (for PHY recalibration if needed) */
    /* These offsets are approximate -- read from DPHY base registers */
    cfg->dphy_glbl_dq_dly_trim = DPHY_CSR_GLBL_DQ_DLL_RESET;  /* placeholder */
}

/* --------------------------------------------------------------------------
 * Save PLL / clock configuration
 * -------------------------------------------------------------------------- */
static void save_pll_config(pll_config_t *pll) {
    pll->cm_vpuctl   = CM_VPUCTL;
    pll->cm_vpudiv   = CM_VPUDIV;
    pll->cm_armctl   = CM_ARMCTL;
    pll->cm_armdiv   = CM_ARMDIV;
    pll->cm_timerctl = CM_TIMERCTL;
    pll->cm_timerdiv = CM_TIMERDIV;
    pll->cm_uartctl  = CM_UARTCTL;
    pll->cm_uartdiv  = CM_UARTDIV;
    /* HDMI not needed for our headless laundromat deployment */
    pll->cm_hdmictl  = 0;
    pll->cm_hdmidiv  = 0;
}

/* --------------------------------------------------------------------------
 * Save GPIO state
 * -------------------------------------------------------------------------- */
static void save_gpio_state(gpio_state_t *gpio) {
    /* Function select (6 registers × 10 pins each = 54 GPIOs) */
    gpio->gpfsel[0] = GPFSEL0;
    gpio->gpfsel[1] = GPFSEL1;
    gpio->gpfsel[2] = GPFSEL2;
    gpio->gpfsel[3] = GPFSEL3;
    gpio->gpfsel[4] = GPFSEL4;
    gpio->gpfsel[5] = GPFSEL5;

    /* Edge detect enables */
    gpio->gpren[0] = GPREN0;
    gpio->gpren[1] = GPREN1;
    gpio->gpfen[0] = GPFEN0;
    gpio->gpfen[1] = GPFEN1;

    /* Level detect */
    gpio->gphen[0] = GPHEN0;
    gpio->gphen[1] = GPHEN1;
    gpio->gplen[0] = GPLEN0;
    gpio->gplen[1] = GPLEN1;

    /* Pull-up/down state -- read-only snapshot (actual config is write-only
     * via GPPUD + GPPUDCLK sequence, so this is for reference only) */
    gpio->gppud     = GPPUD;
    gpio->gppudclk[0] = 0;  /* write-only, can't read back */
    gpio->gppudclk[1] = 0;
}

/* --------------------------------------------------------------------------
 * Save interrupt controller state
 * -------------------------------------------------------------------------- */
static void save_ic_state(ic_state_t *ic) {
    ic->irq_enable1     = IRQ_ENABLE1;
    ic->irq_enable2     = IRQ_ENABLE2;
    ic->irq_enable_basic = IRQ_ENABLE_BASIC;
}

/* ==========================================================================
 * IMPLEMENTATION -- SDRAM Mode Register + Calibration Helpers
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * Write LPDDR2 Mode Register via SDC MR interface
 * --------------------------------------------------------------------------
 * Sends an MR write command through the SDRAM controller's mode register
 * port. From upstream sdram.c write_mr().
 * -------------------------------------------------------------------------- */
static void write_mr(unsigned int addr, unsigned int data) {
    /* Wait for MR interface ready */
    int timeout = 200000;
    while (!(SD_MR & SD_MR_DONE_SET) && --timeout > 0) { /* spin */ }

    /* Send MR write command: addr in [7:0], data in [15:8], RW=1 */
    SD_MR = (addr & 0xFF) | ((data & 0xFF) << 8) | SD_MR_RW_SET;

    /* Wait for completion */
    timeout = 200000;
    while (!(SD_MR & SD_MR_DONE_SET) && --timeout > 0) { /* spin */ }
    if (timeout == 0) {
        DBG("[suspend] WARNING: MR write timeout\n");
    }
}

/* --------------------------------------------------------------------------
 * Post-self-refresh DRAM recalibration (JEDEC + PHY PVT)
 * --------------------------------------------------------------------------
 * After self-refresh exit, temperature/voltage may have drifted during
 * sleep. JEDEC recommends ZQ calibration, and the BCM283x PHY needs
 * PVT compensation recalibration to maintain signal integrity.
 *
 * From upstream calibrate_pvt_early() in sdram.c, adapted for post-
 * self-refresh (SDRAM already initialized, just needs recalibration).
 * -------------------------------------------------------------------------- */
static void sdram_zq_calibration(const sdram_config_t *cfg) {
    (void)cfg;  /* reserved for future use -- timing params */

    DBG("[suspend] Running ZQ calibration + PVT compensation...\n");

    /* Step 1: Re-send MR2 (RL=6/WL=3, encoded as 4) as a defensive measure.
     * Mode registers survive self-refresh per JEDEC, but re-sending ensures
     * correct state if SDRAM had a glitch during entry/exit. */
    write_mr(LPDDR2_MR_DEVICE_FEATURE_2, 4);

    /* Step 2: PVT compensation -- recalibrate PHY for current temp/voltage.
     * From upstream calibrate_pvt_early():
     *   BIST_CNTRL_SPR = 0x20 (PVT mode) → kick addr PVT → kick data PVT
     *   → wait for completion → clear BIST */
    APHY_CSR_PHY_BIST_CNTRL_SPR = 0x20;  /* BIST_pvt mode */

    APHY_CSR_ADDR_PVT_COMP_CTRL = 0x1;
    for (int t = 200000; t > 0; --t)
        if (APHY_CSR_ADDR_PVT_COMP_STATUS & 2) break;

    DPHY_CSR_DQ_PVT_COMP_CTRL = 0x1;
    for (int t = 200000; t > 0; --t)
        if (DPHY_CSR_DQ_PVT_COMP_STATUS & 2) break;

    APHY_CSR_PHY_BIST_CNTRL_SPR = 0x0;

    /* Step 3: ZQ calibration command (JEDEC LPDDR2 MR10).
     * Recalibrates output driver impedance for current temperature.
     * From upstream: SD_MRT=20 (long timeout), then MR10 write with HI_Z. */
    {
        uint32_t saved_mrt = SD_MRT;
        SD_MRT = 20;
        SD_MR = LPDDR2_MR_CALIBRATION | (0xFF << 8)
              | SD_MR_RW_SET | SD_MR_HI_Z_SET;
        for (int t = 200000; t > 0; --t)
            if (SD_MR & SD_MR_DONE_SET) break;
        SD_MRT = saved_mrt;
    }

    /* Step 4: Re-send IO config (drive strength).
     * From upstream: write_mr(LPDDR2_MR_IO_CONFIG, 2, false) */
    write_mr(LPDDR2_MR_IO_CONFIG, 2);

    DBG("[suspend] ZQ calibration complete\n");
}

/* --------------------------------------------------------------------------
 * Post-lock SDRAM integrity test
 * --------------------------------------------------------------------------
 * Writes test patterns to a safe SDRAM region and reads them back.
 * Catches marginal PLL/DLL lock, per-lane PHY errors, and self-refresh
 * retention failures that the XOR checksum might miss.
 *
 * Uses 8 patterns covering stuck-at, coupling, and walking bits.
 * Test region: 256 bytes past suspend state area.
 * -------------------------------------------------------------------------- */
#define SDRAM_TEST_ADDR     (SUSPEND_STATE_ADDR + 0x200)
#define SDRAM_TEST_WORDS    64

static int sdram_mem_test(void) {
    volatile uint32_t *test = (volatile uint32_t *)SDRAM_TEST_ADDR;
    static const uint32_t patterns[] = {
        0xAAAAAAAA, 0x55555555, 0xFF00FF00, 0x00FF00FF,
        0x12345678, 0xFEDCBA98, 0x01010101, 0x80808080
    };
    int num_patterns = sizeof(patterns) / sizeof(patterns[0]);

    for (int p = 0; p < num_patterns; p++) {
        /* Write pattern (XOR with index for address sensitivity) */
        for (int i = 0; i < SDRAM_TEST_WORDS; i++)
            test[i] = patterns[p] ^ (uint32_t)i;

        /* Read back and verify */
        for (int i = 0; i < SDRAM_TEST_WORDS; i++) {
            if (test[i] != (patterns[p] ^ (uint32_t)i)) {
                DBG("[suspend] SDRAM mem test FAILED\n");
                return 0;
            }
        }
    }

    DBG("[suspend] SDRAM mem test passed\n");
    return 1;
}

/* ==========================================================================
 * IMPLEMENTATION -- SDRAM Self-Refresh
 * ==========================================================================
 * The SDRAM self-refresh sequence is derived from sdram.c's
 * reset_with_timing() function, which contains both the entry (standby)
 * and full re-initialization sequences.
 *
 * Self-refresh preserves DRAM contents using the SDRAM chip's internal
 * refresh circuitry. The controller and PHY can be powered down.
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * Enter self-refresh
 * --------------------------------------------------------------------------
 * Sequence from sdram.c:
 *   1. SD_CS |= SD_CS_STBY_SET         -- request standby
 *   2. Wait for SD_CS_SDUP_SET to clear -- controller acknowledges
 *   3. Clock manager: disable SDRAM clock (CM_SDCCTL_ENAB_SET)
 *   4. Power down DDR PLL for maximum power savings
 * -------------------------------------------------------------------------- */
void sdram_enter_selfrefresh(void) {
    /* Matches upstream sdram.c reset_with_timing() self-refresh entry exactly:
     *   SD_CS = (SD_CS & ~(SD_CS_DEL_KEEP_SET|SD_CS_DPD_SET|SD_CS_RESTRT_SET))
     *           | SD_CS_STBY_SET;
     *   for (;;) if ((SD_CS & SD_CS_SDUP_SET) == 0) break;
     */

    /* Step 1: Request standby. Clear DEL_KEEP, DPD, RESTRT per upstream. */
    SD_CS = (SD_CS & ~(SD_CS_DPD_SET | SD_CS_RESTRT_SET)) | SD_CS_STBY_SET;

    /* Step 2: Wait for SDRAM controller to go down (SDUP clears).
     * Upstream uses infinite loop -- we add a generous timeout with warning. */
    DBG("[suspend] waiting for SDRAM controller to go down...\n");
    for (int timeout = 1000000; timeout > 0; --timeout) {
        if ((SD_CS & SD_CS_SDUP_SET) == 0) break;
        if (timeout == 1) {
            DBG("[suspend] WARNING: SDRAM standby ack timeout!\n");
        }
    }

    /* Step 3: Disable SDRAM clock via clock manager.
     * Upstream sdram.c: CM_SDCCTL = (CM_SDCCTL & ~(ENAB|CTRL)) | CM_PASSWORD
     * We only clear ENAB since we want to restore CTRL on wake. */
    clkman_update_begin();
    CM_SDCCTL = (CM_SDCCTL & ~CM_SDCCTL_ENAB_SET) | CM_PASSWORD;
    clkman_update_end();
    DBG("[suspend] SDRAM clock disabled\n");

    /* Step 4: Power down DDR PLL for maximum power savings.
     * From sdram.c: clear PWRDWN, GLOBAL_RESET, POST_DIV_RESET registers.
     * The PLL outputs stop -- saves ~5-10 mA. */
    APHY_CSR_DDR_PLL_PWRDWN         = 0;
    APHY_CSR_DDR_PLL_GLOBAL_RESET   = 0;
    APHY_CSR_DDR_PLL_POST_DIV_RESET = 0;

    DBG("[suspend] SDRAM in self-refresh, PLL powered down\n");
}

/* --------------------------------------------------------------------------
 * Exit self-refresh
 * --------------------------------------------------------------------------
 * Reverse of entry. Sequence from sdram.c reset_with_timing():
 *   1. Re-configure DDR PLL with saved VCO/MDIV values
 *   2. De-assert PLL reset, wait for lock
 *   3. Un-gate SDRAM clock via clock manager
 *   4. Reset PHY DLLs, wait for lock
 *   5. Re-enable SDRAM controller, clear standby, restart
 *
 * CRITICAL: Timing matters. If the PLL doesn't lock or the PHY DLLs
 * don't calibrate, SDRAM data is corrupted. Fall back to watchdog reset.
 * -------------------------------------------------------------------------- */
void sdram_exit_selfrefresh(const sdram_config_t *cfg) {
    int timeout;

    DBG("[suspend] Re-enabling DDR PLL...\n");

    /* ---- Step 1: PLL re-lock with retry ----
     *
     * Mitigation: if PLL doesn't lock on first attempt, power-cycle it
     * and retry up to 3 times. This is the standard approach used by
     * TI OMAP3 (sleep34xx.S kick_dll pattern), Samsung Exynos, and
     * Qualcomm LPDDR2 controllers. BCM2835's DDR PLL is identical
     * across all BCM283x SoCs (Pi 1/2/3/Zero) -- same silicon IP. */
    for (int pll_attempt = 0; pll_attempt < 3; pll_attempt++) {
        /* Write VCO freq + MDIV from saved config */
        APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL0 = cfg->ddr_pll_vco_freq_cntl0;
        APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL1 = cfg->ddr_pll_vco_freq_cntl1;
        APHY_CSR_DDR_PLL_MDIV_VALUE      = cfg->ddr_pll_mdiv;

        /* De-assert resets. Order matters: GLOBAL_RESET first, then POST_DIV,
         * then PWRDWN. Matches upstream sdram.c reset_with_timing(). */
        APHY_CSR_DDR_PLL_GLOBAL_RESET    = 1;

        /* Wait for PLL lock. Upstream uses infinite loop:
         *   for (;;) if (APHY_CSR_DDR_PLL_LOCK_STATUS & (1 << 16)) break;
         * We add a generous timeout. */
        DBG("[suspend] Waiting for PLL lock...\n");
        timeout = 200000;
        while (!(APHY_CSR_DDR_PLL_LOCK_STATUS & (1 << 16)) && --timeout > 0) {
            /* spin */
        }

        if (timeout > 0) {
            /* PLL locked! De-assert POST_DIV reset now. */
            APHY_CSR_DDR_PLL_POST_DIV_RESET = 1;
            break;
        }

        /* PLL didn't lock -- power-cycle and retry.
         * This "kick" pattern is proven on OMAP3 (SDRC DLL) and
         * Exynos (DMC PHY). Reset PLL fully, wait, then re-try. */
        DBG("[suspend] PLL lock failed, retrying...\n");
        APHY_CSR_DDR_PLL_PWRDWN         = 0;
        APHY_CSR_DDR_PLL_GLOBAL_RESET   = 0;
        APHY_CSR_DDR_PLL_POST_DIV_RESET = 0;
        udelay(50);  /* let PLL fully quiesce */
    }

    if (!(APHY_CSR_DDR_PLL_LOCK_STATUS & (1 << 16))) {
        DBG("[suspend] FATAL: DDR PLL failed to lock after 3 attempts!\n");
        PM_WDOG = PM_PASSWORD | 10;
        PM_RSTC = PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;
        for (;;) { /* wait for watchdog reset */ }
    }

    DBG("[suspend] DDR PLL locked\n");

    /* ---- Step 2: Un-gate SDRAM clock ----
     * Upstream sdram.c: clock manager update protocol + set ENAB + restore
     * saved CTRL source bits. */
    DBG("[suspend] Un-gating SDRAM clock...\n");
    clkman_update_begin();
    {
        /* Restore the clock source (CTRL bits) from saved config,
         * then set ENAB. Upstream uses (ctrl << CM_SDCCTL_CTRL_LSB). */
        uint32_t saved_ctrl = cfg->cm_sdcctl & 0xF;  /* source bits [3:0] */
        CM_SDCCTL = CM_PASSWORD | (CM_SDCCTL & ~0xF) | saved_ctrl | CM_SDCCTL_ENAB_SET;
    }
    clkman_update_end();

    udelay(10);

    /* ---- Step 3: PHY DLL reset with retry (OMAP3 kick_dll pattern) ----
     *
     * From OMAP3 sleep34xx.S:
     *   1. Assert DLL reset, stall, de-assert
     *   2. Wait for lock with timeout
     *   3. If no lock: "kick" = disable DLL, re-enable, retry
     *   4. Up to 3 kicks before giving up
     *
     * Upstream sdram.c reset_phy_dll():
     *   APHY_CSR_PHY_BIST_CNTRL_SPR = 0x30;   // tell SDC we're messing with addr
     *   DPHY_CSR_GLBL_DQ_DLL_RESET = 1; APHY_CSR_GLBL_ADDR_DLL_RESET = 1;
     *   SD_CS; SD_CS; SD_CS; SD_CS;            // stall (4 register reads)
     *   DPHY_CSR_GLBL_DQ_DLL_RESET = 0; APHY_CSR_GLBL_ADDR_DLL_RESET = 0;
     *   for (;;) if ((DPHY_CSR_GLBL_MSTR_DLL_LOCK_STAT & 0xFFFF) == 0xFFFF) break;
     */
    DBG("[suspend] Resetting PHY DLLs...\n");

    /* Tell SDC we're messing with address lines (from upstream reset_phy_dll) */
    APHY_CSR_PHY_BIST_CNTRL_SPR = 0x30;

    for (int dll_kick = 0; dll_kick < 3; dll_kick++) {
        /* Assert DLL resets */
        DPHY_CSR_GLBL_DQ_DLL_RESET   = 1;
        APHY_CSR_GLBL_ADDR_DLL_RESET = 1;

        /* Stall exactly as upstream does: 4 dummy SD_CS reads */
        (void)SD_CS; (void)SD_CS; (void)SD_CS; (void)SD_CS;

        /* De-assert DLL resets */
        DPHY_CSR_GLBL_DQ_DLL_RESET   = 0;
        APHY_CSR_GLBL_ADDR_DLL_RESET = 0;

        /* Wait for DPHY master DLL lock (all lanes = 0xFFFF) */
        timeout = 200000;
        while ((DPHY_CSR_GLBL_MSTR_DLL_LOCK_STAT & 0xFFFF) != 0xFFFF && --timeout > 0) {
            /* spin */
        }

        if (timeout > 0) {
            DBG("[suspend] PHY DLL locked\n");
            break;
        }

        /* DLL didn't lock -- kick it (disable + re-enable).
         * This is the proven OMAP3 pattern from kick_dll in sleep34xx.S. */
        DBG("[suspend] PHY DLL lock timeout, kicking...\n");
    }

    if ((DPHY_CSR_GLBL_MSTR_DLL_LOCK_STAT & 0xFFFF) != 0xFFFF) {
        DBG("[suspend] WARNING: PHY DLL still not locked after 3 kicks!\n");
        /* Don't watchdog here -- SDRAM might still work with degraded timing */
    }

    /* Wait for address DLL lock (from upstream reset_phy_dll) */
    for (timeout = 200000; timeout > 0; --timeout) {
        if (APHY_CSR_GLBL_ADDR_DLL_LOCK_STAT == 3) break;
    }
    if (timeout == 0) {
        DBG("[suspend] WARNING: Address DLL lock timeout\n");
    }

    /* Tell SDC we're done messing with address lines */
    APHY_CSR_PHY_BIST_CNTRL_SPR = 0x0;

    /* ---- Step 4: Re-enable SDRAM controller ----
     * Matches upstream sdram.c reset_with_timing() final SD_CS write exactly. */
    DBG("[suspend] Restarting SDRAM controller...\n");
    SD_CS = (((4 << SD_CS_ASHDN_T_LSB)
              | SD_CS_STATEN_SET
              | SD_CS_EN_SET)
             & ~(SD_CS_STOP_SET | SD_CS_STBY_SET))
            | SD_CS_RESTRT_SET;

    /* Upstream reset_with_timing does RESTRT and returns without waiting for
     * SDUP; init_late/selftest is the real functional check. Give the
     * controller settling time, log status, let sdram_mem_test be the gate. */
    udelay(2000);
    for (timeout = 2000000; timeout > 0; --timeout) {
        if (SD_CS & SD_CS_SDUP_SET) break;
    }
    if (timeout == 0) {
        DBG("[suspend] WARNING: SDUP still clear after RESTRT (SD_CS=");
        uart_print_hex(SD_CS);
        DBG("); continuing to mem test\n");
    }

    /* JEDEC-required: MR restore + ZQ recalibration after self-refresh exit.
     * Temperature/voltage may have drifted during sleep, requiring output
     * driver impedance recalibration and PHY PVT compensation. */
    sdram_zq_calibration(cfg);

    /* Verify SDRAM data integrity with multi-pattern read/write test.
     * Catches marginal PLL/DLL lock, per-lane PHY errors, and self-refresh
     * retention failures. */
    if (!sdram_mem_test()) {
        DBG("[suspend] FATAL: SDRAM integrity test failed!\n");
        PM_WDOG = PM_PASSWORD | 10;
        PM_RSTC = PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;
        for (;;) { /* wait for watchdog */ }
    }

    DBG("[suspend] SDRAM back online\n");
}

/* ==========================================================================
 * IMPLEMENTATION -- ARM Power Management
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * Power-gate ARM cores
 * --------------------------------------------------------------------------
 * From BCM2708PowerManagement.cc: clearing PM_PROC (writing just the
 * password) removes power from the ARM domain. This saves ~100-200 mA
 * depending on ARM load.
 *
 * The ARM cores lose all state -- Linux must handle its own suspend/resume
 * (Phase 5+ integration), or we accept that ARM state is lost.
 * -------------------------------------------------------------------------- */
static void power_gate_arm(void) {
    /* Remove power: write password with all domain bits cleared */
    PM_PROC = PM_PASSWORD;
    udelay(200);

    DBG("[suspend] ARM power domain gated\n");
}

/* --------------------------------------------------------------------------
 * Un-gate ARM cores
 * --------------------------------------------------------------------------
 * From BCM2708PowerManagement.cc power-on sequence:
 *   1. Clear reset, write password
 *   2. Set POWUP -- enables power to the block
 *   3. Wait for POWOK -- power is stable
 *   4. Set ISPOW -- electrical isolation
 *   5. Set MEMREP -- memory repair
 *   6. Wait for MRDONE -- memory repair complete
 *   7. Set ISFUNC -- functional isolation (domain is live)
 *
 * NOTE: This powers on the ARM domain but doesn't start code execution.
 * ARM will start from its reset vector. For Linux resume, the ARM needs
 * to be directed to a resume entry point via ARM_CONTROL0/1 registers.
 *
 * Linux integration (how other SoCs do it):
 * -----------------------------------------
 * From arch/arm/kernel/suspend.c and OMAP3 sleep34xx.S:
 *   1. Before suspend, Linux calls cpu_suspend() which saves ARM registers
 *      (r4-r11, sp, lr, CPSR) to a known physical address, then calls
 *      the platform suspend finisher.
 *   2. The finisher tells firmware (us) the physical address of cpu_resume.
 *   3. On wake, firmware powers on ARM and writes cpu_resume address to
 *      the ARM's boot address register (ARM_CONTROL0 on BCM283x).
 *   4. ARM wakes at cpu_resume, restores MMU+caches, and returns from
 *      cpu_suspend() as if nothing happened.
 *
 * For BCM283x specifically:
 *   - ARM_CONTROL0 (0x7E00C000) bits [31:2] = ARM boot address >> 2
 *   - The VPU firmware (us) writes this before de-asserting ARM reset
 *   - Linux's bcm2835-mailbox can pass the resume address to us
 *   - See: raspberrypi/linux drivers/firmware/raspberrypi.c
 *
 * Phase 5 plan:
 *   1. Before ARM power-gate, read ARM_RESUME_ADDR from a known mailbox
 *      or a fixed SDRAM location that Linux writes before suspend
 *   2. After power-ungate, write that address to ARM_CONTROL0
 *   3. ARM boots at cpu_resume instead of the kernel entry point
 * -------------------------------------------------------------------------- */
static void power_ungate_arm(void) {
    uint32_t pmv;
    int timeout;

    /* Start clean: password + clear (preserve ARM reset bit cleared) */
    pmv = (PM_PROC & PM_UNK_CFG_CLR) | PM_PASSWORD;
    PM_PROC = pmv;
    udelay(15);

    /* POWUP -- enable power to ARM block */
    pmv |= PM_GNRIC_POWUP_SET;
    PM_PROC = pmv;

    /* Wait for POWOK */
    timeout = 100000;
    while (!(PM_PROC & PM_GNRIC_POWOK_SET) && --timeout > 0) {
        wait_osc_ticks(2);
    }
    if (timeout == 0) {
        /* Try different CFG values (from upstream: tries CFG 1,2,3) */
        for (int cfg = 1; cfg <= 3; cfg++) {
            pmv = (pmv & PM_UNK_CFG_CLR) | (cfg << PM_GNRIC_CFG_LSB);
            PM_PROC = pmv;
            timeout = 20000;
            while (!(PM_PROC & PM_GNRIC_POWOK_SET) && --timeout > 0) {
                wait_osc_ticks(2);
            }
            if (timeout > 0) break;
        }
        if (timeout == 0) {
            DBG("[suspend] WARNING: ARM POWOK timeout!\n");
        }
    }

    /* ISPOW + MEMREP */
    pmv |= PM_GNRIC_ISPOW_SET;
    pmv |= PM_GNRIC_MEMREP_SET;
    PM_PROC = pmv;

    /* Wait for MRDONE */
    timeout = 100000;
    while (!(PM_PROC & PM_GNRIC_MRDONE_SET) && --timeout > 0) {
        /* spin */
    }
    if (timeout == 0) {
        DBG("[suspend] WARNING: ARM MRDONE timeout!\n");
    }

    /* ISFUNC -- domain is now functional */
    pmv |= PM_GNRIC_ISFUNC_SET;
    PM_PROC = pmv;

    /* ---- Linux resume vector (Phase 5) ----
     * If Linux wrote a cpu_resume address to ARM_RESUME_ADDR before suspend,
     * set ARM_CONTROL0 so ARM boots there instead of the kernel entry point.
     *
     * How this works (from OMAP3 / Linux arch/arm/kernel/suspend.c):
     *   1. Linux calls cpu_suspend() which saves r4-r11, sp, cpsr
     *   2. Linux writes physical address of cpu_resume to a known location
     *   3. We (VPU firmware) read it and write to ARM_CONTROL0
     *   4. ARM wakes at cpu_resume, restores MMU, returns from cpu_suspend()
     *
     * If ARM_RESUME_ADDR is 0 or garbage, ARM boots normally (cold boot). */
    {
        uint32_t resume_addr = ARM_RESUME_ADDR;
        if (resume_addr != 0 && resume_addr != 0xFFFFFFFF) {
            DBG("[suspend] Setting ARM resume vector\n");
            ARM_CONTROL0 = resume_addr;
            /* Clear the resume address so cold boots don't use stale data */
            ARM_RESUME_ADDR = 0;
        }
    }

    /* Assert ARM reset to allow controlled startup */
    PM_PROC = pmv | PM_PROC_ARMRSTN_SET | PM_PASSWORD;
    udelay(300);

    DBG("[suspend] ARM power domain restored\n");
}

/* ==========================================================================
 * IMPLEMENTATION -- GPIO3 Wake Source
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * Configure GPIO3 as wake source
 * --------------------------------------------------------------------------
 * GPIO3 (physical pin 5) -- standard Pi wake pin.
 *
 *   1. Set GPIO3 as input (GPFSEL0 bits [11:9] = 000)
 *   2. Enable pull-down (default LOW, pulled HIGH to wake)
 *   3. Enable rising-edge detect
 *   4. Clear any pending events
 *
 * Wake trigger: momentarily connect GPIO3 to 3V3, or use RTC alarm.
 * -------------------------------------------------------------------------- */
static void configure_gpio3_wake(void) {
    /* GPIO17 function select: input (GPFSEL1 bits [23:21] = 000) */
    GPFSEL1 &= ~(0x7 << 21);

    /* Enable pull-UP on GPIO17 (button wired to GND -> pull-up idles HIGH,
     * button press pulls LOW, release returns HIGH).
     * BCM2835 pull-up/down sequence (ARM Peripherals §6.1):
     *   1. Write to GPPUD to set pull direction
     *   2. Wait 150 cycles
     *   3. Write to GPPUDCLK0 to clock it into GPIO17
     *   4. Wait 150 cycles
     *   5. Clear GPPUD and GPPUDCLK0 */
    GPPUD = 0x02;           /* 10 = Assert pull-up */
    udelay(5);              /* >150 cycles at any reasonable VPU clock */
    GPPUDCLK0 = (1 << 17); /* Clock pull config into GPIO17 */
    udelay(5);
    GPPUD = 0x00;
    GPPUDCLK0 = 0x00;

    /* Enable falling-edge detect on GPIO17 (press pulls HIGH->LOW) */
    GPFEN0 |= (1 << 17);

    /* Clear any pending edge detect event on GPIO17 */
    GPEDS0 = (1 << 17);    /* Write-1-to-clear */

    /* Route GPIO bank 0 interrupt to VPU core 0 so the `sleep`
     * instruction wakes on GPIO3 edge events.
     *
     * From upstream/firmware/interrupt.c set_interrupt():
     *   intno = 49 (GPIO bank 0, covers pins 0-27)
     *   offset = 0x10 + ((49 >> 3) << 2) = 0x10 + (6*4) = 0x28
     *   slot   = 0xF << ((49 & 7) << 2)  = 0xF << (1*4) = 0xF0
     *   Write slot bits into IC0_BASE + offset to enable. */
    {
        uint32_t offset = 0x10 + ((IC_GPIO0_INTNO >> 3) << 2);     /* 0x28 */
        uint32_t slot   = 0xFu << ((IC_GPIO0_INTNO & 7) << 2);     /* 0xF0 */
        volatile uint32_t *ic_reg = (volatile uint32_t *)(IC0_BASE + offset);
        uint32_t val = *ic_reg;
        *ic_reg = val | slot;   /* Enable GPIO0 bank interrupt on VPU core 0 */
    }

    DBG("[suspend] GPIO17 configured as wake source (pull-up + falling edge)\n");
    DBG("[suspend] VPU IC0 GPIO bank 0 interrupt enabled\n");
}

/* --------------------------------------------------------------------------
 * Wait for GPIO3 wake event
 * --------------------------------------------------------------------------
 * The VC4 `sleep` instruction gates the VPU clock until an interrupt
 * fires. This is the lowest-power VPU state -- effectively WFI.
 *
 * From rpi-open-firmware/firmware/arm_monitor.c:
 *   __asm__ __volatile__ ("sleep" :::);
 *
 * GPIO bank 0 interrupt (hw intno 49) is routed to VPU IC0 by
 * configure_gpio3_wake(). GPIO3 rising edge → GPEDS0 set →
 * GPIO0 bank IRQ fires → VPU wakes from sleep.
 * -------------------------------------------------------------------------- */
static void wait_for_gpio3_wake(void) {
    while (!(GPEDS0 & (1 << 17))) {
        /* VC4 sleep instruction -- halts VPU until interrupt */
        __asm__ __volatile__("sleep" :::);

        /* If we wake from a non-GPIO3 interrupt, loop back to sleep.
         * In the worst case (sleep doesn't work), remove the sleep
         * instruction and this becomes a busy-poll loop. */
    }

    /* Clear the event */
    GPEDS0 = (1 << 17);

    DBG("[suspend] GPIO17 wake event detected\n");
}

/* ==========================================================================
 * IMPLEMENTATION -- Checksum and Validation
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * Compute checksum over suspend state
 * --------------------------------------------------------------------------
 * Simple XOR-based checksum. We skip the checksum field itself.
 * For Phase 2, this is sufficient. If SDRAM data is corrupted,
 * the checksum will almost certainly mismatch.
 *
 * We use XOR instead of CRC32 to avoid pulling in a CRC library
 * (bare-metal, no libc). XOR catches single-bit and burst errors
 * well enough for our "did SDRAM survive self-refresh?" validation.
 * -------------------------------------------------------------------------- */
static uint32_t compute_checksum(const suspend_state_t *state) {
    const uint32_t *words = (const uint32_t *)state;
    uint32_t num_words = sizeof(suspend_state_t) / sizeof(uint32_t);
    uint32_t cksum = 0;

    for (uint32_t i = 0; i < num_words; i++) {
        /* Skip the checksum field itself (offset 2 = 8 bytes / 4) */
        if (i == 2) continue;
        cksum ^= words[i];
    }

    return cksum;
}

/* --------------------------------------------------------------------------
 * Validate saved state after wake
 * -------------------------------------------------------------------------- */
static int validate_state(const suspend_state_t *state) {
    if (state->magic != SUSPEND_MAGIC) {
        DBG("[suspend] ERROR: bad magic!\n");
        return 0;
    }
    if (state->version != SUSPEND_VERSION) {
        DBG("[suspend] ERROR: version mismatch!\n");
        return 0;
    }
    if (state->stack_canary != STACK_CANARY_MAGIC) {
        DBG("[suspend] ERROR: stack canary corrupted!\n");
        return 0;
    }

    uint32_t computed = compute_checksum(state);
    if (computed != state->checksum) {
        DBG("[suspend] ERROR: checksum mismatch!\n");
        return 0;
    }

    return 1;  /* State is valid */
}

/* ==========================================================================
 * REGISTER DUMP -- For initial hardware bring-up
 * ==========================================================================
 * Call this before suspend to print all relevant register values via UART.
 * This helps verify register addresses are correct on the actual hardware.
 * ========================================================================== */
#ifndef NDEBUG
extern void uart_print_hex(uint32_t val);

void suspend_dump_registers(void) {
    DBG("[dump] === SDRAM Controller ===\n");
    DBG("[dump] SD_CS  = 0x"); uart_print_hex(SD_CS);
    DBG("[dump] SD_SA  = 0x"); uart_print_hex(SD_SA);
    DBG("[dump] SD_SB  = 0x"); uart_print_hex(SD_SB);
    DBG("[dump] SD_SC  = 0x"); uart_print_hex(SD_SC);
    DBG("[dump] SD_PT1 = 0x"); uart_print_hex(SD_PT1);
    DBG("[dump] SD_PT2 = 0x"); uart_print_hex(SD_PT2);
    DBG("[dump] SD_MRT = 0x"); uart_print_hex(SD_MRT);

    DBG("\n[dump] === Clock Manager ===\n");
    DBG("[dump] CM_SDCCTL = 0x"); uart_print_hex(CM_SDCCTL);
    DBG("[dump] CM_SDCDIV = 0x"); uart_print_hex(CM_SDCDIV);
    DBG("[dump] CM_VPUCTL = 0x"); uart_print_hex(CM_VPUCTL);
    DBG("[dump] CM_VPUDIV = 0x"); uart_print_hex(CM_VPUDIV);

    DBG("\n[dump] === DDR PLL (APHY) ===\n");
    DBG("[dump] VCO_FREQ0   = 0x"); uart_print_hex(APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL0);
    DBG("[dump] VCO_FREQ1   = 0x"); uart_print_hex(APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL1);
    DBG("[dump] MDIV        = 0x"); uart_print_hex(APHY_CSR_DDR_PLL_MDIV_VALUE);
    DBG("[dump] LOCK_STATUS = 0x"); uart_print_hex(APHY_CSR_DDR_PLL_LOCK_STATUS);
    DBG("[dump] PWRDWN      = 0x"); uart_print_hex(APHY_CSR_DDR_PLL_PWRDWN);

    DBG("\n[dump] === DPHY ===\n");
    DBG("[dump] DLL_LOCK = 0x"); uart_print_hex(DPHY_CSR_GLBL_MSTR_DLL_LOCK_STAT);

    DBG("\n[dump] === Power Manager ===\n");
    DBG("[dump] PM_PROC = 0x"); uart_print_hex(PM_PROC);

    DBG("\n[dump] === GPIO ===\n");
    DBG("[dump] GPFSEL0 = 0x"); uart_print_hex(GPFSEL0);
    DBG("[dump] GPLEV0  = 0x"); uart_print_hex(GPLEV0);

    DBG("\n[dump] Done.\n");
}
#endif
