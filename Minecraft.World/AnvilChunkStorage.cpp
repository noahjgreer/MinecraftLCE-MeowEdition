#include "stdafx.h"
#include "AnvilChunkStorage.h"
#include "AnvilRegionFile.h"
#include "AnvilBlockMapping.h"
#include "AnvilSavePaths.h"
#include "AnvilItemMapping.h"
#include "AnvilEntityMapping.h"
#include "EntityIO.h"

#include "CompoundTag.h"
#include "ListTag.h"
#include "StringTag.h"
#include "ByteArrayTag.h"
#include "LongArrayTag.h"
#include "DoubleTag.h"
#include "IntArrayTag.h"
#include "NbtIo.h"
#include "InputOutputStream.h"

#include "LevelChunk.h"
#include "Level.h"
#include "TileEntity.h"
#include "Entity.h"
#include "Player.h"

// 4J Meow - see AnvilChunkStorage.h.

// LCE biome ids are the vanilla 1.6 ids (0-22). Biomes removed in later versions are
// folded onto their closest surviving equivalent.
const wchar_t *AnvilChunkStorage::biomeName(int biomeId)
{
	switch (biomeId)
	{
	case 0:  return L"minecraft:ocean";
	case 1:  return L"minecraft:plains";
	case 2:  return L"minecraft:desert";
	case 3:  return L"minecraft:windswept_hills";		// was extreme_hills
	case 4:  return L"minecraft:forest";
	case 5:  return L"minecraft:taiga";
	case 6:  return L"minecraft:swamp";
	case 7:  return L"minecraft:river";
	case 8:  return L"minecraft:nether_wastes";			// was hell
	case 9:  return L"minecraft:the_end";				// was sky
	case 10: return L"minecraft:frozen_ocean";
	case 11: return L"minecraft:frozen_river";
	case 12: return L"minecraft:snowy_plains";			// was ice_plains
	case 13: return L"minecraft:snowy_plains";			// ice_mountains has no modern form
	case 14: return L"minecraft:mushroom_fields";
	case 15: return L"minecraft:mushroom_fields";		// shore variant was removed
	case 16: return L"minecraft:beach";
	case 17: return L"minecraft:desert";				// desert_hills was removed
	case 18: return L"minecraft:forest";				// forest_hills was removed
	case 19: return L"minecraft:taiga";					// taiga_hills was removed
	case 20: return L"minecraft:windswept_hills";		// extreme_hills_edge was removed
	case 21: return L"minecraft:jungle";
	case 22: return L"minecraft:jungle";				// jungle_hills was removed
	default: return L"minecraft:plains";
	}
}

const wchar_t *AnvilChunkStorage::modernBlockEntityId(const wstring &legacyId)
{
	if (legacyId == L"Chest")        return L"minecraft:chest";
	if (legacyId == L"Furnace")      return L"minecraft:furnace";
	if (legacyId == L"Sign")         return L"minecraft:sign";
	if (legacyId == L"MobSpawner")   return L"minecraft:mob_spawner";
	if (legacyId == L"Music")        return L"minecraft:note_block";
	if (legacyId == L"Trap")         return L"minecraft:dispenser";
	if (legacyId == L"Dropper")      return L"minecraft:dropper";
	if (legacyId == L"Piston")       return L"minecraft:piston";
	if (legacyId == L"Cauldron")     return L"minecraft:brewing_stand";
	if (legacyId == L"EnchantTable") return L"minecraft:enchanting_table";
	if (legacyId == L"Airportal")    return L"minecraft:end_portal";
	if (legacyId == L"Control")      return L"minecraft:command_block";
	if (legacyId == L"Beacon")       return L"minecraft:beacon";
	if (legacyId == L"Skull")        return L"minecraft:skull";
	if (legacyId == L"DLDetector")   return L"minecraft:daylight_detector";
	if (legacyId == L"Hopper")       return L"minecraft:hopper";
	if (legacyId == L"Comparator")   return L"minecraft:comparator";
	if (legacyId == L"RecordPlayer") return L"minecraft:jukebox";
	if (legacyId == L"EnderChest")   return L"minecraft:ender_chest";
	if (legacyId == L"FlowerPot")    return L"minecraft:flower_pot";
	return NULL;
}

