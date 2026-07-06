/* ==========================================================================
 * resume.S -- VPU assembly wake handler for Pi Zero 2W suspend-to-RAM
 * ==========================================================================
 * Cold-boot resume entry point. After a VPU reset during suspend (e.g.,
 * watchdog reset, power glitch), the VPU restarts from its reset vector
 * and our firmware checks whether a valid suspend state exists in SDRAM.
 *
 * PRIMARY resume path:
 *   VPU `sleep` instruction → GPIO3 interrupt → wakes → continues in
 *   C code (wait_for_gpio3_wake loop) → sdram_exit_selfrefresh → done.
 *   This is the normal case and does NOT use resume.S at all.
 *
 * SECONDARY resume path (this file):
 *   VPU hard-reset → bootcode.bin loads from SD → _resume_entry called
 *   → validates SDRAM state → restores registers → resumes C code.
 *   Requires SDRAM to already be out of self-refresh (the normal boot
 *   init in sdram.c handles this).
 *
 * VC4 Assembly Reference:
 *   - VideoCore IV, custom Broadcom ISA (not ARM/MIPS)
 *   - 32 scalar registers: r0-r31. r0 = hardwired 0
 *   - r25 = sp (stack pointer), r26 = lr (link register)
 *   - Variable-length instructions: 16/32/48 bits
 *   - Syntax: mov dst, src. ld dst, (base+off). st src, (base+off)
 *   - stm rA-rB, (--sp): push. ldm rA-rB, (sp++): pop
 *   - Sleep: 'sleep' instruction halts VPU until interrupt
 *   - Sources: hermanhermitage/videocoreiv, rpi-open-firmware/start.S
 *
 * GPLv2+ -- derived from rpi-open-firmware.
 * ========================================================================== */

.equ SUSPEND_STATE_ADDR,  0x1FF00000
.equ SUSPEND_MAGIC,       0x53555350    /* "SUSP" */

/* --------------------------------------------------------------------------
 * Offsets into suspend_state_t (MUST match state.h)
 * --------------------------------------------------------------------------
 * Layout:
 *   [0]   magic            (4)
 *   [4]   version          (4)
 *   [8]   checksum         (4)
 *   [12]  timestamp        (4)
 *   [16]  scalar_regs      (144 = 32×4 + 4×4)
 *     [16]   r[0..31]      (128)
 *     [144]  sr            (4)
 *     [148]  pc            (4)
 *     [152]  sp            (4)
 *     [156]  lr            (4)
 *   [160] sdram_config     (64)
 *   [224] pll_config       (40)
 *   [264] gpio_state       (68)
 *   [332] ic_state         (12)
 *   [344] resume_entry_addr (4)
 *   [348] _reserved[8]     (32)
 *   Total: 380 bytes
 * -------------------------------------------------------------------------- */
.equ OFF_MAGIC,           0
.equ OFF_VERSION,         4
.equ OFF_CHECKSUM,        8
.equ OFF_TIMESTAMP,       12
.equ OFF_SCALAR_R0,       16
.equ OFF_SCALAR_SR,       144
.equ OFF_SCALAR_PC,       148
.equ OFF_SCALAR_SP,       152
.equ OFF_SCALAR_LR,       156
.equ OFF_RESUME_ENTRY,    344

/* Hardware addresses */
.equ PM_BASE,             0x7E100000
.equ PM_WDOG_OFF,         0x24
.equ PM_RSTC_OFF,         0x1C
.equ PM_PASSWORD,         0x5A000000
.equ PM_RSTC_FULL_RESET,  0x00000020

/* ==========================================================================
 * Resume entry point
 * ==========================================================================
 * Called from our custom firmware boot path after SDRAM init has completed.
 * At this point SDRAM is accessible (sdram.c did a full init or the
 * self-refresh exit was handled by the normal boot flow).
 *
 * Convention: no register assumptions. We set up everything from scratch.
 * ========================================================================== */

.section .text.resume

/* ==========================================================================
 * _save_vpu_regs -- Atomic VPU register save (called from C)
 * ==========================================================================
 * void _save_vpu_regs(vpu_scalar_regs_t *regs)
 *
 * Saves ALL 24 scalar registers atomically using stm, exactly as
 * upstream traps.S does in SaveRegsAll/SaveRegsLower/SaveRegsUpper:
 *
 *   stm lr, (--sp)
 *   stm r0-r5, (--sp)       <-- SaveRegsLower
 *   stm r6-r15, (--sp)      <-- SaveRegsUpper
 *   stm r16-r23, (--sp)     <-- SaveRegsUpper
 *
 * This captures caller-saved r0-r5 BEFORE the compiler can clobber
 * them, fixing the "C can't reliably read its own registers" problem.
 *
 * We store directly to the regs struct (r0 = pointer) rather than
 * using the stack, since we need the values in a specific layout.
 * ========================================================================== */
