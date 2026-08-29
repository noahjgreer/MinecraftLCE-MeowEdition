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

	// Stable identity used to deduplicate palette entries within a section, and as the
	// lookup key for the reverse mapping.
	//
	// The properties are sorted so the key is canonical: coming back in they arrive from
	// an unordered_map inside CompoundTag, in whatever order that happens to give.
	wstring key() const
	{
		int order[ANVIL_MAX_BLOCK_PROPERTIES];
		for (int i = 0; i < propCount; i++) order[i] = i;

		for (int i = 1; i < propCount; i++)
		{
			const int held = order[i];
			int j = i - 1;

			while (j >= 0 && wcscmp(propKeys[order[j]], propKeys[held]) > 0)
			{
				order[j + 1] = order[j];
				j--;
			}
			order[j + 1] = held;
		}

		wstring k = name;
		for (int i = 0; i < propCount; i++)
		{
			k += L'\x1f';
			k += propKeys[order[i]];
			k += L'=';
			k += propVals[order[i]];
		}
		return k;
	}
};

// One property of a block state, as read back out of saved NBT.
struct AnvilBlockProperty
{
	wstring key;
	wstring value;
};

class AnvilBlockMapping
{
public:
	// LCE (id, meta) -> modern block state. Always succeeds; unregistered ids become air.
	static void toBlockState(int tileId, int metaData, AnvilBlockState &out);

	// Modern block state -> LCE (id, meta). Returns false when the state has no LCE
	// equivalent, in which case tileId/metaData are left untouched.
	//
	// The name-only form cannot recover metadata that lives in the properties - every
	// oak_stairs resolves to the same facing - so it is only a fallback. Prefer the
	// overload below wherever the properties are available.
	static bool fromBlockState(const wstring &name, int &tileId, int &metaData);

	// As above, but matches the full state. Falls back to the name alone when the exact
	// combination is not one LCE can produce, so a Java-authored state with unfamiliar
	// properties still resolves to the right block.
	static bool fromBlockState(const wstring &name, const vector<AnvilBlockProperty> &properties,
	                           int &tileId, int &metaData);

	// Canonical key for a name plus properties, matching AnvilBlockState::key().
	static wstring stateKey(const wstring &name, const vector<AnvilBlockProperty> &properties);
};
