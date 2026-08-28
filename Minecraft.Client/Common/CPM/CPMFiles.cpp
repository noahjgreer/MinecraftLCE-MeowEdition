#include "stdafx.h"
#include "CPMFiles.h"

// Models live in a "cpm" folder next to the executable's working directory.
#define CPM_MODEL_FOLDER L"cpm"
// Both the exported binary model and the editor project format are
// loadable, so both are listed and both are tried when opening by name.
#define CPM_MODEL_EXT L".cpmmodel"
#define CPM_PROJECT_EXT L".cpmproject"

// A model file is small; anything this large is not one and is refused before
// it is read into memory.
#define CPM_MAX_MODEL_FILE (4 * 1024 * 1024)

std::wstring CPMGetModelFolder()
{
	return std::wstring(CPM_MODEL_FOLDER);
}

#ifdef _WINDOWS64

#include <stdio.h>

void CPMListModels(std::vector<std::wstring> &out)
{
	out.clear();

	const wchar_t *exts[2] = { CPM_MODEL_EXT, CPM_PROJECT_EXT };

	for (int e = 0; e < 2; e++)
	{
		std::wstring pattern = CPMGetModelFolder() + L"\\*" + exts[e];
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) continue;

		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			std::wstring n(fd.cFileName);
			size_t extLen = wcslen(exts[e]);
			if (n.size() > extLen) n = n.substr(0, n.size() - extLen);

			// The same base name can exist as both a project and an export.
			// One entry is enough; CPMReadModel decides which file to open.
			bool dup = false;
			for (size_t i = 0; i < out.size(); i++)
				if (out[i] == n) { dup = true; break; }
			if (!dup) out.push_back(n);
		}
		while (FindNextFileW(h, &fd));

		FindClose(h);
	}
}

bool CPMReadModel(const std::wstring &name, std::vector<unsigned char> &out)
{
	out.clear();

	// Reject anything that could escape the model folder - the name can come
	// from a chat command.
	if (name.empty()) return false;
	if (name.find(L'/') != std::wstring::npos) return false;
	if (name.find(L'\\') != std::wstring::npos) return false;
	if (name.find(L':') != std::wstring::npos) return false;
	if (name.find(L"..") != std::wstring::npos) return false;

	// Prefer an exported model over a project of the same name: it is the
	// format that also carries animations.
	const wchar_t *rexts[2] = { CPM_MODEL_EXT, CPM_PROJECT_EXT };

	FILE *f = NULL;
	for (int e = 0; e < 2 && f == NULL; e++)
	{
		std::wstring path = CPMGetModelFolder() + L"\\" + name + rexts[e];
		if (_wfopen_s(&f, path.c_str(), L"rb") != 0) f = NULL;
	}
	if (f == NULL) return false;

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (len <= 0 || len > CPM_MAX_MODEL_FILE)
	{
		fclose(f);
		return false;
	}

	out.resize((size_t)len);
	size_t rd = fread(&out[0], 1, (size_t)len, f);
	fclose(f);

	if (rd != (size_t)len)
	{
		out.clear();
		return false;
	}
	return true;
}

#else

// Consoles have no writable model folder. Models can still arrive over the
// network from another player; only local loading is unavailable.
void CPMListModels(std::vector<std::wstring> &out)
{
	out.clear();
}

bool CPMReadModel(const std::wstring &name, std::vector<unsigned char> &out)
{
	out.clear();
	return false;
}

#endif
