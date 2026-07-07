/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- SDL_RWops.
 *
 * Memory-backed RWops are real (load_image feeds IMG_Load_RW through
 * SDL_RWFromConstMem). File-backed RWops land with the asset/FS
 * phase; SDLPoP only uses them for SDLPoP.cfg, so failing them is a
 * clean "no saved config" until then.
 */

#include <zephyr/kernel.h>

#include "sdl2shim.h"

enum {
	POP_RW_MEM = 1,
	POP_RW_CONSTMEM,
};

static Sint64 mem_size(SDL_RWops *ctx)
{
	return ctx->mem.stop - ctx->mem.base;
}

static Sint64 mem_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
	Uint8 *target;

	switch (whence) {
	case RW_SEEK_SET:
		target = ctx->mem.base + offset;
		break;
	case RW_SEEK_CUR:
		target = ctx->mem.here + offset;
		break;
	case RW_SEEK_END:
		target = ctx->mem.stop + offset;
		break;
	default:
		s2s_set_error("RW mem_seek: bad whence %d", whence);
		return -1;
	}
	if (target < ctx->mem.base) {
		target = ctx->mem.base;
	}
	if (target > ctx->mem.stop) {
		target = ctx->mem.stop;
	}
	ctx->mem.here = target;
	return ctx->mem.here - ctx->mem.base;
}

static size_t mem_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
	if (size == 0 || maxnum == 0) {
		return 0;
	}

	size_t avail = (size_t)(ctx->mem.stop - ctx->mem.here);
	size_t want = size * maxnum;
	size_t take = MIN(want, avail);
	size_t num = take / size;

	memcpy(ptr, ctx->mem.here, num * size);
	ctx->mem.here += num * size;
	return num;
}

static size_t mem_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
	if (ctx->type == POP_RW_CONSTMEM) {
		s2s_set_error("RW mem_write: const memory");
		return 0;
	}

	size_t avail = (size_t)(ctx->mem.stop - ctx->mem.here);
	size_t take = MIN(size * num, avail);
	size_t n = take / size;

	memcpy(ctx->mem.here, ptr, n * size);
	ctx->mem.here += n * size;
	return n;
}

static int mem_close(SDL_RWops *ctx)
{
	free(ctx);
	return 0;
}

static SDL_RWops *rw_from_mem(void *mem, int size, Uint32 type)
{
	if (mem == NULL || size <= 0) {
		s2s_set_error("RWFromMem: bad buffer");
		return NULL;
	}

	SDL_RWops *rw = calloc(1, sizeof(*rw));

	if (rw == NULL) {
		s2s_set_error("RWFromMem: out of memory");
		return NULL;
	}
	rw->size = mem_size;
	rw->seek = mem_seek;
	rw->read = mem_read;
	rw->write = mem_write;
	rw->close = mem_close;
	rw->type = type;
	rw->mem.base = mem;
	rw->mem.here = mem;
	rw->mem.stop = (Uint8 *)mem + size;
	return rw;
}

SDL_RWops *SDL_RWFromMem(void *mem, int size)
{
	return rw_from_mem(mem, size, POP_RW_MEM);
}

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
	return rw_from_mem((void *)mem, size, POP_RW_CONSTMEM);
}

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode)
{
	ARG_UNUSED(mode);
	/* PHASE1-STUB: file-backed RW (SDLPoP.cfg) lands with the FS phase */
	s2s_set_error("RWFromFile: no filesystem backend yet (%s)", file);
	return NULL;
}

size_t SDL_RWread(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
	return ctx->read(ctx, ptr, size, maxnum);
}

size_t SDL_RWwrite(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
	return ctx->write(ctx, ptr, size, num);
}

Sint64 SDL_RWtell(SDL_RWops *ctx)
{
	return ctx->seek(ctx, 0, RW_SEEK_CUR);
}

int SDL_RWclose(SDL_RWops *ctx)
{
	return ctx->close(ctx);
}
