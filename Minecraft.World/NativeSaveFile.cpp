#include "stdafx.h"

#ifdef _WINDOWS64

#include "NativeSaveFile.h"
#include "File.h"
#include "AnvilSavePaths.h"

// 4J Meow - see NativeSaveFile.h.

// LCE's virtual paths use forward slashes; Win32 is happy with either, but the
// directory-creation walk below needs one consistent separator.
static wstring normalise(const wstring &path)
{
	wstring out = path;
	for (unsigned int i = 0; i < out.length(); i++) if (out[i] == L'/') out[i] = L'\\';
	return out;
}

void NativeSaveFile::makeDirectories(const wstring &path)
{
	// Walk each separator in turn, creating what is missing and ignoring what is not.
	for (unsigned int i = 0; i < path.length(); i++)
	{
		if (path[i] != L'\\') continue;
		if (i == 0) continue;

		const wstring partial = path.substr(0, i);

		// Skip a bare drive prefix such as "C:".
		if (partial.length() == 2 && partial[1] == L':') continue;

		CreateDirectoryW(partial.c_str(), NULL);
	}
}

NativeSaveFile::NativeSaveFile(const wstring &worldRoot)
{
	m_root = normalise(worldRoot);

	// Strip a trailing separator so resolve() can append unconditionally.
	while (!m_root.empty() && m_root[m_root.length() - 1] == L'\\')
	{
		m_root = m_root.substr(0, m_root.length() - 1);
	}

	m_platform = SAVE_FILE_PLATFORM_LOCAL;
	m_endian = LOCALSYTEM_ENDIAN;

	InitializeCriticalSection(&m_lock);

	makeDirectories(m_root + L"\\");
	CreateDirectoryW(m_root.c_str(), NULL);

	const DWORD attributes = GetFileAttributesW(m_root.c_str());
	AnvilSavePaths::log("[anvil] NativeSaveFile root \"%ls\" exists=%d lastError=%lu\n",
	                    m_root.c_str(),
	                    attributes != INVALID_FILE_ATTRIBUTES ? 1 : 0,
	                    (unsigned long)GetLastError());
}

NativeSaveFile::~NativeSaveFile()
{
	for (AUTO_VAR(it, m_files.begin()); it != m_files.end(); ++it)
	{
		OpenFile *file = it->second;
		if (file->handle != NULL) fclose(file->handle);
		delete file->entry;
		delete file;
	}
	m_files.clear();

	DeleteCriticalSection(&m_lock);
}

wstring NativeSaveFile::resolve(const wstring &virtualPath, bool createDirectories)
{
	wstring relative = normalise(virtualPath);

	// Java keeps per-player data in playerdata/; LCE calls the same folder players/.
	// Translating here means the rest of the game keeps using its own naming.
	if (relative.compare(0, 8, L"players\\") == 0)
	{
		relative = L"playerdata\\" + relative.substr(8);
	}

	const wstring full = m_root + L"\\" + relative;
	if (createDirectories) makeDirectories(full);
	return full;
}

NativeSaveFile::OpenFile *NativeSaveFile::lookup(const wstring &virtualPath, bool createIfMissing)
{
	AUTO_VAR(it, m_files.find(virtualPath));
	if (it != m_files.end()) return it->second;

	if (!createIfMissing) return NULL;

	OpenFile *file = new OpenFile();
	file->path = resolve(virtualPath, true);
	file->handle = NULL;

	// FileEntry carries a fixed 64-wchar name; keep the virtual path in it so callers
	// that inspect the name (the load menus, getFilesWithPrefix) see what they expect.
	wchar_t name[64];
	memset(name, 0, sizeof(name));
	wcsncpy(name, virtualPath.c_str(), 63);

	file->entry = new FileEntry(name, 0, 0);
	file->entry->currentFilePointer = 0;

	m_files[virtualPath] = file;
	refreshLength(file);
	return file;
}

bool NativeSaveFile::ensureOpen(OpenFile *file)
{
	if (file == NULL) return false;
	if (file->handle != NULL) return true;

	// "r+b" preserves an existing file; "w+b" creates one when it is not there yet.
	file->handle = _wfopen(file->path.c_str(), L"r+b");
	if (file->handle == NULL) file->handle = _wfopen(file->path.c_str(), L"w+b");

	return file->handle != NULL;
}

