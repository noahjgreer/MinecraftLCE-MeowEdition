#include "stdafx.h"

#ifdef _WINDOWS64

#include "Win64SaveFile.h"
#include <windows.h>

namespace Win64SaveFile
{
	static std::wstring s_worldName = L"world";

	void SetWorldName(const std::wstring &name)
	{
		// An empty level-name would put savegame.dat at the root of the working
		// directory and collide with the next world, so ignore it.
		if( name.empty() ) return;
		s_worldName = name;
	}

	const std::wstring &GetWorldName()
	{
		return s_worldName;
	}

	std::wstring GetSavePath()
	{
		return s_worldName + L"\\savegame.dat";
	}

	static std::wstring GetTempPath_()
	{
		return s_worldName + L"\\savegame.dat.tmp";
	}

	unsigned int GetSize()
	{
		WIN32_FILE_ATTRIBUTE_DATA fad;
		if( !GetFileAttributesExW( GetSavePath().c_str(), GetFileExInfoStandard, &fad ) ) return 0;

		// The blob is nowhere near 4GB and the rest of the save path is all
		// 32-bit, so a file claiming to be bigger than that is corrupt rather
		// than something to truncate.
		if( fad.nFileSizeHigh != 0 ) return 0;

		return (unsigned int)fad.nFileSizeLow;
	}

	bool Read(void *pvData, unsigned int *puiBytes)
	{
		if( puiBytes != NULL ) *puiBytes = 0;
		if( pvData == NULL ) return false;

		HANDLE hFile = CreateFileW( GetSavePath().c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
		if( hFile == INVALID_HANDLE_VALUE ) return false;

		unsigned int uiSize = GetSize();
		unsigned int uiTotal = 0;
		bool bOk = true;

		// ReadFile is not obliged to satisfy the whole request in one go.
		while( uiTotal < uiSize )
		{
			DWORD dwRead = 0;
			if( !ReadFile( hFile, (byte *)pvData + uiTotal, uiSize - uiTotal, &dwRead, NULL ) || dwRead == 0 )
			{
				bOk = false;
				break;
			}
			uiTotal += dwRead;
		}

		CloseHandle( hFile );

		if( !bOk ) return false;
		if( puiBytes != NULL ) *puiBytes = uiTotal;
		return true;
	}

	bool Write(const void *pvData, unsigned int uiBytes)
	{
		if( pvData == NULL || uiBytes == 0 ) return false;

		// The world directory is also where McRegionLevelStorage expects to be,
		// but nothing has necessarily created it by the time we save.
		CreateDirectoryW( s_worldName.c_str(), NULL );

		std::wstring tempPath = GetTempPath_();

		HANDLE hFile = CreateFileW( tempPath.c_str(), GENERIC_WRITE, 0, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
		if( hFile == INVALID_HANDLE_VALUE ) return false;

		unsigned int uiTotal = 0;
		bool bOk = true;

		while( uiTotal < uiBytes )
		{
			DWORD dwWritten = 0;
			if( !WriteFile( hFile, (const byte *)pvData + uiTotal, uiBytes - uiTotal, &dwWritten, NULL ) || dwWritten == 0 )
			{
				bOk = false;
				break;
			}
			uiTotal += dwWritten;
		}

		// Get the bytes onto the platter before the rename, otherwise the rename
		// can land first and the "safe" temp file buys us nothing.
		if( bOk ) FlushFileBuffers( hFile );
		CloseHandle( hFile );

		if( !bOk )
		{
			DeleteFileW( tempPath.c_str() );
			return false;
		}

		if( !MoveFileExW( tempPath.c_str(), GetSavePath().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
		{
			DeleteFileW( tempPath.c_str() );
			return false;
		}

		return true;
	}
}

#endif // _WINDOWS64