// Derived from modernBlockEntityId() rather than written out again, so the two cannot
// disagree about what a Chest is called.
const wchar_t *AnvilChunkStorage::legacyBlockEntityId(const wstring &modernId)
{
	static const wchar_t *legacy[] =
	{
		L"Chest", L"Furnace", L"Sign", L"MobSpawner", L"Music", L"Trap", L"Dropper",
		L"Piston", L"Cauldron", L"EnchantTable", L"Airportal", L"Control", L"Beacon",
		L"Skull", L"DLDetector", L"Hopper", L"Comparator", L"RecordPlayer", L"EnderChest",
		L"FlowerPot"
	};

	for (int i = 0; i < (int)(sizeof(legacy) / sizeof(legacy[0])); i++)
	{
		const wchar_t *modern = modernBlockEntityId(legacy[i]);
		if (modern != NULL && modernId == modern) return legacy[i];
	}

	return NULL;
}

// ---------------------------------------------------------------------------
// Palette packing
// ---------------------------------------------------------------------------

// Java stores a section's 4096 block indices in a long[] using the "no straddling"
// layout introduced in 1.16: each long holds floor(64 / bits) whole indices and any
// spare high bits are left zero.
static longArray packIndices(const int *indices, int count, int bits)
{
	const int perLong = 64 / bits;
	const int longs = (count + perLong - 1) / perLong;

	longArray out(longs);
	for (int i = 0; i < longs; i++) out[i] = 0;

	for (int i = 0; i < count; i++)
	{
		const int slot = i / perLong;
		const int shift = (i % perLong) * bits;
		out[slot] |= ((__int64)(indices[i] & ((1 << bits) - 1))) << shift;
	}

	return out;
}

// Java derives the bit width from the palette size alone (Strategy.
// getConfigurationForPaletteSize -> Mth.ceillog2), not from the length of the data
// array, so this has to agree with it exactly or the chunk is rejected.
static int bitsForPalette(int paletteSize, int minimum)
{
	int bits = 1;
	while ((1 << bits) < paletteSize) bits++;
	return bits < minimum ? minimum : bits;
}

// Builds the {palette, data} compound Java expects for a paletted container. When the
// palette holds a single entry the data array is omitted entirely, which is how Java
// encodes a uniform section.
static CompoundTag *buildPalettedContainer(const vector<AnvilBlockState> &palette,
                                           const int *indices, int count, int minBits)
{
	CompoundTag *container = new CompoundTag();

	ListTag<CompoundTag> *paletteTag = new ListTag<CompoundTag>();
	for (unsigned int i = 0; i < palette.size(); i++)
	{
		CompoundTag *entry = new CompoundTag();
		entry->putString(L"Name", palette[i].name);

		if (palette[i].propCount > 0)
		{
			CompoundTag *props = new CompoundTag();
			for (int p = 0; p < palette[i].propCount; p++)
			{
				props->putString((wchar_t *)palette[i].propKeys[p], palette[i].propVals[p]);
			}
			entry->put(L"Properties", props);
		}

		paletteTag->add(entry);
	}
	container->put(L"palette", paletteTag);

	if (palette.size() > 1)
	{
		container->putLongArray(L"data", packIndices(indices, count, bitsForPalette((int)palette.size(), minBits)));
	}

	return container;
}

