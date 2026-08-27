#pragma once

#ifdef _WINDOWS64

// 4J Meow - Persistence for the Windows x64 profile stub.
//
// On console, per-player settings (volumes, sensitivity, difficulty, HUD
// options, selected skin, tutorial flags...) live in the GAME_SETTINGS blob
// that the 4J profile library stores inside the platform user profile. The
// x64 build has no such library: C_4JProfile in Extrax64Stubs.cpp hands out a
// heap buffer and WriteToProfile() was an empty stub, so every option reverted
// to its default on the next launch.
//
// This mirrors that blob to "profile.dat" in the working directory - the same
// convention the dedicated server uses for server.properties.
//
// See docs/systems/windows-x64-profile-persistence.md.

namespace Win64ProfileStore
{
	// Called once from C_4JProfile::Initialise, after the defaults have been
	// applied, with the four per-pad blobs. Reads profile.dat over the top of
	// any blob it has a saved copy of. Blobs with no saved copy keep their
	// defaults.
	void Load(void **ppvData, int iBlobSize);

	// True once Load() has restored pad iPad from disk. InitGameSettings uses
	// this to skip SetDefaultOptions, which would otherwise immediately
	// overwrite everything just loaded.
	bool HasSavedProfile(int iPad);

	// Writes all four blobs back out. Cheap enough (a few KB) to do on every
	// settings change rather than tracking dirty pads.
	void Save();
}

#endif // _WINDOWS64
