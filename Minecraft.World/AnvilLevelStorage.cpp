#include "stdafx.h"

#ifdef _WINDOWS64

#include "AnvilLevelStorage.h"
#include "AnvilChunkStorage.h"
#include "AnvilItemMapping.h"
#include "NativeSaveFile.h"
#include "AnvilSavePaths.h"

#include "net.minecraft.world.level.dimension.h"
#include "CompoundTag.h"
#include "ListTag.h"
#include "StringTag.h"
#include "NbtIo.h"
#include "LevelData.h"
#include "Player.h"
#include "File.h"

#include "..\Minecraft.Client\Common\zlib\zlib.h"

// 4J Meow - see AnvilLevelStorage.h.

#define ANVIL_VERSION_NAME L"26.1.2"

AnvilLevelStorage::AnvilLevelStorage(ConsoleSaveFile *saveFile, File dir, const wstring &levelName, bool createPlayerDir, bool ownsSaveFile)
	: DirectoryLevelStorage(saveFile, dir, levelName, createPlayerDir, ownsSaveFile)
{
	NativeSaveFile *native = dynamic_cast<NativeSaveFile *>(saveFile);
	m_worldRoot = native != NULL ? native->getRoot() : L".";
}

AnvilLevelStorage::~AnvilLevelStorage()
{
}

wstring AnvilLevelStorage::levelDatPath() const
{
	return m_worldRoot + L"\\level.dat";
}

ChunkStorage *AnvilLevelStorage::createChunkStorage(Dimension *dimension)
{
	// Java keeps the nether and the end in DIM-1 and DIM1 beside the overworld's region
	// folder, each with its own region/ inside.
	if (dynamic_cast<HellDimension *>(dimension) != NULL)
	{
		return new AnvilChunkStorage(m_worldRoot + L"\\DIM-1");
	}

	if (dynamic_cast<TheEndDimension *>(dimension) != NULL)
	{
		return new AnvilChunkStorage(m_worldRoot + L"\\DIM1");
	}

	return new AnvilChunkStorage(m_worldRoot);
}

// ---------------------------------------------------------------------------
// level.dat
// ---------------------------------------------------------------------------

CompoundTag *AnvilLevelStorage::readGzippedNbt(const wstring &path)
{
	FILE *file = _wfopen(path.c_str(), L"rb");
	if (file == NULL) return NULL;

	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (size <= 0)
	{
		fclose(file);
		return NULL;
	}

	byte *raw = new byte[size];
	const size_t got = fread(raw, 1, size, file);
	fclose(file);

	if (got != (size_t)size)
	{
		delete[] raw;
		return NULL;
	}

	byteArray plain;

	// A gzip stream starts 1f 8b. Anything else is treated as the uncompressed NBT
	// that LCE used to write, so worlds saved before this change still load.
	if (size >= 2 && raw[0] == 0x1f && raw[1] == 0x8b)
	{
		z_stream zs;
		memset(&zs, 0, sizeof(zs));

		if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
		{
			delete[] raw;
			return NULL;
		}

		zs.next_in = (Bytef *)raw;
		zs.avail_in = (uInt)size;

		unsigned int capacity = (unsigned int)size * 8 + 4096;
		byte *out = new byte[capacity];
		unsigned int produced = 0;
		int status = Z_OK;

		for (;;)
		{
			zs.next_out = (Bytef *)(out + produced);
			zs.avail_out = capacity - produced;

			status = inflate(&zs, Z_NO_FLUSH);
			produced = capacity - zs.avail_out;

			if (status == Z_STREAM_END) break;
			if (status != Z_OK && !(status == Z_BUF_ERROR && zs.avail_out == 0)) break;

			if (zs.avail_out == 0)
			{
				const unsigned int grown = capacity * 2;
				byte *bigger = new byte[grown];
				memcpy(bigger, out, produced);
				delete[] out;
				out = bigger;
				capacity = grown;
			}
			else if (zs.avail_in == 0) break;
		}

		inflateEnd(&zs);
		delete[] raw;

		if (status != Z_STREAM_END)
		{
			delete[] out;
			return NULL;
		}

		plain = byteArray(produced);
		memcpy(plain.data, out, produced);
		delete[] out;
	}
	else
	{
		plain = byteArray(size);
		memcpy(plain.data, raw, size);
		delete[] raw;
	}

	return NbtIo::decompress(plain);
}