void NativeSaveFile::refreshLength(OpenFile *file)
{
	if (file == NULL) return;

	WIN32_FILE_ATTRIBUTE_DATA attributes;
	if (GetFileAttributesExW(file->path.c_str(), GetFileExInfoStandard, &attributes))
	{
		file->entry->data.length = attributes.nFileSizeLow;

		// FILETIME is 100ns units since 1601; the game wants milliseconds since 1970.
		ULARGE_INTEGER ticks;
		ticks.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
		ticks.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
		file->entry->data.lastModifiedTime = (__int64)(ticks.QuadPart / 10000ULL) - 11644473600000LL;
	}
	else
	{
		file->entry->data.length = 0;
		file->entry->data.lastModifiedTime = 0;
	}
}

FileEntry *NativeSaveFile::createFile(const ConsoleSavePath &fileName)
{
	LockSaveAccess();
	OpenFile *file = lookup(fileName.getName(), true);
	ReleaseSaveAccess();

	return file != NULL ? file->entry : NULL;
}

void NativeSaveFile::deleteFile(FileEntry *file)
{
	if (file == NULL) return;

	LockSaveAccess();

	const wstring key = file->data.filename;
	AUTO_VAR(it, m_files.find(key));

	if (it != m_files.end())
	{
		OpenFile *open = it->second;
		if (open->handle != NULL)
		{
			fclose(open->handle);
			open->handle = NULL;
		}

		_wremove(open->path.c_str());

		m_files.erase(it);
		delete open->entry;
		delete open;
	}

	ReleaseSaveAccess();
}

void NativeSaveFile::setFilePointer(FileEntry *file, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
	if (file == NULL) return;

	// The game only ever uses the low 32 bits; a world file never approaches 4GB.
	__int64 target = lDistanceToMove;

	switch (dwMoveMethod)
	{
	case FILE_BEGIN:   break;
	case FILE_CURRENT: target += file->currentFilePointer; break;
	case FILE_END:     target += file->data.length; break;
	default: return;
	}

	if (target < 0) target = 0;
	file->currentFilePointer = (unsigned int)target;

	if (lpDistanceToMoveHigh != NULL) *lpDistanceToMoveHigh = 0;
}

BOOL NativeSaveFile::readFile(FileEntry *file, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead)
{
	if (lpNumberOfBytesRead != NULL) *lpNumberOfBytesRead = 0;
	if (file == NULL || lpBuffer == NULL) return FALSE;

	LockSaveAccess();

	OpenFile *open = lookup(file->data.filename, false);
	if (open == NULL || !ensureOpen(open))
	{
		ReleaseSaveAccess();
		return FALSE;
	}

	fseek(open->handle, (long)file->currentFilePointer, SEEK_SET);
	const size_t got = fread(lpBuffer, 1, nNumberOfBytesToRead, open->handle);

	file->currentFilePointer += (unsigned int)got;
	if (lpNumberOfBytesRead != NULL) *lpNumberOfBytesRead = (DWORD)got;

	ReleaseSaveAccess();

	// Reading zero bytes at the end of a file is a normal EOF, not a failure - the
	// stream wrappers distinguish the two by the count, not the return value.
	return TRUE;
}

BOOL NativeSaveFile::writeFile(FileEntry *file, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten)
{
	if (lpNumberOfBytesWritten != NULL) *lpNumberOfBytesWritten = 0;
	if (file == NULL || lpBuffer == NULL) return FALSE;

	LockSaveAccess();

	OpenFile *open = lookup(file->data.filename, true);
	if (open == NULL || !ensureOpen(open))
	{
		ReleaseSaveAccess();
		return FALSE;
	}

	fseek(open->handle, (long)file->currentFilePointer, SEEK_SET);
	const size_t put = fwrite(lpBuffer, 1, nNumberOfBytesToWrite, open->handle);

	file->currentFilePointer += (unsigned int)put;
	if (file->currentFilePointer > file->data.length) file->data.length = file->currentFilePointer;

	file->updateLastModifiedTime();

	if (lpNumberOfBytesWritten != NULL) *lpNumberOfBytesWritten = (DWORD)put;

	ReleaseSaveAccess();
	return put == nNumberOfBytesToWrite ? TRUE : FALSE;
}

BOOL NativeSaveFile::zeroFile(FileEntry *file, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten)
{
	if (nNumberOfBytesToWrite == 0)
	{
		if (lpNumberOfBytesWritten != NULL) *lpNumberOfBytesWritten = 0;
		return TRUE;
	}

	byte *zeros = new byte[nNumberOfBytesToWrite];
	memset(zeros, 0, nNumberOfBytesToWrite);

	const BOOL result = writeFile(file, zeros, nNumberOfBytesToWrite, lpNumberOfBytesWritten);

	delete[] zeros;
	return result;
}

