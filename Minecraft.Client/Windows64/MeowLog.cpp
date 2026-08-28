#include "stdafx.h"

#if defined(_WINDOWS64)

#include "MeowLog.h"
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>

// See MeowLog.h - temporary diagnostic scaffolding.
void MeowLogf(const char *fmt, ...)
{
	static FILE *s_pFile = NULL;
	static bool s_bTried = false;

	if(!s_bTried)
	{
		s_bTried = true;
		fopen_s(&s_pFile, "meow_debug.log", "w");
	}

	char buffer[1024];
	va_list args;
	va_start(args, fmt);
	_vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
	va_end(args);

	// Both sinks: the file survives a hang, the debugger output is live.
	OutputDebugStringA(buffer);

	if(s_pFile != NULL)
	{
		fputs(buffer, s_pFile);
		fflush(s_pFile);		// a hang must not lose the last line
	}
}

#endif