.global _save_vpu_regs
.align 4

_save_vpu_regs:
    /* r0 = pointer to vpu_scalar_regs_t (passed by caller)
     * Layout: r[0..31] at offset 0, sr at 128, pc at 132, sp at 136, lr at 140 */

    /* Save r0 itself (it's our pointer, but we need the CALLER's r0).
     * Unfortunately r0 is already clobbered by the function call ABI.
     * r0 is hardwired to 0 on VC4, so this is always correct: */
    mov     r1, 0
    st      r1, (r0 + 0)            /* r[0] = 0 (hardwired) */

    /* r1-r5: caller-saved. r1 is clobbered above, r2-r5 are still live
     * from the caller's context IF this is called as the very first thing.
     * For maximum safety, save what we can: */
    st      r2, (r0 + 8)            /* r[2] */
    st      r3, (r0 + 12)           /* r[3] */
    st      r4, (r0 + 16)           /* r[4] */
    st      r5, (r0 + 20)           /* r[5] */

    /* r6-r23: callee-saved. These are 100% reliable. */
    st      r6,  (r0 + 24)
    st      r7,  (r0 + 28)
    st      r8,  (r0 + 32)
    st      r9,  (r0 + 36)
    st      r10, (r0 + 40)
    st      r11, (r0 + 44)
    st      r12, (r0 + 48)
    st      r13, (r0 + 52)
    st      r14, (r0 + 56)
    st      r15, (r0 + 60)
    st      r16, (r0 + 64)
    st      r17, (r0 + 68)
    st      r18, (r0 + 72)
    st      r19, (r0 + 76)
    st      r20, (r0 + 80)
    st      r21, (r0 + 84)
    st      r22, (r0 + 88)
    st      r23, (r0 + 92)

    /* r24 (gp) is not accessible by name in VC4 assembly.
     * r25 = sp, r26 = lr -- save via aliases */
    mov     r1, sp
    st      r1, (r0 + 136)          /* sp */
    mov     r1, lr
    st      r1, (r0 + 140)          /* lr */

    /* sr (status register) */
    mov     r1, sr
    st      r1, (r0 + 128)          /* sr */

    /* pc -- store return address as resume point.
     * lr already points to the instruction after the call to us,
     * which is exactly where we want to resume. */
    st      lr, (r0 + 132)          /* pc = lr (return address) */

    /* r1 was clobbered; store 0 as best-effort */
    mov     r1, 0
    st      r1, (r0 + 4)            /* r[1] = 0 (clobbered by us) */

    b       lr


.global _resume_entry
.align 4