bool AnvilLevelStorage::writeGzippedNbt(const wstring &path, CompoundTag *root)
{
	byteArray plain = NbtIo::compress(root);	// serialises; despite the name it does not compress
	if (plain.data == NULL || plain.length == 0) return false;

	z_stream zs;
	memset(&zs, 0, sizeof(zs));

	// 16 + MAX_WBITS selects a gzip wrapper rather than zlib's own.
	if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
	{
		return false;
	}

	uLong bound = deflateBound(&zs, plain.length);
	byte *out = new byte[bound];

	zs.next_in = (Bytef *)plain.data;
	zs.avail_in = plain.length;
	zs.next_out = (Bytef *)out;
	zs.avail_out = (uInt)bound;

	const int status = deflate(&zs, Z_FINISH);
	const unsigned int produced = (unsigned int)(bound - zs.avail_out);
	deflateEnd(&zs);

	if (status != Z_STREAM_END)
	{
		delete[] out;
		return false;
	}

	// Write to a sibling and rename over the target so an interrupted save cannot
	// leave a half-written level.dat where a good one used to be.
	const wstring temporary = path + L".tmp";

	FILE *file = _wfopen(temporary.c_str(), L"wb");
	if (file == NULL)
	{
		delete[] out;
		return false;
	}

	const size_t put = fwrite(out, 1, produced, file);
	fclose(file);
	delete[] out;

	if (put != produced)
	{
		_wremove(temporary.c_str());
		return false;
	}

	_wremove(path.c_str());
	return _wrename(temporary.c_str(), path.c_str()) == 0;
}

// Builds one entry of WorldGenSettings.dimensions.
static CompoundTag *makeDimension(const wchar_t *type, const wchar_t *noiseSettings,
                                  const wchar_t *biomeSourceType, const wchar_t *biomePreset)
{
	CompoundTag *biomeSource = new CompoundTag(L"biome_source");
	biomeSource->putString(L"type", biomeSourceType);
	if (biomePreset != NULL) biomeSource->putString(L"preset", biomePreset);

	CompoundTag *generator = new CompoundTag(L"generator");
	generator->putString(L"type", L"minecraft:noise");
	generator->putString(L"settings", noiseSettings);
	generator->put(L"biome_source", biomeSource);

	CompoundTag *dimension = new CompoundTag();
	dimension->putString(L"type", type);
	dimension->put(L"generator", generator);

	return dimension;
}

