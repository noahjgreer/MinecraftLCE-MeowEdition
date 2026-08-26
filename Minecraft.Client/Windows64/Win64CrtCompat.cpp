// 4J Meow - Compatibility shims for linking the prebuilt VS2012 4JLibs
// middleware (4J_Input / 4J_Storage / 4J_Render_PC) against the modern
// Universal CRT.
//
// Those .lib files ship without source, so they still reference CRT symbols
// that were removed when the UCRT landed in VS2015. Each definition below
// stands in for one of them. Only modern toolsets need this; v110 builds are
// unaffected and compile this file away to nothing.

#include "stdafx.h"

#if defined(_MSC_VER) && _MSC_VER >= 1900 && defined(_WINDOWS64)

#include <stdio.h>

extern "C" {

// Pre-UCRT stdio exposed the three standard streams as an array reachable
// through __iob_func(). The UCRT replaced it with per-stream accessors, so
// hand back a small array built from those.
static FILE _iob_compat[3];

FILE *__cdecl __iob_func(void)
{
	_iob_compat[0] = *stdin;
	_iob_compat[1] = *stdout;
	_iob_compat[2] = *stderr;
	return _iob_compat;
}

} // extern "C"

// Internal helper the VS2012 <system_error> emitted calls to, mapping a Win32
// error code to a human-readable name. The modern STL still declares a
// _Winerror_map, but with a different return type, so we cannot simply define
// the old one - redeclaring it clashes. Instead bind the old decorated name
// (const char *__cdecl std::_Winerror_map(int)) to a stub via /alternatename,
// which the linker only uses if nothing else resolves it. Returning null is the
// documented "unknown error" path and is all the middleware needs.
extern "C" const char *__cdecl _4J_Winerror_map_stub(int)
{
	return 0;
}

#pragma comment(linker, "/alternatename:?_Winerror_map@std@@YAPEBDH@Z=_4J_Winerror_map_stub")

#endif // _MSC_VER >= 1900 && _WINDOWS64
