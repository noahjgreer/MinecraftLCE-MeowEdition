#pragma once

#ifdef _WINDOWS64

using namespace std;

#include "stdafx.h"

class CompoundTag;

// 4J Meow - Translation of LCE's saved entity NBT into the modern Java form.
//
// LCE writes the pre-1.11 save ids ("Creeper", "PigZombie", "LavaSlime") from
// EntityIO.cpp. Modern Java uses namespaced ids and keeps entities in their own
// entities/*.mca region rather than inside the chunk.
//
// Beyond the id, three things have to change: item stacks inside an entity go through
// AnvilItemMapping, entities that Java split by variant (minecarts) pick their id from
// a field, and a UUID is required where LCE wrote none.

class AnvilEntityMapping
{
public:
	// Legacy save id -> modern namespaced id. Returns NULL when the entity has no modern
	// counterpart, or is one of LCE's abstract base registrations ("Mob", "Monster").
	static const wchar_t *toEntityId(const wstring &legacyId, CompoundTag *entityTag);

	// Rewrites a saved entity compound in place. Returns false when the entity cannot be
	// represented and should be dropped.
	static bool convertEntity(CompoundTag *entityTag);

	// The reverse pair, for reading a world back in: EntityIO::loadStatic matches on the
	// legacy save id, so a namespaced one loads nothing at all.
	static const wchar_t *toLegacyEntityId(const wstring &modernId);
	static bool convertEntityFromJava(CompoundTag *entityTag);
};

#endif // _WINDOWS64