// Same shape, but for biomes, whose palette entries are bare strings rather than
// compounds and whose minimum bit width is 1.
static CompoundTag *buildBiomeContainer(const vector<wstring> &palette, const int *indices, int count)
{
	CompoundTag *container = new CompoundTag();

	ListTag<StringTag> *paletteTag = new ListTag<StringTag>();
	for (unsigned int i = 0; i < palette.size(); i++)
	{
		paletteTag->add(new StringTag(L"", palette[i]));
	}
	container->put(L"palette", paletteTag);

	if (palette.size() > 1)
	{
		container->putLongArray(L"data", packIndices(indices, count, bitsForPalette((int)palette.size(), 1)));
	}

	return container;
}

// ---------------------------------------------------------------------------
// Chunk serialisation
// ---------------------------------------------------------------------------

CompoundTag *AnvilChunkStorage::toChunkTag(Level *level, LevelChunk *levelChunk)
{
	CompoundTag *tag = new CompoundTag();

	tag->putInt(L"DataVersion", ANVIL_DATA_VERSION);
	tag->putInt(L"xPos", levelChunk->x);
	tag->putInt(L"yPos", ANVIL_MIN_SECTION_Y);
	tag->putInt(L"zPos", levelChunk->z);
	tag->putLong(L"LastUpdate", level != NULL ? level->getTime() : 0);
	tag->putLong(L"InhabitedTime", 0);
	tag->putString(L"Status", L"minecraft:full");

	// Deliberately no "isLightOn": LCE's light arrays cover a different vertical range,
	// so it is cheaper and safer to let Java relight the chunk on first load.

	ListTag<CompoundTag> *sections = new ListTag<CompoundTag>();

	int blockIndices[4096];
	int biomeIndices[64];

	for (int sectionY = 0; sectionY < ANVIL_LCE_SECTION_COUNT; sectionY++)
	{
		vector<AnvilBlockState> palette;
		map<wstring, int> paletteLookup;

		bool anyNonAir = false;
		bool overflowed = false;
		int i = 0;

		for (int y = 0; y < 16; y++)
		{
			const int worldY = sectionY * 16 + y;

			for (int z = 0; z < 16; z++)
			{
				for (int x = 0; x < 16; x++)
				{
					const int tileId = levelChunk->getTile(x, worldY, z);
					const int meta = levelChunk->getData(x, worldY, z);

					AnvilBlockState state;
					AnvilBlockMapping::toBlockState(tileId, meta, state);
					if (tileId != 0) anyNonAir = true;

					const wstring key = state.key();
					AUTO_VAR(found, paletteLookup.find(key));

					int index;
					if (found != paletteLookup.end()) index = found->second;
					else if ((int)palette.size() < ANVIL_MAX_BLOCK_PALETTE)
					{
						index = (int)palette.size();
						palette.push_back(state);
						paletteLookup[key] = index;
					}
					else
					{
						// Past the cap the container would have to use Java's global
						// palette, which cannot be written here. Fall back to the
						// section's first state rather than emit a chunk Java rejects.
						// 4096 blocks drawn from LCE's 2336 possible states makes this
						// essentially unreachable outside a deliberately built section.
						index = 0;
						overflowed = true;
					}

					// Java indexes a section as y*256 + z*16 + x.
					blockIndices[i++] = index;
				}
			}
		}

		// An entirely empty section is left out of the list; Java treats absent
		// sections as air, so writing them would only inflate the region file.
		if (!anyNonAir) continue;

		if (overflowed)
		{
			app.DebugPrintf("Anvil: chunk (%d,%d) section %d exceeded %d palette entries; "
			                "surplus states written as the section's first state\n",
			                levelChunk->x, levelChunk->z, sectionY, ANVIL_MAX_BLOCK_PALETTE);
		}

		CompoundTag *section = new CompoundTag();
		section->putByte(L"Y", (byte)sectionY);
		section->put(L"block_states", buildPalettedContainer(palette, blockIndices, 4096, 4));

		// LCE biomes are per-column; Java wants a 4x4x4 grid per section, so each cell
		// takes the biome of the column at its centre.
		{
			vector<wstring> biomePalette;
			map<wstring, int> biomeLookup;
			int b = 0;

			for (int by = 0; by < 4; by++)
			for (int bz = 0; bz < 4; bz++)
			for (int bx = 0; bx < 4; bx++)
			{
				const int columnX = bx * 4 + 2;
				const int columnZ = bz * 4 + 2;

				int biomeId = 1;	// plains if the chunk has no biome array yet
				if (levelChunk->biomes.data != NULL && levelChunk->biomes.length >= 256)
				{
					biomeId = (int)(unsigned char)levelChunk->biomes[columnZ * 16 + columnX];
				}

				const wstring name = biomeName(biomeId);
				AUTO_VAR(found, biomeLookup.find(name));

				int index;
				if (found != biomeLookup.end()) index = found->second;
				else if ((int)biomePalette.size() < ANVIL_MAX_BIOME_PALETTE)
				{
					index = (int)biomePalette.size();
					biomePalette.push_back(name);
					biomeLookup[name] = index;
				}
				else
				{
					// Same global-palette threshold as above, but biomes cross it at 8.
					// A 4x4x4 cell grid samples 16 columns, so a section straddling many
					// biomes can reach this; the extras take the first biome present.
					index = 0;
				}

				biomeIndices[b++] = index;
			}

			section->put(L"biomes", buildBiomeContainer(biomePalette, biomeIndices, 64));
		}

		sections->add(section);
	}

	tag->put(L"sections", sections);

	// Block entities keep their position but take the modern namespaced id. Their
	// payloads still carry LCE's numeric item ids - see the note in docs/.
	ListTag<CompoundTag> *blockEntities = new ListTag<CompoundTag>();

	AUTO_VAR(itEnd, levelChunk->tileEntities.end());
	for (AUTO_VAR(it, levelChunk->tileEntities.begin()); it != itEnd; ++it)
	{
		shared_ptr<TileEntity> te = it->second;
		if (te == nullptr) continue;

		CompoundTag *teTag = new CompoundTag();
		te->save(teTag);

		const wstring legacyId = teTag->getString(L"id");
		const wchar_t *modernId = modernBlockEntityId(legacyId);
		if (modernId == NULL)
		{
			delete teTag;
			continue;
		}

		teTag->putString(L"id", modernId);
		teTag->putBoolean(L"keepPacked", false);

		// The stacks inside still carry LCE's numeric item ids until this runs.
		AnvilItemMapping::convertItemList(teTag, L"Items");

		blockEntities->add(teTag);
	}
	tag->put(L"block_entities", blockEntities);

	// Java 1.17+ keeps entities in a parallel entities/ region rather than in the chunk;
	// saveEntities() writes them there.
	tag->put(L"block_ticks", new ListTag<CompoundTag>());
	tag->put(L"fluid_ticks", new ListTag<CompoundTag>());
	tag->put(L"structures", new CompoundTag(L"structures"));

	return tag;
}

