#include "stdafx.h"
#include "CPMZip.h"
#include "../zlib/zlib.h"
#include <string.h>

// Archives arrive from other players, so every offset and length out of the
// file is bounds-checked and the inflated size is capped.
#define CPM_ZIP_MAX_ENTRY (16 * 1024 * 1024)

namespace
{
	unsigned int rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
	unsigned int rd32(const unsigned char *p)
	{
		return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
		       ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
	}

	bool inflateRaw(const unsigned char *src, int srcLen, unsigned int outLen,
	                std::vector<unsigned char> &out, std::string &errOut)
	{
		out.clear();
		if (outLen == 0) return true;
		out.resize(outLen);

		z_stream s;
		memset(&s, 0, sizeof(s));
		s.next_in = (Bytef *)src;
		s.avail_in = (uInt)srcLen;
		s.next_out = (Bytef *)&out[0];
		s.avail_out = (uInt)outLen;

		// Negative window bits selects raw deflate, which is what a zip entry
		// holds - there is no zlib header in front of it.
		if (inflateInit2(&s, -MAX_WBITS) != Z_OK)
		{
			errOut = "could not start decompression";
			out.clear();
			return false;
		}

		int r = inflate(&s, Z_FINISH);
		inflateEnd(&s);

		if (r != Z_STREAM_END || s.total_out != outLen)
		{
			errOut = "compressed data is corrupt";
			out.clear();
			return false;
		}
		return true;
	}
}

bool CPMZipLooksLikeZip(const unsigned char *d, int len)
{
	if (d == NULL || len < 4) return false;
	if (d[0] != 'P' || d[1] != 'K') return false;
	return d[2] == 3 || d[2] == 5 || d[2] == 7;
}

bool CPMZipExtract(const unsigned char *zip, int zipLen,
                   const char *entryName,
                   std::vector<unsigned char> &out,
                   std::string &errOut)
{
	out.clear();
	errOut.clear();

	if (zip == NULL || zipLen < 22)
	{
		errOut = "file is too small to be a zip";
		return false;
	}

	// Find the end-of-central-directory record. It sits at the very end unless
	// the archive carries a comment, so scan back over the largest comment a
	// 16-bit length can describe.
	int scanFrom = zipLen - 22;
	int scanTo = zipLen - 22 - 65535;
	if (scanTo < 0) scanTo = 0;

	int eocd = -1;
	for (int i = scanFrom; i >= scanTo; i--)
	{
		if (zip[i] == 'P' && zip[i + 1] == 'K' && zip[i + 2] == 5 && zip[i + 3] == 6)
		{
			eocd = i;
			break;
		}
	}
	if (eocd < 0)
	{
		errOut = "not a zip archive (no central directory)";
		return false;
	}

	unsigned int count = rd16(zip + eocd + 10);
	unsigned int cdSize = rd32(zip + eocd + 12);
	unsigned int cdOff = rd32(zip + eocd + 16);

	if ((double)cdOff + (double)cdSize > (double)zipLen)
	{
		errOut = "zip central directory is out of range";
		return false;
	}

	const size_t nameLenWanted = strlen(entryName);
	unsigned int p = cdOff;

	for (unsigned int i = 0; i < count; i++)
	{
		if (p + 46 > (unsigned int)zipLen) break;
		if (!(zip[p] == 'P' && zip[p + 1] == 'K' && zip[p + 2] == 1 && zip[p + 3] == 2)) break;

		unsigned int method  = rd16(zip + p + 10);
		unsigned int csize   = rd32(zip + p + 20);
		unsigned int usize   = rd32(zip + p + 24);
		unsigned int nlen    = rd16(zip + p + 28);
		unsigned int elen    = rd16(zip + p + 30);
		unsigned int clen    = rd16(zip + p + 32);
		unsigned int lho     = rd32(zip + p + 42);

		if (p + 46 + nlen > (unsigned int)zipLen) break;
		const char *name = (const char *)(zip + p + 46);

		bool match = (nlen == nameLenWanted) && (memcmp(name, entryName, nlen) == 0);
		if (match)
		{
			if (usize > CPM_ZIP_MAX_ENTRY || csize > (unsigned int)zipLen)
			{
				errOut = "zip entry is implausibly large";
				return false;
			}
			if (lho + 30 > (unsigned int)zipLen)
			{
				errOut = "zip entry points outside the file";
				return false;
			}
			if (!(zip[lho] == 'P' && zip[lho + 1] == 'K' && zip[lho + 2] == 3 && zip[lho + 3] == 4))
			{
				errOut = "zip entry header is corrupt";
				return false;
			}

			// The local header repeats the name and extra-field lengths, and
			// they can differ from the central directory's, so the data offset
			// has to come from the local header.
			unsigned int lnlen = rd16(zip + lho + 26);
			unsigned int lelen = rd16(zip + lho + 28);
			unsigned int dataOff = lho + 30 + lnlen + lelen;

			if ((double)dataOff + (double)csize > (double)zipLen)
			{
				errOut = "zip entry data is out of range";
				return false;
			}

			unsigned int crc = rd32(zip + p + 16);

			if (method == 0)
			{
				// Stored.
				out.assign(zip + dataOff, zip + dataOff + csize);
			}
			else if (method == 8)
			{
				if (!inflateRaw(zip + dataOff, (int)csize, usize, out, errOut)) return false;
			}
			else
			{
				errOut = "zip entry uses an unsupported compression method";
				return false;
			}

			// Deflate tolerates a lot of corruption without failing outright, so
			// the stored CRC is what actually catches a damaged entry. These
			// archives can arrive from another player.
			if (!out.empty())
			{
				unsigned long actual = crc32(0L, Z_NULL, 0);
				actual = crc32(actual, (const Bytef *)&out[0], (uInt)out.size());
				if ((unsigned int)actual != crc)
				{
					errOut = "zip entry failed its checksum (file is corrupt)";
					out.clear();
					return false;
				}
			}
			return true;

		}

		p += 46 + nlen + elen + clen;
	}

	errOut = std::string("the archive has no ") + entryName;
	return false;
}