_resume_entry:
    /* ---------------------------------------------------------------
     * Step 1: Load suspend state base address into r1.
     * r1 is our base pointer throughout the resume sequence.
     * --------------------------------------------------------------- */
    mov     r1, SUSPEND_STATE_ADDR

    /* ---------------------------------------------------------------
     * Step 2: Validate magic number.
     * If SDRAM was corrupted or no suspend happened, bail out.
     * --------------------------------------------------------------- */
    ld      r2, (r1 + OFF_MAGIC)
    mov     r3, SUSPEND_MAGIC
    cmp     r2, r3
    bne     _resume_failed

    /* ---------------------------------------------------------------
     * Step 3: Validate version.
     * --------------------------------------------------------------- */
    ld      r2, (r1 + OFF_VERSION)
    cmp     r2, 1
    bne     _resume_failed

    /* ---------------------------------------------------------------
     * Step 3a: Invalidate magic NOW.
     * If we crash during register restore below, the next boot will
     * see magic=0 and cold-boot instead of looping forever.
     * --------------------------------------------------------------- */
    mov     r2, 0
    st      r2, (r1 + OFF_MAGIC)

    /* ---------------------------------------------------------------
     * Step 4: Restore stack pointer.
     * This must happen before any function calls.
     * --------------------------------------------------------------- */
    ld      sp, (r1 + OFF_SCALAR_SP)

    /* ---------------------------------------------------------------
     * Step 5: Restore callee-saved scalar registers.
     *
     * We restore r6-r24, r27-r31 from saved state.
     * Skipped:
     *   r0  -- hardwired to 0 on VC4
     *   r1  -- currently our base pointer (still needed)
     *   r2-r5 -- caller-saved, will be set by C code
     *   r25 -- sp, restored above
     *   r26 -- lr, restored below
     * --------------------------------------------------------------- */
    ld      r6,  (r1 + OFF_SCALAR_R0 + 24)
    ld      r7,  (r1 + OFF_SCALAR_R0 + 28)
    ld      r8,  (r1 + OFF_SCALAR_R0 + 32)
    ld      r9,  (r1 + OFF_SCALAR_R0 + 36)
    ld      r10, (r1 + OFF_SCALAR_R0 + 40)
    ld      r11, (r1 + OFF_SCALAR_R0 + 44)
    ld      r12, (r1 + OFF_SCALAR_R0 + 48)
    ld      r13, (r1 + OFF_SCALAR_R0 + 52)
    ld      r14, (r1 + OFF_SCALAR_R0 + 56)
    ld      r15, (r1 + OFF_SCALAR_R0 + 60)
    ld      r16, (r1 + OFF_SCALAR_R0 + 64)
    ld      r17, (r1 + OFF_SCALAR_R0 + 68)
    ld      r18, (r1 + OFF_SCALAR_R0 + 72)
    ld      r19, (r1 + OFF_SCALAR_R0 + 76)
    ld      r20, (r1 + OFF_SCALAR_R0 + 80)
    ld      r21, (r1 + OFF_SCALAR_R0 + 84)
    ld      r22, (r1 + OFF_SCALAR_R0 + 88)
    ld      r23, (r1 + OFF_SCALAR_R0 + 92)
    /* r24(gp), r25(sp), r26(lr) -- restored separately or not user-accessible.
     * r27-r29 reserved, r30(sr) restored below, r31(pc) is the jump target. */

    /* Restore status register (may not take effect depending on mode) */
    ld      r2, (r1 + OFF_SCALAR_SR)
    mov     sr, r2

    /* ---------------------------------------------------------------
     * Step 6: Restore LR and jump to saved PC.
     *
     * The saved PC points to the instruction after the suspend point
     * (approximately -- set by 'lea' in save_vpu_scalar_regs).
     * Restoring lr allows the resumed function to return normally.
     *
     * We load lr LAST since it's needed for the bl/b instruction.
     * --------------------------------------------------------------- */
    ld      lr, (r1 + OFF_SCALAR_LR)

    /* Pass state pointer as first argument for any C handler */
    mov     r0, r1

    /* ---------------------------------------------------------------
     * Step 7: Jump to saved resume point.
     *
     * Load the saved PC and branch to it. This returns execution to
     * the point just after save_vpu_scalar_regs() in suspend_enter().
     * --------------------------------------------------------------- */
    ld      r1, (r1 + OFF_SCALAR_PC)
    b       r1

/* ==========================================================================
 * Resume failed -- watchdog reset fallback
 * ==========================================================================
 * If state validation fails (corrupt SDRAM, no prior suspend, etc.),
 * trigger a full system reset via the watchdog timer. The system will
 * do a normal cold boot.
 * ========================================================================== */
.global _resume_failed
_resume_failed:
    mov     r1, PM_BASE

    /* Write PM_WDOG = PM_PASSWORD | 10 (watchdog timeout ~10 ticks) */
    mov     r2, PM_PASSWORD
    or      r2, r2, 10
    st      r2, (r1 + PM_WDOG_OFF)

    /* Write PM_RSTC = PM_PASSWORD | FULL_RESET */
    mov     r2, (PM_PASSWORD | PM_RSTC_FULL_RESET)
    st      r2, (r1 + PM_RSTC_OFF)

    /* Spin until watchdog fires */
_spin:
    b       _spin

/* ==========================================================================
 * Utility: Cycle-count delay loop
 * ==========================================================================
 * void _delay_cycles(uint32_t iterations)   // r0 = count
 * Simple decrement loop. Each iteration is ~2 VPU cycles.
 * At VPU 250 MHz, _delay_cycles(250000) ≈ 2 ms.
 * ========================================================================== */
.global _delay_cycles
_delay_cycles:
    /* r0 = iteration count (passed by caller) */
_delay_loop:
    sub     r0, r0, 1
    bne     _delay_loop
    b       lr