// ---------------------------------------------------------------------------
// Deserialisation
// ---------------------------------------------------------------------------

LevelChunk *AnvilChunkStorage::fromChunkTag(Level *level, CompoundTag *tag)
{
	if (tag == NULL) return NULL;

	const int chunkX = tag->getInt(L"xPos");
	const int chunkZ = tag->getInt(L"zPos");

	LevelChunk *chunk = new LevelChunk(level, chunkX, chunkZ);

	// A freshly constructed LevelChunk has lowerData, lowerSkyLight and lowerBlockLight
	// set to NULL - they are only allocated by the bulk setters below. Writing through
	// setTileAndData() before that point dereferences null, which is what crashed the
	// first version of this loader.
	//
	// So the section palettes are decoded into flat arrays and handed over in one go,
	// the same way OldChunkStorage::load does it.
	//
	// LCE's linear index is "xxxxzzzzyyyyyyy" (CompressedTileStorage::getIndex), i.e.
	// x * 2048 + z * 128 + y within each 128-block half, with the upper half following
	// the lower at COMPRESSED_CHUNK_SECTION_TILES.
	byteArray blocks(Level::CHUNK_TILE_COUNT);
	byteArray data(Level::CHUNK_TILE_COUNT / 2);

	memset(blocks.data, 0, Level::CHUNK_TILE_COUNT);
	memset(data.data, 0, Level::CHUNK_TILE_COUNT / 2);

	ListTag<CompoundTag> *sections = (ListTag<CompoundTag> *)tag->getList(L"sections");
	if (sections != NULL)
	{
		for (int s = 0; s < sections->size(); s++)
		{
			CompoundTag *section = sections->get(s);
			if (section == NULL) continue;

			const int sectionY = (int)(char)section->getByte(L"Y");

			// Only the range LCE can physically represent is read back; anything Java
			// wrote below y=0 or above y=255 has nowhere to go in a 256-tall chunk.
			if (sectionY < 0 || sectionY >= ANVIL_LCE_SECTION_COUNT) continue;
			if (!section->contains(L"block_states")) continue;

			CompoundTag *states = section->getCompound(L"block_states");
			ListTag<CompoundTag> *palette = (ListTag<CompoundTag> *)states->getList(L"palette");
			if (palette == NULL || palette->size() == 0) continue;

			const int paletteSize = palette->size();
			int *paletteIds = new int[paletteSize];
			int *paletteMeta = new int[paletteSize];

			for (int p = 0; p < paletteSize; p++)
			{
				CompoundTag *entry = palette->get(p);
				const wstring name = entry != NULL ? entry->getString(L"Name") : wstring(L"minecraft:air");

				// The properties carry everything LCE keeps in its metadata - stair and
				// torch facing, slab half, log axis, door hinge. Matching on the name
				// alone silently flattened all of it.
				vector<AnvilBlockProperty> properties;

				if (entry != NULL && entry->contains(L"Properties"))
				{
					CompoundTag *props = entry->getCompound(L"Properties");
					vector<Tag *> *all = props->getAllTags();

					if (all != NULL)
					{
						for (unsigned int t = 0; t < all->size(); t++)
						{
							Tag *tag = (*all)[t];
							if (tag == NULL || tag->getId() != Tag::TAG_String) continue;

							AnvilBlockProperty property;
							property.key = tag->getName();
							property.value = ((StringTag *)tag)->data;
							properties.push_back(property);
						}
						delete all;
					}
				}

				int id = 0, meta = 0;
				if (!AnvilBlockMapping::fromBlockState(name, properties, id, meta))
				{
					// No LCE equivalent - leave the space empty rather than guessing.
					id = 0;
					meta = 0;
				}

				paletteIds[p] = id;
				paletteMeta[p] = meta;
			}

			// A single-entry palette carries no data array; every cell is that entry.
			longArray packed;
			int bits = 0;
			int perLong = 0;
			__int64 mask = 0;

			if (paletteSize > 1)
			{
				if (!states->contains(L"data"))
				{
					delete[] paletteIds;
					delete[] paletteMeta;
					continue;
				}

				packed = states->getLongArray(L"data");

				bits = 1;
				while ((1 << bits) < paletteSize) bits++;
				if (bits < 4) bits = 4;

				perLong = 64 / bits;
				mask = (1LL << bits) - 1;
			}

			for (int i = 0; i < 4096; i++)
			{
				int index = 0;

				if (paletteSize > 1)
				{
					const int slot = i / perLong;
					if (slot >= (int)packed.length) break;

					index = (int)((packed[slot] >> ((i % perLong) * bits)) & mask);
					if (index >= paletteSize) continue;
				}

				const int id = paletteIds[index];
				if (id == 0) continue;

				// Java indexes a section as y * 256 + z * 16 + x.
				const int x = i & 15;
				const int z = (i >> 4) & 15;
				const int y = sectionY * 16 + (i >> 8);

				const int half = (y >= Level::COMPRESSED_CHUNK_SECTION_HEIGHT) ? Level::COMPRESSED_CHUNK_SECTION_TILES : 0;
				const int linear = half + (x << 11) + (z << 7) + (y & (Level::COMPRESSED_CHUNK_SECTION_HEIGHT - 1));

				blocks[linear] = (byte)id;

				const int meta = paletteMeta[index] & 15;
				if (meta != 0)
				{
					// The metadata array is nibble packed, low nibble first.
					if ((linear & 1) != 0) data[linear >> 1] |= (byte)(meta << 4);
					else                   data[linear >> 1] |= (byte)meta;
				}
			}

			delete[] paletteIds;
			delete[] paletteMeta;
		}
	}

	chunk->setBlockData(blocks);
	chunk->setDataData(data);

	delete[] blocks.data;
	delete[] data.data;

	// Lighting is not stored - Java recomputes it and so does LCE. The arrays still have
	// to be allocated, though, because the rest of the chunk code assumes they exist.
	byteArray light(Level::CHUNK_TILE_COUNT / 2);
	memset(light.data, 0, Level::CHUNK_TILE_COUNT / 2);

	chunk->setSkyLightData(light);
	chunk->setBlockLightData(light);
	delete[] light.data;

	chunk->setSkyLightDataAllBright();
	chunk->recalcHeightmap();

	// Without this the generator would treat the chunk as new and populate it a second
	// time, dropping a fresh set of ores and structures into an already-built world.
	chunk->terrainPopulated = LevelChunk::sTerrainPopulatedAllNeighbours | LevelChunk::sTerrainPostPostProcessed;

	// Biomes are deliberately left at their unset marker so the biome source recomputes
	// them, exactly as OldChunkStorage does when a chunk has no Biomes array.

	// Block entities. Without these a chest is still a chest block but has no container
	// behind it, so it opens empty and its contents are gone.
	ListTag<CompoundTag> *blockEntities = (ListTag<CompoundTag> *)tag->getList(L"block_entities");
	if (blockEntities != NULL)
	{
		for (int i = 0; i < blockEntities->size(); i++)
		{
			CompoundTag *teTag = blockEntities->get(i);
			if (teTag == NULL || !teTag->contains(L"id")) continue;

			const wchar_t *legacyId = legacyBlockEntityId(teTag->getString(L"id"));
			if (legacyId == NULL) continue;

			// TileEntity::loadStatic matches on the legacy id, and the stacks inside are
			// still in Java's form until convertItemListFromJava runs.
			teTag->putString(L"id", legacyId);
			AnvilItemMapping::convertItemListFromJava(teTag, L"Items");

			shared_ptr<TileEntity> te = TileEntity::loadStatic(teTag);
			if (te != nullptr) chunk->addTileEntity(te);
		}
	}

	chunk->loaded = true;
	return chunk;
}

