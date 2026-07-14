/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- audio bridge (public entry point).
 */

#ifndef PIZZA_RETRO_AUDIO_H
#define PIZZA_RETRO_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ring accept limit, in 44.1 kHz stereo frames: push rejects once this
 * much is buffered, which is the glue producer's backpressure signal --
 * the MAI drain rate paces production and audio lag stays bounded at
 * ~35 ms.
 */
#define RETRO_AUDIO_TARGET_FRAMES 1536u

/* Open the HDMI-MAI audio backend and start its feeder. Call once at
 * boot (hardware only; qemu has no MAI). Returns 0, or negative if the
 * MAI path is unavailable (DVI sink / no HDMI display) -- the frontend
 * then simply runs silent.
 */
int retro_audio_init(void);

/* Producer seam: push `frames` of interleaved S16 stereo (44.1 kHz),
 * as handed to libretro's audio_batch callback, into the bridge ring.
 * Lock-free SPSC -- the core's audio producer is the sole producer, the
 * MAI feeder the sole consumer. All-or-nothing: returns `frames` when
 * accepted, 0 when the ring is full (the producer's backpressure signal
 * -- it retries the same chunk). Never blocks.
 */
size_t retro_audio_push(const int16_t *data, size_t frames);

/* RetroPiZZa-global master volume, percent of unity (0..200; >100
 * amplifies with clamping). Applied to every core's mixed stream as it
 * enters the bridge ring. Boot default = CONFIG_RETRO_AUDIO_GAIN_PERCENT;
 * `retro vol` adjusts live.
 */
void retro_audio_set_volume(unsigned int percent);
unsigned int retro_audio_get_volume(void);

#endif /* PIZZA_RETRO_AUDIO_H */