void AnvilLevelStorage::addModernLevelFields(CompoundTag *data, LevelData *levelData)
{
	// Java identifies the format generation from these two; without them it refuses the
	// world or runs it through the wrong data fixers.
	data->putInt(L"DataVersion", ANVIL_DATA_VERSION);
	data->putInt(L"version", ANVIL_VERSION_ID);

	CompoundTag *version = new CompoundTag(L"Version");
	version->putInt(L"Id", ANVIL_DATA_VERSION);
	version->putString(L"Name", ANVIL_VERSION_NAME);
	version->putString(L"Series", L"main");
	version->putBoolean(L"Snapshot", false);
	data->put(L"Version", version);

	// LCE has no separate day-time clock; the world clock doubles as one.
	data->putLong(L"DayTime", levelData->getTime());

	data->putByte(L"Difficulty", 2);
	data->putBoolean(L"DifficultyLocked", false);
	data->putFloat(L"SpawnAngle", 0.0f);
	data->putBoolean(L"WasModded", true);

	// WorldGenSettings replaced the loose RandomSeed / generatorName fields. A missing
	// or malformed one fails the load outright, so all three dimensions are declared.
	CompoundTag *dimensions = new CompoundTag(L"dimensions");
	dimensions->put(L"minecraft:overworld",
		makeDimension(L"minecraft:overworld", L"minecraft:overworld", L"minecraft:multi_noise", L"minecraft:overworld"));
	dimensions->put(L"minecraft:the_nether",
		makeDimension(L"minecraft:the_nether", L"minecraft:nether", L"minecraft:multi_noise", L"minecraft:nether"));
	dimensions->put(L"minecraft:the_end",
		makeDimension(L"minecraft:the_end", L"minecraft:end", L"minecraft:the_end", NULL));

	CompoundTag *worldGen = new CompoundTag(L"WorldGenSettings");
	worldGen->putLong(L"seed", levelData->getSeed());
	worldGen->putBoolean(L"generate_features", levelData->isGenerateMapFeatures());
	worldGen->putBoolean(L"bonus_chest", false);
	worldGen->put(L"dimensions", dimensions);
	data->put(L"WorldGenSettings", worldGen);

	// Java expects every game rule as a string; an empty compound means "all defaults".
	data->put(L"GameRules", new CompoundTag(L"GameRules"));

	ListTag<StringTag> *enabled = new ListTag<StringTag>();
	enabled->add(new StringTag(L"", L"vanilla"));

	CompoundTag *dataPacks = new CompoundTag(L"DataPacks");
	dataPacks->put(L"Enabled", enabled);
	dataPacks->put(L"Disabled", new ListTag<StringTag>());
	data->put(L"DataPacks", dataPacks);
}

void AnvilLevelStorage::writeLevelDat(CompoundTag *dataTag)
{
	CompoundTag *root = new CompoundTag();
	root->put(L"Data", dataTag);

	const bool ok = writeGzippedNbt(levelDatPath(), root);

	// Autosave rewrites level.dat constantly, so only report the first write for a given
	// world and any failure - otherwise the log is megabytes of identical lines.
	static wstring s_lastLogged;
	if (!ok || s_lastLogged != levelDatPath())
	{
		s_lastLogged = levelDatPath();
		AnvilSavePaths::log("[anvil] wrote level.dat to \"%ls\" ok=%d\n", levelDatPath().c_str(), ok ? 1 : 0);
	}

	delete root;	// owns dataTag
}

void AnvilLevelStorage::saveLevelData(LevelData *levelData, vector<shared_ptr<Player> > *players)
{
	CompoundTag *dataTag = levelData->createTag(players);
	addModernLevelFields(dataTag, levelData);
	writeLevelDat(dataTag);
}

void AnvilLevelStorage::saveLevelData(LevelData *levelData)
{
	CompoundTag *dataTag = levelData->createTag();
	addModernLevelFields(dataTag, levelData);
	writeLevelDat(dataTag);
}

LevelData *AnvilLevelStorage::prepareLevel()
{
	CompoundTag *root = readGzippedNbt(levelDatPath());

	AnvilSavePaths::log("[anvil] prepareLevel \"%ls\" -> %s\n",
	                    levelDatPath().c_str(), root != NULL ? "loaded" : "no level.dat (new world)");

	if (root == NULL) return NULL;

	if (!root->contains(L"Data"))
	{
		delete root;
		return NULL;
	}

	LevelData *result = new LevelData(root->getCompound(L"Data"));
	delete root;
	return result;
}



// ---------------------------------------------------------------------------
// Player data
// ---------------------------------------------------------------------------