// ---------------------------------------------------------------------------
// ChunkStorage
// ---------------------------------------------------------------------------

AnvilChunkStorage::AnvilChunkStorage(const wstring &dimensionDir)
{
	m_dimensionDir = dimensionDir;
}

AnvilChunkStorage::~AnvilChunkStorage()
{
	for (AUTO_VAR(it, m_regions.begin()); it != m_regions.end(); ++it) delete it->second;
	m_regions.clear();

	for (AUTO_VAR(it, m_entityRegions.begin()); it != m_entityRegions.end(); ++it) delete it->second;
	m_entityRegions.clear();
}

// Creates a directory and any missing parents. The world root may not exist yet either,
// so this cannot assume anything above it is already there.
static void createDirectoryTree(const wstring &path)
{
	for (unsigned int i = 0; i < path.length(); i++)
	{
		if (path[i] != L'\\' || i == 0) continue;

		const wstring partial = path.substr(0, i);
		if (partial.length() == 2 && partial[1] == L':') continue;

		CreateDirectoryW(partial.c_str(), NULL);
	}

	CreateDirectoryW(path.c_str(), NULL);
}

AnvilRegionFile *AnvilChunkStorage::openRegion(map<__int64, AnvilRegionFile *> &cache, const wchar_t *folder,
                                              int chunkX, int chunkZ, bool createIfMissing)
{
	const int regionX = chunkX >> 5;
	const int regionZ = chunkZ >> 5;
	const __int64 key = ((__int64)regionX << 32) ^ (unsigned int)regionZ;

	AUTO_VAR(it, cache.find(key));
	if (it != cache.end()) return it->second;

	wchar_t name[64];
	swprintf(name, 64, L"r.%d.%d.mca", regionX, regionZ);

	const wstring directory = m_dimensionDir + L"\\" + folder;
	const wstring path = directory + L"\\" + name;

	if (!createIfMissing)
	{
		FILE *probe = _wfopen(path.c_str(), L"rb");
		if (probe == NULL) return NULL;
		fclose(probe);
	}

	// AnvilRegionFile opens the file directly, so the dimension and region folders have
	// to exist first or every _wfopen fails and nothing is ever written.
	createDirectoryTree(m_dimensionDir);
	createDirectoryTree(directory);

	AnvilRegionFile *region = new AnvilRegionFile(path);
	if (!region->isOpen())
	{
		app.DebugPrintf("Anvil: could not open region file %ls\n", path.c_str());
		delete region;
		return NULL;
	}

	cache[key] = region;
	return region;
}

