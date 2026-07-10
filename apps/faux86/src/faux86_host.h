/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PiZZa Faux86 -- Zephyr implementation of Faux86's HostSystemInterface.
 * Mirrors the structure of win32/SDLInterface.cpp but drives the PiZZa
 * backends directly (present seam / Zephyr timer), no SDL.
 */
#pragma once

#include "Config.h"
#include "Types.h"
#include "HostSystemInterface.h"
#include "Renderer.h"

namespace Faux86 {

class VM;
class DiskInterface;

class Faux86FrameBufferInterface : public FrameBufferInterface {
public:
	void init(uint32_t desiredWidth, uint32_t desiredHeight) override;
	void resize(uint32_t desiredWidth, uint32_t desiredHeight) override;
	RenderSurface *getSurface() override;
	void setPalette(Palette *palette) override;
#if defined(ARDUINO) || (VIDEO_FRAMEBUFFER_DEPTH == 16)
	void blit(uint16_t *pixels, int w, int h, int stride) override;
#else
	void blit(uint32_t *pixels, int w, int h, int stride) override;
#endif

private:
	RenderSurface renderSurface{};
	int cur_w = -1;
	int cur_h = -1;
	uint32_t *xrgb = nullptr;   /* RGB565 -> XRGB scratch for the present seam */
	size_t xrgb_cap = 0;
};

class Faux86TimerInterface : public TimerInterface {
public:
	uint64_t getHostFreq() override;
	uint64_t getTicks() override;
};

class Faux86AudioInterface : public AudioInterface {
public:
	void init(VM &vm) override;
	void shutdown() override;
};

class Faux86HostSystemInterface : public HostSystemInterface {
public:
	Faux86HostSystemInterface();
	void init(VM *inVM) override;
	void resize(uint32_t desiredWidth, uint32_t desiredHeight) override;
	FrameBufferInterface &getFrameBuffer() override { return frameBufferInterface; }
	TimerInterface &getTimer() override { return timerInterface; }
	AudioInterface &getAudio() override { return audioInterface; }
	DiskInterface *openFile(const char *filename) override;
	void tick();

private:
	Faux86FrameBufferInterface frameBufferInterface;
	Faux86TimerInterface timerInterface;
	Faux86AudioInterface audioInterface;
};

} /* namespace Faux86 */