wstring AnvilLevelStorage::uuidStringFor(PlayerUID xuid)
{
	// Spread the 64-bit id over 128 bits with a pair of mixes, then format it the way
	// Java names its player files. Version and variant nibbles are set so the result is
	// a well-formed type-4 UUID rather than something Java's parser rejects.
	unsigned __int64 high = (unsigned __int64)xuid;
	unsigned __int64 low  = (unsigned __int64)xuid;

	high ^= high >> 33; high *= 0xff51afd7ed558ccdULL; high ^= high >> 33;
	low  ^= low  >> 29; low  *= 0xc4ceb9fe1a85ec53ULL; low  ^= low  >> 32;

	high = (high & ~0xf000ULL) | 0x4000ULL;					// version 4
	low  = (low & ~(0xc000ULL << 48)) | (0x8000ULL << 48);	// variant 1

	wchar_t buffer[64];
	swprintf(buffer, 64, L"%08x-%04x-%04x-%04x-%012llx",
	         (unsigned int)(high >> 32),
	         (unsigned int)((high >> 16) & 0xffff),
	         (unsigned int)(high & 0xffff),
	         (unsigned int)((low >> 48) & 0xffff),
	         (unsigned long long)(low & 0xffffffffffffULL));

	return wstring(buffer);
}

wstring AnvilLevelStorage::playerDataPath(PlayerUID xuid) const
{
	return m_worldRoot + L"\\playerdata\\" + uuidStringFor(xuid) + L".dat";
}

void AnvilLevelStorage::convertPlayerTag(CompoundTag *tag)
{
	if (tag == NULL) return;

	tag->putInt(L"DataVersion", ANVIL_DATA_VERSION);

	// The player's carried items are the main thing worth preserving here.
	AnvilItemMapping::convertItemList(tag, L"Inventory");
	AnvilItemMapping::convertItemList(tag, L"EnderItems");

	// Dimension went from an int to a namespaced string.
	if (tag->contains(L"Dimension"))
	{
		const int dimension = tag->getInt(L"Dimension");

		Tag *old = tag->get(L"Dimension");
		tag->remove(L"Dimension");
		delete old;

		tag->putString(L"Dimension",
			dimension == -1 ? L"minecraft:the_nether" :
			(dimension == 1 ? L"minecraft:the_end" : L"minecraft:overworld"));
	}
}

void AnvilLevelStorage::save(shared_ptr<Player> player)
{
	if (player == nullptr) return;

	const PlayerUID xuid = player->getXuid();
	if (xuid == INVALID_XUID || player->isGuest()) return;

	CompoundTag *tag = new CompoundTag();
	player->saveWithoutId(tag);
	convertPlayerTag(tag);

	const wstring path = playerDataPath(xuid);

	// playerdata/ may not exist yet on a brand new world.
	CreateDirectoryW((m_worldRoot + L"\\playerdata").c_str(), NULL);

	writeGzippedNbt(path, tag);
	delete tag;
}

CompoundTag *AnvilLevelStorage::loadPlayerDataTag(PlayerUID xuid)
{
	if (xuid == INVALID_XUID) return NULL;
	return readGzippedNbt(playerDataPath(xuid));
}

bool AnvilLevelStorage::load(shared_ptr<Player> player)
{
	if (player == nullptr) return false;

	CompoundTag *tag = loadPlayerDataTag(player->getXuid());
	if (tag == NULL) return false;

	// The stored form is Java's, so the item lists have to come back the other way before
	// the game reads them: ItemInstance::load expects a short id and would otherwise read
	// a StringTag through a ShortTag cast.
	AnvilItemMapping::convertItemListFromJava(tag, L"Inventory");
	AnvilItemMapping::convertItemListFromJava(tag, L"EnderItems");

	// Dimension went out as a namespaced string and has to come back as an int.
	if (tag->contains(L"Dimension"))
	{
		const wstring dimension = tag->getString(L"Dimension");

		Tag *old = tag->get(L"Dimension");
		tag->remove(L"Dimension");
		delete old;

		tag->putInt(L"Dimension", dimension == L"minecraft:the_nether" ? -1 :
		                          (dimension == L"minecraft:the_end" ? 1 : 0));
	}

	player->load(tag);

	delete tag;
	return true;
}

#endif // _WINDOWS64
