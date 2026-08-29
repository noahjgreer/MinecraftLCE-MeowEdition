#pragma once

#ifdef _WINDOWS64

using namespace std;

#include "DirectoryLevelStorage.h"

class CompoundTag;

// 4J Meow - A DirectoryLevelStorage that keeps its world in Java Edition's layout.
//
// It subclasses rather than replaces DirectoryLevelStorage so that all of the world
// list, rename, delete, map-data and player-IO machinery keeps working untouched. Only
// three things change:
//
//   * createChunkStorage() hands back an AnvilChunkStorage writing region/*.mca, with
//     the nether and end in Java's DIM-1 and DIM1 folders.
//   * level.dat is written in Java's shape - gzipped, with DataVersion, a version
//     compound and a WorldGenSettings block - instead of LCE's uncompressed McRegion one.
//   * prepareLevel() reads that back, transparently accepting either form so worlds
//     saved before this change still open.

class AnvilLevelStorage : public DirectoryLevelStorage
{
private:
	// The world folder on disk, taken from the NativeSaveFile backing this level.
	wstring m_worldRoot;

	wstring levelDatPath() const;

	// level.dat is gzipped in Java, unlike every other NBT file LCE writes.
	static CompoundTag *readGzippedNbt(const wstring &path);
	static bool writeGzippedNbt(const wstring &path, CompoundTag *root);

	// Adds the fields modern Java requires that LevelData knows nothing about.
	static void addModernLevelFields(CompoundTag *data, LevelData *levelData);

	void writeLevelDat(CompoundTag *dataTag);

	// <root>/playerdata/<uuid>.dat, the location and naming Java expects.
	wstring playerDataPath(PlayerUID xuid) const;

	// LCE identifies a player by a 64-bit XUID; Java wants a 128-bit UUID and takes it
	// from the file name. The mapping is deterministic so a player keeps their file.
	static wstring uuidStringFor(PlayerUID xuid);

	// Rewrites LCE player NBT into Java's shape: namespaced item ids and dimension.
	static void convertPlayerTag(CompoundTag *tag);

public:
	static const int ANVIL_VERSION_ID = 19133;	// as opposed to McRegion's 19132

	AnvilLevelStorage(ConsoleSaveFile *saveFile, File dir, const wstring &levelName, bool createPlayerDir, bool ownsSaveFile = true);
	~AnvilLevelStorage();

	virtual ChunkStorage *createChunkStorage(Dimension *dimension);
	virtual LevelData *prepareLevel();
	virtual void saveLevelData(LevelData *levelData, vector<shared_ptr<Player> > *players);
	virtual void saveLevelData(LevelData *levelData);

	virtual void save(shared_ptr<Player> player);
	virtual bool load(shared_ptr<Player> player);
	virtual CompoundTag *loadPlayerDataTag(PlayerUID xuid);
};

#endif // _WINDOWS64
