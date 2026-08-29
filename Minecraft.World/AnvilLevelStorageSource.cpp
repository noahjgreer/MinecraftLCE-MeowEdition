#include "stdafx.h"

#ifdef _WINDOWS64

#include "AnvilLevelStorageSource.h"
#include "AnvilLevelStorage.h"
#include "AnvilSavePaths.h"
#include "NativeSaveFile.h"

#include "LevelData.h"
#include "LevelSummary.h"
#include "CompoundTag.h"
#include "File.h"

// 4J Meow - see AnvilLevelStorageSource.h.

AnvilLevelStorageSource::AnvilLevelStorageSource(File dir)
	: DirectoryLevelStorageSource(dir)
{
}

wstring AnvilLevelStorageSource::getName()
{
	return L"Anvil";
}

shared_ptr<LevelStorage> AnvilLevelStorageSource::selectLevel(ConsoleSaveFile *saveFile, const wstring &levelId, bool createPlayerDir)
{
	return shared_ptr<LevelStorage>(new AnvilLevelStorage(saveFile, baseDir, levelId, createPlayerDir));
}

vector<LevelSummary *> *AnvilLevelStorageSource::getLevelList()
{
	// The base class returns an empty list - 4J disabled directory enumeration because
	// the consoles pick worlds through the platform save UI instead. A Java-shaped saves
	// folder can be enumerated properly, so do that.
	vector<LevelSummary *> *levels = new vector<LevelSummary *>();

	vector<wstring> worlds = AnvilSavePaths::listWorlds();

	for (unsigned int i = 0; i < worlds.size(); i++)
	{
		// The storage takes ownership and deletes this in ~DirectoryLevelStorage, so it
		// has to be on the heap - a stack save file here corrupts the heap on scope exit.
		NativeSaveFile *saveFile = new NativeSaveFile(AnvilSavePaths::worldDirectory(worlds[i]));

		AnvilLevelStorage storage(saveFile, baseDir, worlds[i], false);
		LevelData *levelData = storage.prepareLevel();
		if (levelData == NULL) continue;

		levels->push_back(new LevelSummary(worlds[i], levelData->getLevelName(),
		                                   levelData->getLastPlayed(), saveFile->getSizeOnDisk(),
		                                   levelData->getGameType(), false, levelData->isHardcore(),
		                                   levelData->getAllowCommands()));
		delete levelData;
	}

	return levels;
}

LevelData *AnvilLevelStorageSource::loadLevelDataFor(const wstring &worldName)
{
	if (worldName.empty()) return NULL;

	// Heap-allocated because the storage owns and deletes it - see getLevelList().
	NativeSaveFile *saveFile = new NativeSaveFile(AnvilSavePaths::worldDirectory(worldName));

	AnvilLevelStorage storage(saveFile, baseDir, worldName, false);
	return storage.prepareLevel();
}

LevelData *AnvilLevelStorageSource::getDataTagFor(ConsoleSaveFile *saveFile, const wstring &levelId)
{
	// The base class reads level.dat through the save-file stream, which cannot see a
	// gzip wrapper. Going through AnvilLevelStorage keeps that logic in one place and
	// means a world written before this change still reports its summary correctly.
	NativeSaveFile *native = dynamic_cast<NativeSaveFile *>(saveFile);
	if (native == NULL) return DirectoryLevelStorageSource::getDataTagFor(saveFile, levelId);

	// saveFile belongs to the caller, so the storage must not delete it on scope exit.
	AnvilLevelStorage storage(saveFile, baseDir, levelId, false, false);
	return storage.prepareLevel();
}

#endif // _WINDOWS64
