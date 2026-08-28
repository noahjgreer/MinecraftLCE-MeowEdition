#pragma once
// CPM - minimal in-memory ZIP reader, for .cpmproject files.
//
// A .cpmproject is the CPM editor's working format: a ZIP holding config.json,
// skin.png and optionally an animations/ folder. Only named-entry extraction is
// needed, so this is a reader and nothing more.
//
// It walks the central directory rather than the local file headers, because
// Java's ZipOutputStream sets the data-descriptor flag and leaves the
// compressed size zero in the local header.
//
// Decompression uses the zlib already compiled into the client
// (Minecraft.Client/Common/zlib), in raw-deflate mode.

#include <vector>
#include <string>

// Extracts one entry by exact name. Returns false if the archive is malformed
// or the entry is missing; `errOut` gets a reason.
bool CPMZipExtract(const unsigned char *zip, int zipLen,
                   const char *entryName,
                   std::vector<unsigned char> &out,
                   std::string &errOut);

// True if the buffer starts with a local-file-header or empty-archive magic.
bool CPMZipLooksLikeZip(const unsigned char *data, int len);
