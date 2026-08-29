#pragma once

#ifdef _WINDOWS64

using namespace std;

#include "stdafx.h"
#include "ConsoleSaveFile.h"

// 4J Meow - A ConsoleSaveFile backed by a real directory instead of a single blob.
//
// On console a world is one compressed file: ConsoleSaveFileOriginal keeps a FileHeader
// of (name, offset, length) entries and every "file" inside the world is a range of
// bytes in that blob. Everything above it - DirectoryLevelStorage, the load menus,
// thumbnails, save-size display - is written against the ConsoleSaveFile interface, so
// re-implementing that interface over a directory moves the whole game onto plain files
// without touching any of its 51 callers.
//
// The virtual paths LCE already uses line up with a Java world folder almost exactly:
//
//     level.dat          ->  <root>/level.dat
//     data/<id>.dat      ->  <root>/data/<id>.dat
//     players/<uid>.dat  ->  <root>/playerdata/<uid>.dat   (renamed to Java's folder)
//     r.<x>.<z>.mcr      ->  <root>/region/r.<x>.<z>.mca   (written by AnvilChunkStorage)
//
// A FileEntry is a handle rather than a byte range here: it holds the resolved path and
// a lazily-opened stdio handle, and is cached so repeated createFile() calls for the
// same path return the same entry, which is what the callers assume.

class NativeSaveFile : public ConsoleSaveFile
{
private:
	struct OpenFile
	{
		wstring path;
		FILE *handle;
		FileEntry *entry;

		OpenFile() : handle(NULL), entry(NULL) {}
	};

	wstring m_root;
	map<wstring, OpenFile *> m_files;		// keyed by virtual path

	ESavePlatform m_platform;
	ByteOrder m_endian;
	CRITICAL_SECTION m_lock;

	// Maps a virtual path onto its place in the world folder, creating parent
	// directories as needed.
	wstring resolve(const wstring &virtualPath, bool createDirectories);

	OpenFile *lookup(const wstring &virtualPath, bool createIfMissing);
	bool ensureOpen(OpenFile *file);
	void refreshLength(OpenFile *file);

	static void makeDirectories(const wstring &path);

public:
	NativeSaveFile(const wstring &worldRoot);
	virtual ~NativeSaveFile();

	const wstring &getRoot() const { return m_root; }

	virtual FileEntry *createFile(const ConsoleSavePath &fileName);
	virtual void deleteFile(FileEntry *file);
	virtual void setFilePointer(FileEntry *file, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
	virtual BOOL writeFile(FileEntry *file, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten);
	virtual BOOL zeroFile(FileEntry *file, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten);
	virtual BOOL readFile(FileEntry *file, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead);
	virtual BOOL closeHandle(FileEntry *file);
	virtual void finalizeWrite();

	virtual bool doesFileExist(ConsoleSavePath file);

	virtual void Flush(bool autosave, bool updateThumbnail = true);

#ifndef _CONTENT_PACKAGE
	virtual void DebugFlushToFile(void *compressedData = NULL, unsigned int compressedDataSize = 0);
#endif

	virtual unsigned int getSizeOnDisk();
	virtual wstring getFilename();
	virtual vector<FileEntry *> *getFilesWithPrefix(const wstring &prefix);
	virtual vector<FileEntry *> *getRegionFilesByDimension(unsigned int dimensionIndex);

	virtual int getSaveVersion()         { return SAVE_FILE_VERSION_NUMBER; }
	virtual int getOriginalSaveVersion() { return SAVE_FILE_VERSION_NUMBER; }

	virtual void LockSaveAccess()    { EnterCriticalSection(&m_lock); }
	virtual void ReleaseSaveAccess() { LeaveCriticalSection(&m_lock); }

	virtual ESavePlatform getSavePlatform()  { return m_platform; }
	virtual bool isSaveEndianDifferent()     { return false; }
	virtual void setLocalPlatform()          { m_platform = SAVE_FILE_PLATFORM_LOCAL; }
	virtual void setPlatform(ESavePlatform plat) { m_platform = plat; }
	virtual ByteOrder getSaveEndian()        { return m_endian; }
	virtual ByteOrder getLocalEndian()       { return m_endian; }
	virtual void setEndian(ByteOrder endian) { m_endian = endian; }

	// Both are no-ops: a native save is always already in the local platform's form.
	virtual void ConvertRegionFile(File sourceFile) {}
	virtual void ConvertToLocalPlatform() {}
};

#endif // _WINDOWS64
