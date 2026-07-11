#pragma once
#ifndef HP_ASSERT_H
#define HP_ASSERT_H

#include <stdio.h>

#ifndef RELEASE
#define HP_ASSERTS_ENABLED 1
#endif

#if defined _MSC_VER
#define HP_BREAK __debugbreak();
#elif defined __arm__ || defined __aarch64__
#define HP_BREAK __builtin_trap();
#elif defined __GNUC__
#define HP_BREAK __asm__ ("int $3");
#else
#error Unsupported compiler
#endif

void HpAssertMessage(const char* expr, const char* type, const char* file, int line, const char* func, const char* fmt = nullptr, ...);

#if HP_ASSERTS_ENABLED

// http://cnicholson.net/2009/02/stupid-c-tricks-adventures-in-assert/
#define HP_ASSERT(expr, ...) \
	do \
	{ \
		if( !(expr) ) \
		{ \
			HpAssertMessage(#expr, "ASSERT", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
		HP_BREAK \
		} \
	} while (0)

#define HP_FATAL_ERROR(...) \
	do \
	{ \
		HpAssertMessage("", "FATAL ERROR", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
		HP_BREAK \
	} while (0)

#else

#define HP_ASSERT(expr, ...)
#define HP_FATAL_ERROR(...)

#endif

// this macro is always defined
#define HP_VERIFY(expr, ...) \
	do \
	{ \
		if( !(expr) ) \
		{ \
			HpAssertMessage(#expr, "VERIFY FAIL", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
		HP_BREAK \
		} \
	} while (0)

#endif // HP_ASSERT_H