AnvilRegionFile *AnvilChunkStorage::getRegion(int chunkX, int chunkZ, bool createIfMissing)
{
	return openRegion(m_regions, L"region", chunkX, chunkZ, createIfMissing);
}

AnvilRegionFile *AnvilChunkStorage::getEntityRegion(int chunkX, int chunkZ, bool createIfMissing)
{
	return openRegion(m_entityRegions, L"entities", chunkX, chunkZ, createIfMissing);
}

// Entities live in their own region file, so they are read separately from the chunk and
// attached afterwards.
void AnvilChunkStorage::loadEntities(Level *level, LevelChunk *chunk, int x, int z)
{
	if (chunk == NULL) return;

	AnvilRegionFile *region = getEntityRegion(x, z, false);
	if (region == NULL) return;

	byteArray raw = region->readChunk(x, z);
	if (raw.data == NULL || raw.length == 0) return;

	CompoundTag *tag = NbtIo::decompress(raw);
	if (tag == NULL) return;

	ListTag<CompoundTag> *entities = (ListTag<CompoundTag> *)tag->getList(L"Entities");
	if (entities != NULL)
	{
		for (int i = 0; i < entities->size(); i++)
		{
			CompoundTag *entityTag = entities->get(i);
			if (entityTag == NULL) continue;

			if (!AnvilEntityMapping::convertEntityFromJava(entityTag)) continue;

			shared_ptr<Entity> entity = EntityIO::loadStatic(entityTag, level);
			if (entity != nullptr)
			{
				chunk->lastSaveHadEntities = true;
				chunk->addEntity(entity);
			}
		}
	}

	delete tag;
}

