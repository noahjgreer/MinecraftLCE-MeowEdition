#include "stdafx.h"
#include "AnvilRegionFile.h"

#include "..\Minecraft.Client\Common\zlib\zlib.h"

// 4J Meow - see AnvilRegionFile.h.

static void writeBE32(byte *p, int v)
{
	p[0] = (byte)((v >> 24) & 0xff);
	p[1] = (byte)((v >> 16) & 0xff);
	p[2] = (byte)((v >> 8) & 0xff);
	p[3] = (byte)(v & 0xff);
}

static int readBE32(const byte *p)
{
	return ((int)p[0] << 24) | ((int)p[1] << 16) | ((int)p[2] << 8) | (int)p[3];
}

AnvilRegionFile::AnvilRegionFile(const wstring &path)
{
	m_path = path;
	m_file = NULL;

	memset(m_offsets, 0, sizeof(m_offsets));
	memset(m_timestamps, 0, sizeof(m_timestamps));

	// "r+b" first so an existing region is not truncated; fall back to creating it.
	m_file = _wfopen(path.c_str(), L"r+b");
	if (m_file == NULL) m_file = _wfopen(path.c_str(), L"w+b");
	if (m_file == NULL) return;

	if (!readHeader())
	{
		fclose(m_file);
		m_file = NULL;
	}
}

AnvilRegionFile::~AnvilRegionFile()
{
	if (m_file != NULL)
	{
		writeHeader();
		fclose(m_file);
		m_file = NULL;
	}
}

bool AnvilRegionFile::readHeader()
{
	fseek(m_file, 0, SEEK_END);
	long size = ftell(m_file);

	if (size < SECTOR_BYTES * 2)
	{
		// New or truncated file - lay down the two empty header sectors.
		byte blank[SECTOR_BYTES];
		memset(blank, 0, sizeof(blank));

		fseek(m_file, 0, SEEK_SET);
		if (fwrite(blank, 1, SECTOR_BYTES, m_file) != SECTOR_BYTES) return false;
		if (fwrite(blank, 1, SECTOR_BYTES, m_file) != SECTOR_BYTES) return false;

		size = SECTOR_BYTES * 2;
	}
	else
	{
		byte header[SECTOR_BYTES * 2];

		fseek(m_file, 0, SEEK_SET);
		if (fread(header, 1, SECTOR_BYTES * 2, m_file) != SECTOR_BYTES * 2) return false;

		for (int i = 0; i < SECTOR_INTS; i++)
		{
			m_offsets[i] = readBE32(header + i * 4);
			m_timestamps[i] = readBE32(header + SECTOR_BYTES + i * 4);
		}
	}

	// A partial trailing sector would corrupt the allocator's arithmetic; pad it out.
	if ((size % SECTOR_BYTES) != 0)
	{
		const long pad = SECTOR_BYTES - (size % SECTOR_BYTES);
		byte zero = 0;

		fseek(m_file, 0, SEEK_END);
		for (long i = 0; i < pad; i++) fwrite(&zero, 1, 1, m_file);
		size += pad;
	}

	const int totalSectors = (int)(size / SECTOR_BYTES);

	m_sectorUsed.clear();
	m_sectorUsed.resize(totalSectors, false);
	m_sectorUsed[0] = true;	// offset table
	m_sectorUsed[1] = true;	// timestamp table

	for (int i = 0; i < SECTOR_INTS; i++)
	{
		const int entry = m_offsets[i];
		if (entry == 0) continue;

		const int start = entry >> 8;
		const int count = entry & 0xff;

		// Drop entries that point outside the file rather than trusting them.
		if (start < 2 || count <= 0 || start + count > totalSectors)
		{
			m_offsets[i] = 0;
			continue;
		}

		for (int s = 0; s < count; s++) m_sectorUsed[start + s] = true;
	}

	return true;
}

void AnvilRegionFile::writeHeader()
{
	byte header[SECTOR_BYTES * 2];
	memset(header, 0, sizeof(header));

	for (int i = 0; i < SECTOR_INTS; i++)
	{
		writeBE32(header + i * 4, m_offsets[i]);
		writeBE32(header + SECTOR_BYTES + i * 4, m_timestamps[i]);
	}

	fseek(m_file, 0, SEEK_SET);
	fwrite(header, 1, SECTOR_BYTES * 2, m_file);
	fflush(m_file);
}

int AnvilRegionFile::allocateSectors(int count)
{
	const int total = (int)m_sectorUsed.size();

	for (int start = 2; start + count <= total; start++)
	{
		bool ok = true;
		for (int s = 0; s < count && ok; s++) if (m_sectorUsed[start + s]) ok = false;

		if (ok)
		{
			for (int s = 0; s < count; s++) m_sectorUsed[start + s] = true;
			return start;
		}
	}

	// Nothing free - grow the file by the requested run.
	const int start = total;
	byte blank[SECTOR_BYTES];
	memset(blank, 0, sizeof(blank));

	fseek(m_file, 0, SEEK_END);
	for (int s = 0; s < count; s++)
	{
		fwrite(blank, 1, SECTOR_BYTES, m_file);
		m_sectorUsed.push_back(true);
	}

	return start;
}