BOOL NativeSaveFile::closeHandle(FileEntry *file)
{
	if (file == NULL) return FALSE;

	LockSaveAccess();

	OpenFile *open = lookup(file->data.filename, false);
	if (open != NULL && open->handle != NULL)
	{
		fflush(open->handle);

		// The handle is kept open deliberately: the callers close and reopen the same
		// few files constantly, and the entry has to stay valid either way.
		refreshLength(open);
	}

	ReleaseSaveAccess();
	return TRUE;
}

void NativeSaveFile::finalizeWrite()
{
	Flush(false, false);
}

bool NativeSaveFile::doesFileExist(ConsoleSavePath file)
{
	const wstring full = resolve(file.getName(), false);
	const DWORD attributes = GetFileAttributesW(full.c_str());

	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void NativeSaveFile::Flush(bool autosave, bool updateThumbnail)
{
	LockSaveAccess();

	for (AUTO_VAR(it, m_files.begin()); it != m_files.end(); ++it)
	{
		OpenFile *open = it->second;
		if (open->handle != NULL)
		{
			fflush(open->handle);
			refreshLength(open);
		}
	}

	ReleaseSaveAccess();
}

#ifndef _CONTENT_PACKAGE
void NativeSaveFile::DebugFlushToFile(void *compressedData, unsigned int compressedDataSize)
{
	// Nothing to dump: the save is already a directory of plain files on disk.
	Flush(false, false);
}
#endif

wstring NativeSaveFile::getFilename()
{
	return m_root;
}

// Walks the world folder and makes sure every file actually on disk has an entry, so
// the enumeration calls below can see worlds this process did not itself create.
static void scanDirectory(const wstring &root, const wstring &relative, vector<wstring> &out)
{
	const wstring pattern = root + (relative.empty() ? L"" : L"\\" + relative) + L"\\*";

	WIN32_FIND_DATAW found;
	HANDLE search = FindFirstFileW(pattern.c_str(), &found);
	if (search == INVALID_HANDLE_VALUE) return;

	do
	{
		const wstring name = found.cFileName;
		if (name == L"." || name == L"..") continue;

		const wstring child = relative.empty() ? name : relative + L"\\" + name;

		if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) scanDirectory(root, child, out);
		else out.push_back(child);
	}
	while (FindNextFileW(search, &found));

	FindClose(search);
}

// Turns an on-disk relative path back into the virtual path the game uses.
static wstring toVirtualPath(const wstring &diskRelative)
{
	wstring out = diskRelative;
	for (unsigned int i = 0; i < out.length(); i++) if (out[i] == L'\\') out[i] = L'/';

	if (out.compare(0, 11, L"playerdata/") == 0) out = L"players/" + out.substr(11);

	return out;
}

vector<FileEntry *> *NativeSaveFile::getFilesWithPrefix(const wstring &prefix)
{
	LockSaveAccess();

	vector<wstring> onDisk;
	scanDirectory(m_root, L"", onDisk);

	for (unsigned int i = 0; i < onDisk.size(); i++) lookup(toVirtualPath(onDisk[i]), true);

	// The callers own the returned vector and delete it (see McRegionLevelStorage.cpp),
	// so this must be heap allocated. The FileEntry pointers inside stay owned by m_files.
	vector<FileEntry *> *results = new vector<FileEntry *>();

	for (AUTO_VAR(it, m_files.begin()); it != m_files.end(); ++it)
	{
		if (it->first.compare(0, prefix.length(), prefix) != 0) continue;

		refreshLength(it->second);
		results->push_back(it->second->entry);
	}

	ReleaseSaveAccess();
	return results;
}

vector<FileEntry *> *NativeSaveFile::getRegionFilesByDimension(unsigned int dimensionIndex)
{
	// Region data does not live in the save file any more - AnvilChunkStorage owns
	// region/*.mca directly - so there is nothing here to enumerate. The caller still
	// deletes what it gets back, so hand it an empty heap vector rather than NULL.
	return new vector<FileEntry *>();
}

unsigned int NativeSaveFile::getSizeOnDisk()
{
	vector<wstring> onDisk;
	scanDirectory(m_root, L"", onDisk);

	unsigned __int64 total = 0;

	for (unsigned int i = 0; i < onDisk.size(); i++)
	{
		WIN32_FILE_ATTRIBUTE_DATA attributes;
		if (GetFileAttributesExW((m_root + L"\\" + onDisk[i]).c_str(), GetFileExInfoStandard, &attributes))
		{
			total += ((unsigned __int64)attributes.nFileSizeHigh << 32) | attributes.nFileSizeLow;
		}
	}

	// The callers display this as a 32-bit byte count; clamp rather than wrap.
	return total > 0xffffffffULL ? 0xffffffffU : (unsigned int)total;
}

#endif // _WINDOWS64