LevelChunk *AnvilChunkStorage::load(Level *level, int x, int z)
{
	static int s_logged = 0;
	const bool trace = s_logged < 3;
	if (trace) s_logged++;

	AnvilRegionFile *region = getRegion(x, z, false);
	if (region == NULL)
	{
		if (trace) AnvilSavePaths::log("[anvil] load chunk (%d,%d): no region file under \"%ls\"\n",
		                               x, z, m_dimensionDir.c_str());
		return NULL;
	}

	byteArray raw = region->readChunk(x, z);
	if (raw.data == NULL || raw.length == 0) return NULL;

	CompoundTag *tag = NbtIo::decompress(raw);
	if (tag == NULL) return NULL;

	LevelChunk *chunk = fromChunkTag(level, tag);
	delete tag;

	if (chunk != NULL) loadEntities(level, chunk, x, z);

	if (trace) AnvilSavePaths::log("[anvil] loaded chunk (%d,%d) bytes=%u chunk=%s\n",
	                               x, z, raw.length, chunk != NULL ? "ok" : "NULL");

	return chunk;
}

void AnvilChunkStorage::save(Level *level, LevelChunk *levelChunk)
{
	if (levelChunk == NULL) return;

	static int s_logged = 0;
	const bool trace = s_logged < 3;
	if (trace) s_logged++;

	AnvilRegionFile *region = getRegion(levelChunk->x, levelChunk->z, true);
	if (region == NULL)
	{
		if (trace) AnvilSavePaths::log("[anvil] save chunk (%d,%d): no region file under \"%ls\"\n",
		                               levelChunk->x, levelChunk->z, m_dimensionDir.c_str());
		return;
	}

	CompoundTag *tag = toChunkTag(level, levelChunk);

	byteArray encoded = NbtIo::compress(tag);
	bool ok = false;
	if (encoded.data != NULL && encoded.length > 0)
	{
		ok = region->writeChunk(levelChunk->x, levelChunk->z, encoded.data, encoded.length);
	}

	if (trace) AnvilSavePaths::log("[anvil] saved chunk (%d,%d) bytes=%u ok=%d dir=\"%ls\"\n",
	                               levelChunk->x, levelChunk->z, encoded.length, ok ? 1 : 0,
	                               m_dimensionDir.c_str());

	delete tag;
}

