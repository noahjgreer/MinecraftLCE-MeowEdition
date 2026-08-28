#pragma once

#ifdef _WINDOWS64

#include <string>

// 4J Meow - Plain-file world blob storage for the Windows x64 build.
//
// On the consoles the whole world is one compressed blob handed to
// C4JStorage (4J_Storage.lib) as a save-game "slot". That layer was never
// brought up on Windows x64 - StorageManager.Init() sits inside an #if 0 in
// Windows64_Minecraft.cpp because the Win64 build of the library takes a
// different Init signature - so ConsoleSaveFileOriginal built its save blob
// and then handed it to a manager that dropped it on the floor. Nothing was
// ever written and every launch generated a fresh world.
//
// This is the replacement backend for that one job, and only that job: read
// and write the blob as an ordinary file. The bytes are byte-for-byte what
// StorageManager would have been given, so the format is unchanged and the
// existing ConsoleSaveFileOriginal load path parses it as-is:
//
//     [int32 0 = compressed] [int32 decompressed size] [deflate stream]
//
// Saves land in <world name>/savegame.dat relative to the working directory,
// matching the level-name convention the dedicated server already uses for
// server.properties.
//
// See docs/systems/dedicated-server.md.
namespace Win64SaveFile
{
	// Sets the world whose blob we read and write. Called from
	// MinecraftServer::loadLevel, so it covers the client and the dedicated
	// server alike. Defaults to L"world" if never called.
	void SetWorldName(const std::wstring &name);
	const std::wstring &GetWorldName();

	// <world name>/savegame.dat
	std::wstring GetSavePath();

	// Size in bytes of the stored blob, or 0 if there is not one. This is the
	// StorageManager.GetSaveSize() replacement, so 0 must mean "generate a new
	// world" exactly as it did before.
	unsigned int GetSize();

	// Reads the whole blob into pvData, which must be at least GetSize() bytes.
	// *puiBytes receives the number actually read. False leaves *puiBytes at 0.
	bool Read(void *pvData, unsigned int *puiBytes);

	// Writes the blob, replacing any previous one. Writes to a .tmp alongside
	// and renames over the target, so an interrupted save cannot leave a
	// half-written world where a good one used to be.
	bool Write(const void *pvData, unsigned int uiBytes);
}

#endif // _WINDOWS64
