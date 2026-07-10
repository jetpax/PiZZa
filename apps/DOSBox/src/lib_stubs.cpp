/* Stubs for optional libraries referenced by kept TUs.
 *
 * - opusfile + speexdsp: cdrom_image.cpp calls them directly for Opus
 *   CD-audio tracks and sample-rate conversion; both fail gracefully
 *   at runtime (tracks report unreadable). Revisit in the audio phase.
 * - OPL2AudioBoard / Opl3DuoBoard: serial-port OPL hardware, never
 *   selected by config; only the constructors are referenced.
 */
#include "config.h"

extern "C" {

void *op_open_callbacks(void *stream, const void *cb, const unsigned char *data,
			size_t count, int *error)
{
	(void)stream; (void)cb; (void)data; (void)count;
	if (error) *error = -1;
	return 0;
}
void op_free(void *of) { (void)of; }
const void *op_head(void *of, int li) { (void)of; (void)li; return 0; }
int op_seekable(void *of) { (void)of; return 0; }
long long op_pcm_total(void *of, int li) { (void)of; (void)li; return -1; }
int op_pcm_seek(void *of, long long offset) { (void)of; (void)offset; return -1; }
int op_read(void *of, short *pcm, int buf_size, int *li)
{
	(void)of; (void)pcm; (void)buf_size; (void)li;
	return -1;
}

void *speex_resampler_init(unsigned int channels, unsigned int in_rate,
			   unsigned int out_rate, int quality, int *err)
{
	(void)channels; (void)in_rate; (void)out_rate; (void)quality;
	if (err) *err = -1;
	return 0;
}
void speex_resampler_destroy(void *st) { (void)st; }
int speex_resampler_process_int(void *st, unsigned int idx,
				const short *in, unsigned int *in_len,
				short *out, unsigned int *out_len)
{
	(void)st; (void)idx; (void)in; (void)out;
	if (in_len) *in_len = 0;
	if (out_len) *out_len = 0;
	return -1;
}

} /* extern "C" */

/* pc98_fm.cpp (dormant, not built) — referenced by dosbox.cpp VM-event
 * registration and bios.cpp callback install, both runtime-gated on
 * PC-98 machine types we never select.
 */
class Section;
void PC98_FM_Destroy(Section *sec) { (void)sec; }
void PC98_FM_OnEnterPC98(Section *sec) { (void)sec; }
Bitu PC98_AVSDRV_PCM_Handler(void) { return 0; }

/* voodoo/glide TUs dropped; menu callbacks reference their VM hooks */
void VOODOO_Destroy(Section *sec) { (void)sec; }
void VOODOO_OnPowerOn(Section *sec) { (void)sec; }
void GLIDE_PowerOn(Section *sec) { (void)sec; }
void GLIDE_ShutDown(Section *sec) { (void)sec; }

/* serial OPL boards: adlib.cpp instantiates them unconditionally but
 * only drives them under oplemu= choices we never select. Their real
 * TUs own std::threads (no gthreads on Zephyr) — method stubs suffice.
 */
#include "opl2board/opl2board.h"
#include "opl3duoboard/opl3duoboard.h"

OPL2AudioBoard::OPL2AudioBoard() {}
void OPL2AudioBoard::connect(const char *port) { (void)port; }
void OPL2AudioBoard::disconnect() {}
void OPL2AudioBoard::reset() {}
void OPL2AudioBoard::write(uint8_t reg, uint8_t val) { (void)reg; (void)val; }

Opl3DuoBoard::Opl3DuoBoard() {}
void Opl3DuoBoard::connect(const char *port) { (void)port; }
void Opl3DuoBoard::disconnect() {}
void Opl3DuoBoard::reset() {}
void Opl3DuoBoard::write(uint32_t reg, uint8_t val) { (void)reg; (void)val; }
