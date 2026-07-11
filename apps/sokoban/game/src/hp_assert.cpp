#include "hp_assert.h"

#include <stdarg.h>

void HpAssertMessage(const char* expr, const char* type, const char* file, int line, const char* func, const char* fmt /*= nullptr*/, ...)
{
	char message[1024];
	message[0] = '\0';

	if(fmt)
	{
		va_list argList;
		va_start(argList, fmt);
		vsnprintf(message, sizeof(message)-1, fmt, argList);
		message[sizeof(message)-1] = '\0';		// ensure NULL terminated
		va_end(argList);
	}

	fprintf(stderr, 
		"\n"
		"%20s:\t%s\n"
		"%20s:\t%s\n"
		"%20s:\t%s\n"
		"%20s:\t%s(%d)\n"
		"\n", 
		type, expr, 
		"Message", message, 
		"Function", func,
		"File", file, line); 
}