void AnvilRegionFile::releaseSectors(int start, int count)
{
	for (int s = 0; s < count; s++)
	{
		const int sector = start + s;
		if (sector >= 0 && sector < (int)m_sectorUsed.size()) m_sectorUsed[sector] = false;
	}
}

bool AnvilRegionFile::hasChunk(int x, int z)
{
	if (m_file == NULL) return false;
	return m_offsets[index(x, z)] != 0;
}

byteArray AnvilRegionFile::readChunk(int x, int z)
{
	if (m_file == NULL) return byteArray();

	const int entry = m_offsets[index(x, z)];
	if (entry == 0) return byteArray();

	const int start = entry >> 8;
	const int count = entry & 0xff;

	fseek(m_file, start * SECTOR_BYTES, SEEK_SET);

	byte head[CHUNK_HEADER_SIZE];
	if (fread(head, 1, CHUNK_HEADER_SIZE, m_file) != CHUNK_HEADER_SIZE) return byteArray();

	const int length = readBE32(head);			// includes the compression type byte
	const int compression = head[4];

	if (length <= 1 || length - 1 > count * SECTOR_BYTES) return byteArray();

	const int payload = length - 1;
	byte *raw = new byte[payload];
	if (fread(raw, 1, payload, m_file) != (size_t)payload)
	{
		delete[] raw;
		return byteArray();
	}

	if (compression == COMPRESSION_NONE)
	{
		byteArray result(payload);
		memcpy(result.data, raw, payload);
		delete[] raw;
		return result;
	}

	// zlib and gzip differ only in the window-bits argument to inflateInit2.
	z_stream zs;
	memset(&zs, 0, sizeof(zs));
	if (inflateInit2(&zs, compression == COMPRESSION_GZIP ? (16 + MAX_WBITS) : MAX_WBITS) != Z_OK)
	{
		delete[] raw;
		return byteArray();
	}

	zs.next_in = (Bytef *)raw;
	zs.avail_in = payload;

	// Chunk NBT is a few tens of KiB; grow geometrically rather than guess a cap.
	unsigned int capacity = payload * 8 + 4096;
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

		if (status != Z_OK)
		{
			// Z_BUF_ERROR with no output space left just means we need a bigger buffer.
			if (status != Z_BUF_ERROR || zs.avail_out != 0) break;
		}

		if (zs.avail_out == 0)
		{
			const unsigned int grown = capacity * 2;
			byte *bigger = new byte[grown];
			memcpy(bigger, out, produced);
			delete[] out;
			out = bigger;
			capacity = grown;
		}
		else if (status == Z_OK && zs.avail_in == 0)
		{
			break;	// ran out of input without a stream end - truncated chunk
		}
	}

	inflateEnd(&zs);
	delete[] raw;

	if (status != Z_STREAM_END)
	{
		delete[] out;
		return byteArray();
	}

	byteArray result(produced);
	memcpy(result.data, out, produced);
	delete[] out;
	return result;
}

bool AnvilRegionFile::writeChunk(int x, int z, const byte *data, unsigned int length)
{
	if (m_file == NULL || data == NULL || length == 0) return false;

	uLongf bound = compressBound(length);
	byte *comp = new byte[bound];

	if (compress2((Bytef *)comp, &bound, (const Bytef *)data, length, Z_DEFAULT_COMPRESSION) != Z_OK)
	{
		delete[] comp;
		return false;
	}

	const int total = (int)bound + CHUNK_HEADER_SIZE;
	const int needed = (total + SECTOR_BYTES - 1) / SECTOR_BYTES;

	// A chunk larger than 255 sectors cannot be addressed by the offset table. Java
	// spills these into an external .mcc file; LCE chunks are far too small to hit it,
	// so refuse rather than silently corrupt the region.
	if (needed > 255)
	{
		delete[] comp;
		return false;
	}

	const int slot = index(x, z);
	const int existing = m_offsets[slot];
	const int existingStart = existing >> 8;
	const int existingCount = existing & 0xff;

	int start;
	if (existing != 0 && existingCount == needed)
	{
		start = existingStart;	// same footprint - overwrite in place
	}
	else
	{
		if (existing != 0) releaseSectors(existingStart, existingCount);
		start = allocateSectors(needed);
	}

	byte head[CHUNK_HEADER_SIZE];
	writeBE32(head, (int)bound + 1);	// length counts the compression type byte
	head[4] = (byte)COMPRESSION_ZLIB;

	fseek(m_file, start * SECTOR_BYTES, SEEK_SET);
	const bool ok = fwrite(head, 1, CHUNK_HEADER_SIZE, m_file) == CHUNK_HEADER_SIZE &&
	                fwrite(comp, 1, bound, m_file) == bound;

	delete[] comp;
	if (!ok) return false;

	// Zero the remainder of the final sector so stale bytes never leak into a reader.
	const int tail = needed * SECTOR_BYTES - total;
	if (tail > 0)
	{
		byte *pad = new byte[tail];
		memset(pad, 0, tail);
		fwrite(pad, 1, tail, m_file);
		delete[] pad;
	}

	m_offsets[slot] = (start << 8) | needed;
	m_timestamps[slot] = (int)(System::currentRealTimeMillis() / 1000);

	writeHeader();
	return true;
}

void AnvilRegionFile::flush()
{
	if (m_file == NULL) return;
	writeHeader();
	fflush(m_file);
}
