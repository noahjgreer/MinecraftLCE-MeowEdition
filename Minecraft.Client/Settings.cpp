#include "stdafx.h"
#include "Settings.h"
#include "..\Minecraft.World\File.h"
#include "..\Minecraft.World\StringHelpers.h"
#include <stdio.h>

// 4J Meow - Implemented. This was "4J - TODO - serialise/deserialise from file"
// with an empty constructor and an empty saveProperties(), so server.properties
// was constructed and then never read: every getString/getInt/getBoolean fell
// through to its default and quietly wrote the default back into the map.
//
// MinecraftServer::initServer reads level-name, gamemode, level-type,
// max-build-height, spawn-animals and spawn-npcs from here, so with the stub in
// place a dedicated server always loaded the level called "world" no matter what
// it was told. See docs/systems/dedicated-server.md.
//
// Format is the Java server's: "key=value" per line, '#' or '!' starts a comment,
// blank lines ignored, whitespace around key and value trimmed.

Settings::Settings(File *file)
{
	if (file == NULL) return;

	m_filename = file->getPath();
	if (m_filename.empty()) return;

	char szPath[512];
	size_t converted = 0;
	if (wcstombs_s(&converted, szPath, sizeof(szPath), m_filename.c_str(), _TRUNCATE) != 0) return;

	FILE *pFile = NULL;
	if (fopen_s(&pFile, szPath, "r") != 0 || pFile == NULL)
	{
		// Not an error - a missing file just means every default applies, which
		// is what generateNewProperties would have produced anyway.
		generateNewProperties();
		return;
	}

	char szLine[1024];
	while (fgets(szLine, sizeof(szLine), pFile) != NULL)
	{
		char *pchStart = szLine;
		while (*pchStart == ' ' || *pchStart == '\t') pchStart++;

		if (*pchStart == 0 || *pchStart == '\n' || *pchStart == '\r') continue;
		if (*pchStart == '#' || *pchStart == '!') continue;

		char *pchEquals = strchr(pchStart, '=');
		if (pchEquals == NULL) continue;

		*pchEquals = 0;
		char *pchValue = pchEquals + 1;

		// Trim the trailing whitespace off the key...
		char *pchKeyEnd = pchEquals - 1;
		while (pchKeyEnd >= pchStart && (*pchKeyEnd == ' ' || *pchKeyEnd == '\t')) *pchKeyEnd-- = 0;

		// ...and both ends off the value.
		while (*pchValue == ' ' || *pchValue == '\t') pchValue++;
		char *pchValueEnd = pchValue + strlen(pchValue) - 1;
		while (pchValueEnd >= pchValue &&
			(*pchValueEnd == '\n' || *pchValueEnd == '\r' || *pchValueEnd == ' ' || *pchValueEnd == '\t'))
		{
			*pchValueEnd-- = 0;
		}

		if (*pchStart == 0) continue;

		properties[convStringToWstring(pchStart)] = convStringToWstring(pchValue);
	}

	fclose(pFile);
}

void Settings::generateNewProperties()
{
	// Nothing to seed - every getter supplies its own default and records it, so
	// the first saveProperties() writes a complete file.
}

void Settings::saveProperties()
{
	if (m_filename.empty()) return;

	char szPath[512];
	size_t converted = 0;
	if (wcstombs_s(&converted, szPath, sizeof(szPath), m_filename.c_str(), _TRUNCATE) != 0) return;

	FILE *pFile = NULL;
	if (fopen_s(&pFile, szPath, "w") != 0 || pFile == NULL) return;

	fprintf(pFile, "#Minecraft server properties\n");

	for (AUTO_VAR(it, properties.begin()); it != properties.end(); ++it)
	{
		char szKey[256];
		char szValue[512];
		if (wcstombs_s(&converted, szKey, sizeof(szKey), it->first.c_str(), _TRUNCATE) != 0) continue;
		if (wcstombs_s(&converted, szValue, sizeof(szValue), it->second.c_str(), _TRUNCATE) != 0) continue;

		fprintf(pFile, "%s=%s\n", szKey, szValue);
	}

	fclose(pFile);
}

wstring Settings::getString(const wstring& key, const wstring& defaultValue)
{
	if(properties.find(key) == properties.end())
	{
		properties[key] = defaultValue;
		saveProperties();
	}
	return properties[key];
}

int Settings::getInt(const wstring& key, int defaultValue)
{
	if(properties.find(key) == properties.end())
	{
		properties[key] = _toString<int>(defaultValue);
		saveProperties();
	}
	return _fromString<int>(properties[key]);
}

bool Settings::getBoolean(const wstring& key, bool defaultValue)
{
	if(properties.find(key) == properties.end())
	{
		properties[key] = _toString<bool>(defaultValue);
		saveProperties();
	}
	MemSect(35);
	// _fromString<bool> reads "0"/"1"; the file convention is true/false, so
	// accept both rather than silently reading every "true" as false.
	wstring value = properties[key];
	bool retval;
	if (value == L"true" || value == L"TRUE" || value == L"True")		retval = true;
	else if (value == L"false" || value == L"FALSE" || value == L"False")	retval = false;
	else																retval = _fromString<bool>(value);
	MemSect(0);
	return retval;
}

void Settings::setBooleanAndSave(const wstring& key, bool value)
{
	properties[key] = value ? L"true" : L"false";
	saveProperties();
}
