#pragma once
using namespace std;

#include "stdafx.h"

// 4J Meow - Translation between LCE's legacy numeric tile space and the namespaced
// block states used by the modern Java "Anvil" save format.
//
// LCE stores a tile as an 8-bit id plus 4 bits of metadata (see CompressedTileStorage.h),
// so the entire block universe is the 4096-entry pre-1.13 (id, meta) space. The registered
// id set is Tile.h's *_Id constants: 1-136, 139-145, 153, 155, 156 and 171 - the 1.6.4
// block set. Every one of those has a known flattened equivalent, so LCE -> Java is a
// total function with no loss.
//
// The reverse direction is inherently partial: modern Java has tens of thousands of states
// with no LCE representation. fromBlockState() returns false for anything unmapped and the
// caller decides on a substitute.

#define ANVIL_MAX_BLOCK_PROPERTIES 6

struct AnvilBlockState
{
	const wchar_t *name;								// e.g. L"minecraft:oak_log"
	const wchar_t *propKeys[ANVIL_MAX_BLOCK_PROPERTIES];
	const wchar_t *propVals[ANVIL_MAX_BLOCK_PROPERTIES];
	int propCount;

	AnvilBlockState() : name(L"minecraft:air"), propCount(0) {}

	void set(const wchar_t *n) { name = n; propCount = 0; }

	void prop(const wchar_t *k, const wchar_t *v)
	{
		if (propCount >= ANVIL_MAX_BLOCK_PROPERTIES) return;
		propKeys[propCount] = k;
		propVals[propCount] = v;
		propCount++;
	}

	// Stable identity used to deduplicate palette entries within a section.
	wstring key() const
	{
		wstring k = name;
		for (int i = 0; i < propCount; i++)
		{
			k += L'\x1f';
			k += propKeys[i];
			k += L'=';
			k += propVals[i];
		}
		return k;
	}
};

class AnvilBlockMapping
{
public:
	// LCE (id, meta) -> modern block state. Always succeeds; unregistered ids become air.
	static void toBlockState(int tileId, int metaData, AnvilBlockState &out);

	// Modern block state -> LCE (id, meta). Returns false when the state has no LCE
	// equivalent, in which case tileId/metaData are left untouched.
	static bool fromBlockState(const wstring &name, int &tileId, int &metaData);
};
