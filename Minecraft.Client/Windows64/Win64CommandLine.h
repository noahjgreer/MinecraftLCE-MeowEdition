#pragma once

#ifdef _WINDOWS64

// 4J Meow - Command-line direct connect for the Windows x64 build.
//
//   Minecraft.Client.exe -server <address> [-port <n>] [-name <display name>]
//
// This is the headless-friendly way in: it needs no UI at all, which matters
// because the main menu is a prebuilt Flash asset that cannot gain a button
// from source. The in-game equivalent is DirectConnectScreen (F6).
//
// The pre-existing single-digit resolution argument ("1", "2", "3") is still
// honoured, so old shortcuts keep working.
//
// See docs/systems/dedicated-server-and-direct-connect.md.

namespace Win64CommandLine
{
	// Takes the raw lpCmdLine, which is TCHAR - this project builds non-Unicode,
	// so that is char* here, and is widened internally.
	void Parse(const TCHAR *pszCommandLine);

	// Empty when -server was not given.
	const wchar_t *GetServerAddress();
	int GetServerPort();

	// Empty when -name was not given.
	const wchar_t *GetPlayerName();

	bool WantsDirectConnect();

	// Dedicated server: -dedicated [-world <name>] [-seed <n>] [-port <n>] [-maxplayers <n>]
	bool WantsDedicatedServer();

	// 4J Meow - -flashui keeps the original Iggy/Flash menus (no native Screen
	// intercept) and dumps each scene's live layout to meow_debug.log. This is
	// how the console layout is recovered for the native reimplementation:
	// tools/swf_layout.py reads positions out of the .swf, but control sizes
	// only exist at runtime because the buttons are AS3 library instances.
	bool WantsFlashUI();
	const wchar_t *GetWorldName();
	bool HasWorldName();
	__int64 GetSeed();
	bool HasSeed();
	int GetMaxPlayers();
}

#endif // _WINDOWS64
