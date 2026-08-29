#pragma once

#ifdef _WINDOWS64

using namespace std;

#include "DirectoryLevelStorageSource.h"

class ProgressListener;
class LevelSummary;
class LevelStorage;

// 4J Meow - The LevelStorageSource that hands out AnvilLevelStorage.
//
// It inherits DirectoryLevelStorageSource's world enumeration, renaming and deletion
// unchanged and overrides only the two places that touch level.dat, which is gzipped
// and Java-shaped now.

class AnvilLevelStorageSource : public DirectoryLevelStorageSource
{
public:
	AnvilLevelStorageSource(File dir);

	virtual wstring getName();
	virtual vector<LevelSummary *> *getLevelList();
	virtual shared_ptr<LevelStorage> selectLevel(ConsoleSaveFile *saveFile, const wstring &levelId, bool createPlayerDir);
	virtual LevelData *getDataTagFor(ConsoleSaveFile *saveFile, const wstring &levelId);

	// The saved level.dat for one world, opened by name. The caller owns the result and
	// gets NULL when there is no readable world by that name. Used by the load menu,
	// which has a world name but no save file of its own.
	LevelData *loadLevelDataFor(const wstring &worldName);

	// An Anvil world is already in its final form; there is nothing to convert.
	virtual bool isConvertible(ConsoleSaveFile *saveFile, const wstring &levelId) { return false; }
	virtual bool requiresConversion(ConsoleSaveFile *saveFile, const wstring &levelId) { return false; }
	virtual bool convertLevel(ConsoleSaveFile *saveFile, const wstring &levelId, ProgressListener *progress) { return true; }
};

#endif // _WINDOWS64
