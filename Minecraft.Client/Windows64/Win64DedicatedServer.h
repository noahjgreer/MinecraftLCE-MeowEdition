#pragma once

#ifdef _WINDOWS64

// 4J Meow - Dedicated server mode for the Windows x64 build.
//
//   Minecraft.Client.exe -dedicated [-world <name>] [-port <n>] [-seed <n>] [-maxplayers <n>]
//
// The client is still initialised - MinecraftServer reaches for
// Minecraft::GetInstance()->progressRenderer and ->gameRenderer - but the window is
// hidden and, instead of a local player joining, only the server thread is started.
// See CGameNetworkManager::StartDedicatedServer and docs/systems/dedicated-server.md.
//
// The world itself is plain McRegion files under the working directory, driven by
// server.properties, exactly like the Java dedicated server.

namespace Win64DedicatedServer
{
	// True when -dedicated was on the command line.
	bool IsEnabled();

	// Attaches a console and installs the Ctrl+C handler. Call before anything
	// that logs.
	void Initialise();

	// Timestamped console output. Log() is also where app.DebugPrintf is
	// mirrored, so the server's own existing tracing shows up.
	void Log(const char *pszFormat, ...);
	void LogRaw(const char *pszText);

	// Writes server.properties if it is not already there, then hosts and starts
	// the server thread. Called once, after Minecraft::init(). Returns false if
	// the server could not be started.
	bool Start();

	// Per-frame housekeeping: the periodic status line, and acting on a pending
	// Ctrl+C.
	void Tick();

	// Halts the server so it saves, and waits for it to finish.
	void Shutdown();
}

#endif // _WINDOWS64
