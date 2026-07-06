/* ==========================================================================
 * state.h -- VPU suspend/resume state structures
 * ==========================================================================
 * Defines the data structures used to save and restore VPU state across
 * a suspend-to-RAM cycle.
 *
 * The VPU has:
 *   - 32 scalar registers (r0-r31), 32 bits each
 *   - 64 vector registers (V0-V63), each 16 x 32-bit = 64 bytes
 *   - Status registers (sr, cpuid)
 *   - Interrupt controller state
 *   - PLL / clock configuration
 *   - GPIO pin state
 *
 * Total state to save is ~5KB -- easily fits in SDRAM during self-refresh.
 *
 * Reference: Broadcom Patent US20110264902A1 (expired 2017)
 *            rpi-open-firmware/firmware/sdram.c
 * ========================================================================== */

#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include <stddef.h>   /* for offsetof() compile-time offset checks */

/* --------------------------------------------------------------------------
 * Magic number to validate saved state after wake
 * -------------------------------------------------------------------------- */
#define SUSPEND_MAGIC       0x53555350  /* "SUSP" */
#define SUSPEND_VERSION     1
#define STACK_CANARY_MAGIC  0xDEADBEEF

/* --------------------------------------------------------------------------
 * SDRAM self-refresh state
 * --------------------------------------------------------------------------
 * Captures the minimum config needed to exit self-refresh without doing
 * a full sdram.c re-init. If full re-init is needed (Phase 2 fallback),
 * these values help verify SDRAM came back correctly.
 * -------------------------------------------------------------------------- */
typedef struct {
    /* APHY (analog PHY) PLL settings -- from APHY_CSR_DDR_PLL_* registers */
    uint32_t ddr_pll_vco_freq_cntl0;
    uint32_t ddr_pll_vco_freq_cntl1;
    uint32_t ddr_pll_mdiv;
    uint32_t ddr_pll_global_reset;

    /* DPHY (digital PHY) settings -- from DPHY_CSR_* registers */
    uint32_t dphy_glbl_dq_dly_trim;
    uint32_t dphy_wrdq0_trim;
    uint32_t dphy_wrdq1_trim;

    /* SDRAM controller -- from SD_* registers */
    uint32_t sd_sa;     /* address config */
    uint32_t sd_sb;
    uint32_t sd_sc;
    uint32_t sd_pt1;    /* timing */
    uint32_t sd_pt2;
    uint32_t sd_mrt;
    uint32_t sd_cs;     /* control/status -- includes SD_CS_STBY_SET */

    /* CM (clock manager) SDRAM clock */
    uint32_t cm_sdcctl;
    uint32_t cm_sdcdiv;
} sdram_config_t;

/* --------------------------------------------------------------------------
 * VPU scalar register state
 * -------------------------------------------------------------------------- */
typedef struct {
    uint32_t r[32];     /* r0-r31 (r0 is always 0, saved for consistency) */
    uint32_t sr;        /* status register */
    uint32_t pc;        /* program counter at suspend entry */
    uint32_t sp;        /* stack pointer */
    uint32_t lr;        /* link register */
} vpu_scalar_regs_t;

/* --------------------------------------------------------------------------
 * VPU vector register state (optional -- Phase 3+)
 * --------------------------------------------------------------------------
 * Each vector register is 16 elements x 32 bits = 64 bytes.
 * 64 vector registers = 4096 bytes total.
 * For Phase 2, we skip vector regs to minimize complexity.
 * -------------------------------------------------------------------------- */
typedef struct {
    uint32_t v[64][16]; /* V0-V63, 16 x 32-bit elements each */
} vpu_vector_regs_t;

/* --------------------------------------------------------------------------
 * PLL / clock state
 * --------------------------------------------------------------------------
 * All the PLL configurations needed to restore clocks without a cold boot.
 * See pll_read.c in upstream for the register list.
 * -------------------------------------------------------------------------- */
typedef struct {
    /* ARM PLL */
    uint32_t cm_armctl;
    uint32_t cm_armdiv;

    /* Core PLL */
    uint32_t cm_vpuctl;
    uint32_t cm_vpudiv;

    /* HDMI PLL (probably don't care, but save for completeness) */
    uint32_t cm_hdmictl;
    uint32_t cm_hdmidiv;

    /* Peripheral PLLs */
    uint32_t cm_timerctl;
    uint32_t cm_timerdiv;
    uint32_t cm_uartctl;
    uint32_t cm_uartdiv;
} pll_config_t;

/* --------------------------------------------------------------------------
 * GPIO state (for wake pin configuration)
 * -------------------------------------------------------------------------- */
