#pragma once
#ifndef HELPERS_H
#define HELPERS_H

#include <stddef.h> // gcc size_t

#define PI 3.14159265359f
#define TWOPI 6.28318530718f

// http://cnicholson.net/2011/01/stupid-c-tricks-a-better-sizeof_array/
template<typename T, size_t N> char (&COUNTOF_REQUIRES_ARRAY_ARGUMENT(const T(&)[N]) )[N];
#define COUNTOF_ARRAY(_x) sizeof(COUNTOF_REQUIRES_ARRAY_ARGUMENT(_x) )

#define HP_UNUSED(X)	(void)X

#define FOURCC(a,b,c,d) ( (uint32_t) (((d)<<24) | ((c)<<16) | ((b)<<8) | (a)) )

template<typename T>
inline T Min( const T& a, const T& b )
{
	return (a < b) ? a : b;
}

template<typename T>
inline T Max( const T& a, const T& b )
{
	return (a > b) ? a : b;
}

template<typename T>
inline T Clamp( const T& v, const T& low, const T& high )
{
	return Max( Min( v, high ), low );
}

inline float Floor(float t)
{
	return (float)(int)t;
}

inline float Frac(float t)
{
	return t - Floor(t);
}

#endif
