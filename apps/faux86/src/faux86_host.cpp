/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PiZZa Faux86 -- Zephyr HostSystemInterface impl. Present goes through the
 * app-local s2s_present_* seam (semihost PPM on qemu, bcm2835_fb on HW);
 * timing is Zephyr cycles; audio is a silent stub for now (MAI is a later
 * phase); disks are handed to Config directly by faux86_main, so openFile
 * is unused.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <cstdarg>
#include <cstdio>

#include "Config.h"
#include "Types.h"
#include "HostSystemInterface.h"
#include "Renderer.h"
#include "VM.h"
#include "DriveManager.h"

#include "faux86_host.h"
#include "faux86_present.h"

using namespace Faux86;

/* Faux86 requires the host to provide log(). */
namespace Faux86 {
void log(LogChannel channel, const char *message, ...)
{
	char buf[256];
	va_list args;

	va_start(args, message);
	vsnprintf(buf, sizeof(buf), message, args);
	va_end(args);

	if (channel == LogRaw) {
		printk("%s", buf);
	} else {
		printk("%s\n", buf);
	}
}
} /* namespace Faux86 */

/*
 * The base HostSystemInterface declares non-pure init()/resize() with no
 * bodies (derived classes override them). Define them out-of-line so the
 * vtable + typeinfo are emitted (Itanium ABI key-function rule; MSVC/Circle
 * tolerate the omission, aarch64 does not).
 */
void Faux86::HostSystemInterface::init(VM *inVM) { (void)inVM; }
void Faux86::HostSystemInterface::resize(uint32_t w, uint32_t h) { (void)w; (void)h; }

/* ---------------------------------------------------------------- FrameBuffer */
void Faux86FrameBufferInterface::init(uint32_t w, uint32_t h)
{
	resize(w, h);
}

void Faux86FrameBufferInterface::resize(uint32_t w, uint32_t h)
{
	renderSurface.width = w;
	renderSurface.height = h;
	renderSurface.pitch = w;
	/*
	 * pixels intentionally left null: the active render path converts VGA
	 * memory to XRGB and hands that buffer straight to blit(). getSurface()
	 * is only stashed by Renderer::init (same as the SDL frontend).
	 */
}

RenderSurface *Faux86FrameBufferInterface::getSurface()
{
	return &renderSurface;
}

void Faux86FrameBufferInterface::setPalette(Palette *palette)
{
	(void)palette;
}

#if defined(ARDUINO) || (VIDEO_FRAMEBUFFER_DEPTH == 16)
void Faux86FrameBufferInterface::blit(uint16_t *pixels, int w, int h, int stride)
{
	/* Faux86 emits RGB565 (uint16_t); the present seam wants 32bpp XRGB. */
	size_t need = (size_t)w * (size_t)h * sizeof(uint32_t);

	if (need > xrgb_cap) {
		free(xrgb);
		xrgb = (uint32_t *)malloc(need);
		xrgb_cap = (xrgb != nullptr) ? need : 0;
	}
	if (xrgb == nullptr) {
		return;
	}

	for (int y = 0; y < h; y++) {
		const uint16_t *src = (const uint16_t *)((const uint8_t *)pixels + (size_t)stride * y);
		uint32_t *dst = xrgb + (size_t)w * y;

		for (int x = 0; x < w; x++) {
			uint16_t p = src[x];
			uint32_t r = (p >> 11) & 0x1F;
			uint32_t g = (p >> 5) & 0x3F;
			uint32_t b = p & 0x1F;

			dst[x] = (((r << 3) | (r >> 2)) << 16) |
				 (((g << 2) | (g >> 4)) << 8) |
				 ((b << 3) | (b >> 2));
		}
	}

	if (w != cur_w || h != cur_h) {
		s2s_present_init(w, h);
		cur_w = w;
		cur_h = h;
	}
	s2s_present_frame(xrgb, w * (int)sizeof(uint32_t));
}
#else
void Faux86FrameBufferInterface::blit(uint32_t *pixels, int w, int h, int stride)
{
	if (w != cur_w || h != cur_h) {
		s2s_present_init(w, h);
		cur_w = w;
		cur_h = h;
	}
	s2s_present_frame(pixels, stride);
}
#endif

/* --------------------------------------------------------------------- Timer */
uint64_t Faux86TimerInterface::getHostFreq()
{
	return (uint64_t)sys_clock_hw_cycles_per_sec();
}

uint64_t Faux86TimerInterface::getTicks()
{
	return k_cycle_get_64();
}

/* --------------------------------------------------------------------- Audio */
void Faux86AudioInterface::init(VM &vm)
{
	(void)vm;
	printk("[faux86] audio: silent stub (MAI backend is a later phase)\n");
}

void Faux86AudioInterface::shutdown() {}

/* ---------------------------------------------------------------- HostSystem */
Faux86HostSystemInterface::Faux86HostSystemInterface() {}

void Faux86HostSystemInterface::init(VM *inVM)
{
	vm = inVM;
}

void Faux86HostSystemInterface::resize(uint32_t w, uint32_t h)
{
	scrWidth = w;
	scrHeight = h;
}

DiskInterface *Faux86HostSystemInterface::openFile(const char *filename)
{
	(void)filename;
	return nullptr; /* disks are set directly on Config by faux86_main */
}

void Faux86HostSystemInterface::tick()
{
	/* input pump lands here in a later phase */
}
