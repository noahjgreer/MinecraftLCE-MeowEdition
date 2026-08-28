#include "stdafx.h"

#ifdef _WINDOWS64

#include "Win64CommandLine.h"
#include "..\Common\Network\Sockets\PlatformNetworkManagerSockets.h"
#include <string.h>

namespace Win64CommandLine
{
	static wchar_t	s_szServerAddress[256]	= L"";
	static wchar_t	s_szPlayerName[64]		= L"";
	static int		s_iServerPort			= MINECRAFT_DEFAULT_SERVER_PORT;

	static bool		s_bDedicated			= false;
	static bool		s_bFlashUI				= false;	// 4J Meow
	static wchar_t	s_szWorldName[64]		= L"world";
	static bool		s_bHasWorldName			= false;
	static __int64	s_iSeed					= 0;
	static bool		s_bHasSeed				= false;
	static int		s_iMaxPlayers			= MINECRAFT_NET_MAX_PLAYERS;

	// Copies the token starting at *ppsz into pszOut, honouring double quotes so
	// a display name may contain spaces. Advances *ppsz past the token.
	static void ReadToken(const wchar_t **ppsz, wchar_t *pszOut, int iOutSize)
	{
		const wchar_t *psz = *ppsz;

		while (*psz == L' ' || *psz == L'\t') psz++;

		bool bQuoted = false;
		if (*psz == L'"')
		{
			bQuoted = true;
			psz++;
		}

		int i = 0;
		while (*psz != 0 && i < iOutSize - 1)
		{
			if (bQuoted)
			{
				if (*psz == L'"') { psz++; break; }
			}
			else
			{
				if (*psz == L' ' || *psz == L'\t') break;
			}

			pszOut[i++] = *psz++;
		}
		pszOut[i] = 0;

		*ppsz = psz;
	}

	static bool TokenIs(const wchar_t *pszToken, const wchar_t *pszName)
	{
		// Accept both -flag and --flag.
		if (pszToken[0] != L'-') return false;
		pszToken++;
		if (pszToken[0] == L'-') pszToken++;

		return ( _wcsicmp(pszToken, pszName) == 0 );
	}

	static void ParseWide(const wchar_t *pszCommandLine)
	{
		if (pszCommandLine == NULL) return;

		const wchar_t *psz = pszCommandLine;
		wchar_t szToken[256];

		while (*psz != 0)
		{
			ReadToken(&psz, szToken, 256);
			if (szToken[0] == 0) break;

			if (TokenIs(szToken, L"server"))
			{
				ReadToken(&psz, s_szServerAddress, 256);

				// Allow "host:port" as well as a separate -port.
				wchar_t *pchColon = wcschr(s_szServerAddress, L':');
				if (pchColon != NULL)
				{
					*pchColon = 0;
					int iPort = _wtoi(pchColon + 1);
					if (iPort > 0 && iPort <= 65535) s_iServerPort = iPort;
				}
			}
			else if (TokenIs(szToken, L"port"))
			{
				ReadToken(&psz, szToken, 256);
				int iPort = _wtoi(szToken);
				if (iPort > 0 && iPort <= 65535) s_iServerPort = iPort;
			}
			else if (TokenIs(szToken, L"name"))
			{
				ReadToken(&psz, s_szPlayerName, 64);
			}
			else if (TokenIs(szToken, L"dedicated"))
			{
				s_bDedicated = true;
			}
			else if (TokenIs(szToken, L"flashui"))
			{
				// 4J Meow - keep the original Iggy/Flash menus instead of the
				// native Screen replacements, and dump each scene's authored
				// layout to the log. Used to recover the console layout that
				// the native screens have to reproduce - the .swf gives
				// positions (see tools/swf_layout.py) but not control sizes.
				s_bFlashUI = true;
			}
			else if (TokenIs(szToken, L"world"))
			{
				ReadToken(&psz, s_szWorldName, 64);
				s_bHasWorldName = (s_szWorldName[0] != 0);
			}
			else if (TokenIs(szToken, L"seed"))
			{
				ReadToken(&psz, szToken, 256);
				s_iSeed = _wtoi64(szToken);
				s_bHasSeed = true;
			}
			else if (TokenIs(szToken, L"maxplayers"))
			{
				ReadToken(&psz, szToken, 256);
				int iMax = _wtoi(szToken);
				if (iMax > 0 && iMax <= MINECRAFT_NET_MAX_PLAYERS) s_iMaxPlayers = iMax;
			}
		}
	}

	void Parse(const TCHAR *pszCommandLine)
	{
		if (pszCommandLine == NULL) return;

		wchar_t szWide[1024];

#ifdef UNICODE
		wcsncpy_s(szWide, 1024, pszCommandLine, _TRUNCATE);
#else
		size_t converted = 0;
		if (mbstowcs_s(&converted, szWide, 1024, pszCommandLine, _TRUNCATE) != 0) return;
#endif

		ParseWide(szWide);
	}

	const wchar_t *GetServerAddress()	{ return s_szServerAddress; }
	int GetServerPort()					{ return s_iServerPort; }
	const wchar_t *GetPlayerName()		{ return s_szPlayerName; }
	bool WantsDirectConnect()			{ return ( s_szServerAddress[0] != 0 && !s_bDedicated ); }

	bool WantsDedicatedServer()			{ return s_bDedicated; }
	bool WantsFlashUI()					{ return s_bFlashUI; }	// 4J Meow
	const wchar_t *GetWorldName()		{ return s_szWorldName; }
	bool HasWorldName()					{ return s_bHasWorldName; }
	__int64 GetSeed()					{ return s_iSeed; }
	bool HasSeed()						{ return s_bHasSeed; }
	int GetMaxPlayers()					{ return s_iMaxPlayers; }
}

#endif // _WINDOWS64
