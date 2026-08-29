#pragma once
using namespace std;

#include "stdafx.h"
#include "ChunkStorage.h"

class AnvilRegionFile;
class CompoundTag;
class LevelChunk;
class Level;

// 4J Meow - Serialises LCE chunks into the modern Java "Anvil" chunk schema
// (SerializableChunkData in 26.1.2) and back.
//
// Height mapping: LCE worlds are 256 blocks tall with bedrock at y=0. Java's overworld
// spans y=-64..319. LCE y is written unchanged as Java y, so terrain and bedrock land
// exactly where a Java player expects them; sections -4..-1 and 16..19 are simply absent
// from the section list and Java fills them with air on load.

#define ANVIL_DATA_VERSION 4790		// SharedConstants.WORLD_VERSION for 26.1.2
#define ANVIL_MIN_SECTION_Y (-4)	// overworld min_y (-64) / 16
#define ANVIL_LCE_SECTION_COUNT 16	// LCE's 256 blocks / 16

// Java switches a paletted container over to the *global* palette once the entry count
// needs more than 8 bits for blocks or 3 for biomes (Strategy.getConfigurationForBitCount).
// In that mode the packed indices are global registry ids rather than offsets into the
// palette written alongside them, which is not something this code can produce. Both
// containers are therefore capped just below that threshold.
#define ANVIL_MAX_BLOCK_PALETTE 256
#define ANVIL_MAX_BIOME_PALETTE 8

class AnvilChunkStorage : public ChunkStorage
{
private:
	wstring m_dimensionDir;

	// Region files are kept open between chunk writes; a save touches the same handful
	// of regions repeatedly, and reopening per chunk would dominate the cost.
	map<__int64, AnvilRegionFile *> m_regions;

	AnvilRegionFile *getRegion(int chunkX, int chunkZ, bool createIfMissing);

	// Entities live in a parallel entities/ region in modern Java, with its own set of
	// region files keyed the same way.
	map<__int64, AnvilRegionFile *> m_entityRegions;
	AnvilRegionFile *getEntityRegion(int chunkX, int chunkZ, bool createIfMissing);

	AnvilRegionFile *openRegion(map<__int64, AnvilRegionFile *> &cache, const wchar_t *folder,
	                            int chunkX, int chunkZ, bool createIfMissing);

public:
	AnvilChunkStorage(const wstring &dimensionDir);
	virtual ~AnvilChunkStorage();

	virtual LevelChunk *load(Level *level, int x, int z);
	virtual void save(Level *level, LevelChunk *levelChunk);
	virtual void saveEntities(Level *level, LevelChunk *levelChunk);
	virtual void tick();
	virtual void flush();

	// Exposed so the level-conversion path can reuse them without a storage instance.
	static CompoundTag *toChunkTag(Level *level, LevelChunk *levelChunk);

	// The entities/ region counterpart: {DataVersion, Position:int[2], Entities:[...]}.
	static CompoundTag *toEntitiesTag(LevelChunk *levelChunk);
	static LevelChunk *fromChunkTag(Level *level, CompoundTag *tag);

	// Legacy block-entity id ("Chest") -> modern namespaced id ("minecraft:chest").
	// Returns NULL when the type has no modern counterpart.
	static const wchar_t *modernBlockEntityId(const wstring &legacyId);

	// LCE biome id -> modern biome name. Always yields a valid name.
	static const wchar_t *biomeName(int biomeId);
};
