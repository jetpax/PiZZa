/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * sdl2shim-over-libretro -- minimal C++ runtime for C++ core builds.
 *
 * llext relocatable cores are partial-linked (-r): no libstdc++ is
 * pulled in, so the handful of ABI symbols C++ games actually need is
 * provided here and resolves inside the core. No exceptions, no RTTI
 * (the fleet's GLIBCXX_LIBCPP recipe builds with both off). Static
 * destructors are intentionally never run -- unloading the llext IS
 * the destructor.
 */

#include <stdlib.h>
#include <stddef.h>

extern "C" {
void *__dso_handle;

int __cxa_guard_acquire(long long *g)
{
	return !*(volatile char *)g;
}

void __cxa_guard_release(long long *g)
{
	*(volatile char *)g = 1;
}

void __cxa_guard_abort(long long *g)
{
	(void)g;
}

void __cxa_pure_virtual(void)
{
	for (;;) {
	}
}

int __cxa_atexit(void (*fn)(void *), void *arg, void *dso)
{
	(void)fn;
	(void)arg;
	(void)dso;
	return 0;
}
}

void *operator new(size_t n)
{
	return malloc(n);
}

void *operator new[](size_t n)
{
	return malloc(n);
}

void operator delete(void *p) noexcept
{
	free(p);
}

void operator delete[](void *p) noexcept
{
	free(p);
}

void operator delete(void *p, size_t n) noexcept
{
	(void)n;
	free(p);
}

void operator delete[](void *p, size_t n) noexcept
{
	(void)n;
	free(p);
}