CompoundTag *AnvilChunkStorage::toEntitiesTag(LevelChunk *levelChunk)
{
	CompoundTag *tag = new CompoundTag();

	tag->putInt(L"DataVersion", ANVIL_DATA_VERSION);

	intArray position(2);
	position[0] = levelChunk->x;
	position[1] = levelChunk->z;
	tag->putIntArray(L"Position", position);

	ListTag<CompoundTag> *entities = new ListTag<CompoundTag>();

	// LCE buckets a chunk's entities by vertical slice; Java wants one flat list.
	if (levelChunk->entityBlocks != NULL)
	{
		for (int slice = 0; slice < levelChunk->ENTITY_BLOCKS_LENGTH; slice++)
		{
			vector<shared_ptr<Entity> > *bucket = levelChunk->entityBlocks[slice];
			if (bucket == NULL) continue;

			for (AUTO_VAR(it, bucket->begin()); it != bucket->end(); ++it)
			{
				shared_ptr<Entity> entity = *it;
				if (entity == nullptr) continue;

				// Players are stored in playerdata/, never in the chunk.
				if (dynamic_cast<Player *>(entity.get()) != NULL) continue;

				CompoundTag *entityTag = new CompoundTag();

				// Entity::save() returns false for entities that decline to persist.
				if (!entity->save(entityTag) || !AnvilEntityMapping::convertEntity(entityTag))
				{
					delete entityTag;
					continue;
				}

				entities->add(entityTag);
			}
		}
	}

	tag->put(L"Entities", entities);
	return tag;
}

void AnvilChunkStorage::saveEntities(Level *level, LevelChunk *levelChunk)
{
	if (levelChunk == NULL) return;

	AnvilRegionFile *region = getEntityRegion(levelChunk->x, levelChunk->z, true);
	if (region == NULL) return;

	CompoundTag *tag = toEntitiesTag(levelChunk);

	byteArray encoded = NbtIo::compress(tag);
	if (encoded.data != NULL && encoded.length > 0)
	{
		region->writeChunk(levelChunk->x, levelChunk->z, encoded.data, encoded.length);
	}

	delete tag;
}

void AnvilChunkStorage::tick()
{
}

void AnvilChunkStorage::flush()
{
	for (AUTO_VAR(it, m_regions.begin()); it != m_regions.end(); ++it) it->second->flush();
	for (AUTO_VAR(it, m_entityRegions.begin()); it != m_entityRegions.end(); ++it) it->second->flush();
}
