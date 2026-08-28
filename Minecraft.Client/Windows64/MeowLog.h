#pragma once

// ---------------------------------------------------------------------------
// 4J Meow - TEMPORARY diagnostic logger.
//
// Added while chasing the hang on leaving the startup save-message screen.
// Writes a flushed line to meow_debug.log next to the working directory, so a
// hang leaves a complete trace up to the last statement that executed.
//
// This is scaffolding, not a feature. Delete this file, MeowLog.cpp and every
// MEOWLOG call once the native UI migration is stable.
// ---------------------------------------------------------------------------

#if defined(_WINDOWS64)
void MeowLogf(const char *fmt, ...);
#define MEOWLOG(...) MeowLogf(__VA_ARGS__)
#else
#define MEOWLOG(...) ((void)0)
#endif