typedef struct {
    uint32_t gpfsel[6];     /* GPIO function select (pins 0-53) */
    uint32_t gpren[2];      /* Rising edge detect enable */
    uint32_t gpfen[2];      /* Falling edge detect enable */
    uint32_t gphen[2];      /* High detect enable */
    uint32_t gplen[2];      /* Low detect enable */
    uint32_t gppud;         /* Pull-up/down enable */
    uint32_t gppudclk[2];   /* Pull-up/down clock */
} gpio_state_t;

/* --------------------------------------------------------------------------
 * Interrupt controller state
 * -------------------------------------------------------------------------- */
typedef struct {
    uint32_t irq_enable1;
    uint32_t irq_enable2;
    uint32_t irq_enable_basic;
} ic_state_t;

/* --------------------------------------------------------------------------
 * Master suspend state -- saved to a known SDRAM address
 * --------------------------------------------------------------------------
 * Located at SUSPEND_STATE_ADDR during self-refresh.
 * On wake, the resume stub reads this to validate and restore.
 * -------------------------------------------------------------------------- */

/* Fixed address in SDRAM to store suspend state.
 * Choose an address that won't conflict with VPU firmware or ARM Linux.
 * 0x1FF00000 = 511MB mark (near top of 512MB) -- safe for Pi Zero 2W. */
#define SUSPEND_STATE_ADDR  0x1FF00000

typedef struct {
    /* Header */
    uint32_t magic;         /* Must be SUSPEND_MAGIC */
    uint32_t version;       /* SUSPEND_VERSION */
    uint32_t checksum;      /* CRC32 of everything after this field */
    uint32_t timestamp;     /* System timer value at suspend */

    /* State sections */
    vpu_scalar_regs_t   scalar_regs;
    sdram_config_t      sdram_config;
    pll_config_t        pll_config;
    gpio_state_t        gpio_state;
    ic_state_t          ic_state;

    /* Resume entry point -- address of _resume_entry in resume.S */
    uint32_t            resume_entry_addr;

    /* Phase 3+: vector regs (4KB, optional) */
    /* vpu_vector_regs_t vector_regs; */

    /* Stack integrity canary -- set to STACK_CANARY_MAGIC on save,
     * verified on resume to detect SDRAM bit-flips in state region */
    uint32_t            stack_canary;

    /* Padding to cache-line boundary */
    uint32_t            _reserved[7];
} suspend_state_t;

/* Compile-time size check */
_Static_assert(sizeof(suspend_state_t) < 4096,
    "suspend_state_t exceeds 4KB -- review what we're saving");

/* --------------------------------------------------------------------------
 * Compile-time offset verification for resume.S hardcoded constants.
 * If you change the struct layout, update resume.S .equ values to match.
 * -------------------------------------------------------------------------- */
_Static_assert(offsetof(suspend_state_t, magic) == 0,
    "resume.S OFF_MAGIC mismatch");
_Static_assert(offsetof(suspend_state_t, version) == 4,
    "resume.S OFF_VERSION mismatch");
_Static_assert(offsetof(suspend_state_t, scalar_regs) == 16,
    "resume.S OFF_SCALAR_R0 mismatch");
_Static_assert(offsetof(suspend_state_t, scalar_regs)
    + offsetof(vpu_scalar_regs_t, sr) == 144,
    "resume.S OFF_SCALAR_SR mismatch");
_Static_assert(offsetof(suspend_state_t, scalar_regs)
    + offsetof(vpu_scalar_regs_t, pc) == 148,
    "resume.S OFF_SCALAR_PC mismatch");
_Static_assert(offsetof(suspend_state_t, scalar_regs)
    + offsetof(vpu_scalar_regs_t, sp) == 152,
    "resume.S OFF_SCALAR_SP mismatch");
_Static_assert(offsetof(suspend_state_t, scalar_regs)
    + offsetof(vpu_scalar_regs_t, lr) == 156,
    "resume.S OFF_SCALAR_LR mismatch");
_Static_assert(offsetof(suspend_state_t, resume_entry_addr) == 344,
    "resume.S OFF_RESUME_ENTRY mismatch");

/* Verify vpu_scalar_regs_t internal offsets match _save_vpu_regs assembly */
_Static_assert(offsetof(vpu_scalar_regs_t, sr) == 128,
    "_save_vpu_regs sr offset mismatch");
_Static_assert(offsetof(vpu_scalar_regs_t, pc) == 132,
    "_save_vpu_regs pc offset mismatch");
_Static_assert(offsetof(vpu_scalar_regs_t, sp) == 136,
    "_save_vpu_regs sp offset mismatch");
_Static_assert(offsetof(vpu_scalar_regs_t, lr) == 140,
    "_save_vpu_regs lr offset mismatch");

#endif /* STATE_H */
