#pragma once
using namespace std;

#include "stdafx.h"

// 4J Meow - A region file written to the real filesystem in the format Java Edition
// actually uses, as opposed to Minecraft.World/RegionFile.h which stores its regions
// inside the console save blob and compresses them with 4J's LZX/RLE codec.
//
// Layout (unchanged since Beta 1.3):
//   [0..4096)      1024 big-endian ints: (sectorOffset << 8) | sectorCount
//   [4096..8192)   1024 big-endian ints: last-modified timestamps
//   [8192..)       4KiB sectors; each chunk is a big-endian int length, a compression
//                  type byte, then that many - 1 bytes of compressed NBT.
//
// Only compression type 2 (zlib) is written; types 1 (gzip) and 3 (uncompressed) are
// accepted on read so externally-produced regions still load.

class AnvilRegionFile
{
private:
	static const int SECTOR_BYTES = 4096;
	static const int SECTOR_INTS = SECTOR_BYTES / 4;
	static const int CHUNK_HEADER_SIZE = 5;

	static const int COMPRESSION_GZIP = 1;
	static const int COMPRESSION_ZLIB = 2;
	static const int COMPRESSION_NONE = 3;

	FILE *m_file;
	wstring m_path;

	int m_offsets[SECTOR_INTS];
	int m_timestamps[SECTOR_INTS];

	// One entry per 4KiB sector in the file; true means in use.
	vector<bool> m_sectorUsed;

	static int index(int x, int z) { return (x & 31) + (z & 31) * 32; }

	bool readHeader();
	void writeHeader();

	// Finds a run of the requested length, extending the file if nothing fits.
	int allocateSectors(int count);
	void releaseSectors(int start, int count);

public:
	AnvilRegionFile(const wstring &path);
	~AnvilRegionFile();

	bool isOpen() const { return m_file != NULL; }

	bool hasChunk(int x, int z);

	// Returns the decompressed NBT payload for a chunk, or an empty array if absent.
	byteArray readChunk(int x, int z);

	// Compresses and stores the NBT payload for a chunk.
	bool writeChunk(int x, int z, const byte *data, unsigned int length);

	void flush();
};
