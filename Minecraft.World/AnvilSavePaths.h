#pragma once

#ifdef _WINDOWS64

using namespace std;

#include "stdafx.h"

// 4J Meow - The single authority for where worlds live on disk.
//
// Before this existed the two halves of the game disagreed: Minecraft.cpp pointed the
// level source at <working directory>/saves, while MinecraftServer::loadLevel built its
// save file as "<level name>" relative to the current directory. The world list and the
// world data were looking at different places.
//
// Everything now resolves through here, giving the layout Java uses:
//
//     <saves root>/<world name>/level.dat
//     <saves root>/<world name>/region/r.0.0.mca
//     <saves root>/<world name>/playerdata/<uid>.dat
//     <saves root>/<world name>/DIM-1/region/...
//
// The root defaults to "saves" relative to the current directory, which is what a
// dedicated server with no Minecraft instance gets. The client overrides it during
// startup once it knows its working directory.

namespace AnvilSavePaths
{
	void setSavesRoot(const wstring &root);
	const wstring &getSavesRoot();

	// <saves root>/<levelId>. Does not create anything.
	wstring worldDirectory(const wstring &levelId);

	// Every immediate subdirectory of the saves root that contains a level.dat.
	vector<wstring> listWorlds();

	// The world the next server start should open.
	//
	// On console this job belongs to C4JStorage (StorageManager): the create-world and
	// load-world screens set a save title, and the server takes its level from the
	// selected save slot. StorageManager.Init() is inside an #if 0 on Windows x64
	// (Windows64_Minecraft.cpp), so none of that exists here - which is why every world
	// used to open "Server World" from server.properties no matter what was typed.
	//
	// Empty means "not chosen"; MinecraftServer then falls back to server.properties,
	// which is what the dedicated server wants.
	void setCurrentWorld(const wstring &name);
	const wstring &getCurrentWorld();

	// Turns a user-entered world name into a directory name that is safe on Windows and
	// not already taken, e.g. "My World" -> "My World (2)".
	wstring makeUniqueWorldName(const wstring &displayName);

	// 4J Meow - DIAGNOSTIC. Appends to anvil.log next to the executable's working
	// directory and mirrors to OutputDebugString. The save path has no other visible
	// tracing on a client build (app.DebugPrintf only reaches the console when the
	// dedicated server is running), which made a silent failure impossible to locate.
	// Safe to delete once the save path is trusted.
	void log(const char *format, ...);
}

#endif // _WINDOWS64
