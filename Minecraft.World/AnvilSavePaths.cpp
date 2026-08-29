#include "stdafx.h"

#ifdef _WINDOWS64

#include "AnvilSavePaths.h"

// 4J Meow - see AnvilSavePaths.h.

namespace AnvilSavePaths
{
	// Empty until first use, then resolved to <executable directory>\\saves. An explicit
	// setSavesRoot() call overrides it.
	static wstring s_savesRoot;

	static wstring executableDirectory();

	void log(const char *format, ...)
	{
		char buffer[1024];

		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);

		OutputDebugStringA(buffer);

		// Beside the executable, for the same reason the saves are: the working directory
		// depends on how the game was launched.
		const wstring path = executableDirectory() + L"\\anvil.log";

		FILE *file = _wfopen(path.c_str(), L"a");
		if (file != NULL)
		{
			fputs(buffer, file);
			fclose(file);
		}
	}

	// The directory holding Minecraft.Client.exe.
	//
	// Note this is the *executable's* directory, not the process working directory:
	// those differ when the game is launched from a debugger or a shortcut with its own
	// "start in" folder, and worlds should not move about depending on how it was
	// started. GetCurrentDirectory() would give the latter.
	static wstring executableDirectory()
	{
		// GetModuleFileNameW truncates rather than failing, so grow until it fits.
		vector<wchar_t> buffer(MAX_PATH);

		for (;;)
		{
			const DWORD length = GetModuleFileNameW(NULL, &buffer[0], (DWORD)buffer.size());
			if (length == 0) return L".";

			if (length < buffer.size() - 1)
			{
				const wstring path(&buffer[0], length);

				const size_t separator = path.find_last_of(L'\\');
				if (separator == wstring::npos) return L".";

				return path.substr(0, separator);
			}

			if (buffer.size() > 32768) return L".";
			buffer.resize(buffer.size() * 2);
		}
	}

	static wstring stripTrailingSeparators(const wstring &path)
	{
		wstring out = path;
		while (!out.empty() && (out[out.length() - 1] == L'\\' || out[out.length() - 1] == L'/'))
		{
			out = out.substr(0, out.length() - 1);
		}
		return out;
	}

	void setSavesRoot(const wstring &root)
	{
		// An empty root would put every world at the current directory's top level and
		// let them collide with each other, so ignore it.
		if (root.empty()) return;
		s_savesRoot = stripTrailingSeparators(root);

		wchar_t cwd[MAX_PATH];
		GetCurrentDirectoryW(MAX_PATH, cwd);
		log("[anvil] saves root set to \"%ls\" (cwd \"%ls\")\n", s_savesRoot.c_str(), cwd);
	}

	const wstring &getSavesRoot()
	{
		if (s_savesRoot.empty())
		{
			s_savesRoot = executableDirectory() + L"\\saves";

			wchar_t cwd[MAX_PATH];
			GetCurrentDirectoryW(MAX_PATH, cwd);
			log("[anvil] saves root defaulted to \"%ls\" (cwd \"%ls\")\n", s_savesRoot.c_str(), cwd);
		}

		return s_savesRoot;
	}

	wstring worldDirectory(const wstring &levelId)
	{
		// A world with no name would be the saves root itself, which would then be
		// scanned as if it were a world. Give it the same fallback the blob backend uses.
		const wstring name = levelId.empty() ? wstring(L"world") : levelId;

		const wstring resolved = getSavesRoot() + L"\\" + name;
		log("[anvil] worldDirectory(\"%ls\") -> \"%ls\"\n", levelId.c_str(), resolved.c_str());
		return resolved;
	}

	static wstring s_currentWorld;

	void setCurrentWorld(const wstring &name)
	{
		s_currentWorld = name;
		log("[anvil] current world set to \"%ls\"\n", s_currentWorld.c_str());
	}

	const wstring &getCurrentWorld()
	{
		return s_currentWorld;
	}

	wstring makeUniqueWorldName(const wstring &displayName)
	{
		// Strip the characters Windows will not accept in a directory name, plus any
		// leading or trailing dots and spaces, which Windows also silently mangles.
		wstring cleaned;

		for (unsigned int i = 0; i < displayName.length(); i++)
		{
			const wchar_t c = displayName[i];

			if (c < 32) continue;
			if (c == L'\\' || c == L'/' || c == L':' || c == L'*' ||
			    c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') continue;

			cleaned += c;
		}

		while (!cleaned.empty() && (cleaned[0] == L' ' || cleaned[0] == L'.'))
		{
			cleaned = cleaned.substr(1);
		}
		while (!cleaned.empty() &&
		       (cleaned[cleaned.length() - 1] == L' ' || cleaned[cleaned.length() - 1] == L'.'))
		{
			cleaned = cleaned.substr(0, cleaned.length() - 1);
		}

		if (cleaned.empty()) cleaned = L"World";

		// Reserved DOS device names are rejected as directory names too.
		static const wchar_t *reserved[] =
		{
			L"CON", L"PRN", L"AUX", L"NUL",
			L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
			L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"
		};

		for (int i = 0; i < (int)(sizeof(reserved) / sizeof(reserved[0])); i++)
		{
			if (_wcsicmp(cleaned.c_str(), reserved[i]) == 0)
			{
				cleaned += L"_";
				break;
			}
		}

		// "My World", then "My World (2)", "My World (3)", ... until one is free.
		wstring candidate = cleaned;

		for (int suffix = 2; suffix < 1000; suffix++)
		{
			const wstring path = getSavesRoot() + L"\\" + candidate;
			if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return candidate;

			wchar_t buffer[16];
			swprintf(buffer, 16, L" (%d)", suffix);
			candidate = cleaned + buffer;
		}

		return candidate;
	}

	vector<wstring> listWorlds()
	{
		vector<wstring> worlds;

		const wstring pattern = getSavesRoot() + L"\\*";

		WIN32_FIND_DATAW found;
		HANDLE search = FindFirstFileW(pattern.c_str(), &found);
		if (search == INVALID_HANDLE_VALUE) return worlds;

		do
		{
			if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;

			const wstring name = found.cFileName;
			if (name == L"." || name == L"..") continue;

			// A directory only counts as a world if it actually holds a level.dat;
			// otherwise stray folders would show up in the world list.
			const wstring levelDat = getSavesRoot() + L"\\" + name + L"\\level.dat";
			const DWORD attributes = GetFileAttributesW(levelDat.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES) continue;

			worlds.push_back(name);
		}
		while (FindNextFileW(search, &found));

		FindClose(search);
		return worlds;
	}
}

#endif // _WINDOWS64
