#pragma once

#ifdef _WINDOWS64

using namespace std;

#include "stdafx.h"

class CompoundTag;

// 4J Meow - Translation of LCE's numeric item ids into modern namespaced item ids.
//
// LCE stores an item stack the pre-1.13 way: a numeric id plus a damage/aux value, where
// the aux doubles as both durability (for tools) and a variant selector (dyes, potions,
// spawn eggs, records...). Modern Java has a distinct item id per variant and keeps
// durability in components, so the split has to be made explicitly here.
//
// Ids below 256 are tile ids and resolve through AnvilBlockMapping's block table instead;
// the item form of a block is named after the block. Ids 256-406 are LCE's items and
// 2256-2267 its music discs.

class AnvilItemMapping
{
public:
	// Returns the modern item id for an LCE (id, aux) pair, or NULL when there is no
	// modern equivalent. `aux` is ignored for items whose aux is durability.
	static const wchar_t *toItemId(int itemId, int aux);

	// Rewrites one saved ItemInstance compound in place: replaces the numeric `id` with
	// a modern string id, drops `Damage` where it was a variant selector rather than
	// durability, and recurses into any nested inventory. Returns false when the item
	// has no modern equivalent, in which case the caller should drop the stack.
	static bool convertItemStack(CompoundTag *tag);

	// Rewrites every stack in a block entity or entity's item list ("Items", "Inventory",
	// "HandItems", ...), dropping the ones that cannot be represented.
	static void convertItemList(CompoundTag *owner, const wchar_t *listName);

	// The reverse of the two above, used when the game reads back its own saves. Without
	// these the loader would find a string where ItemInstance::load expects a short.
	static bool fromItemStack(CompoundTag *tag);
	static void convertItemListFromJava(CompoundTag *owner, const wchar_t *listName);
};

#endif // _WINDOWS64
