/* ==========================================================================
 * boot_suspend_hook.h -- Integration API for upstream romstage.c
 * ========================================================================== */
#ifndef BOOT_SUSPEND_HOOK_H
#define BOOT_SUSPEND_HOOK_H

/* Call early in _main(), BEFORE sdram_init().
 * If a valid suspend state is found in SDRAM, this function does NOT return
 * -- it jumps to _resume_entry to restore the previous session.
 * If no valid state (cold boot), returns normally. */
void check_resume_from_suspend(void);

/* Call after platform init to trigger suspend-to-RAM.
 * Returns after wake/resume. */
void enter_suspend_mode(void);

#endif /* BOOT_SUSPEND_HOOK_H */
