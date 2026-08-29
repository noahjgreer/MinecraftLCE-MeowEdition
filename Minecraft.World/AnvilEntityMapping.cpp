#include "stdafx.h"

#ifdef _WINDOWS64

#include "AnvilEntityMapping.h"
#include "AnvilItemMapping.h"

#include "CompoundTag.h"
#include "ListTag.h"
#include "IntArrayTag.h"

// 4J Meow - see AnvilEntityMapping.h.

const wchar_t *AnvilEntityMapping::toEntityId(const wstring &legacyId, CompoundTag *entityTag)
{
	// LCE keeps one "Minecart" entity with a Type field; Java has a separate entity per
	// variant. Type 0 is rideable, 1 chest, 2 furnace - the only three LCE builds.
	if (legacyId == L"Minecart")
	{
		const int type = entityTag != NULL && entityTag->contains(L"Type") ? entityTag->getInt(L"Type") : 0;
		if (type == 1) return L"minecraft:chest_minecart";
		if (type == 2) return L"minecraft:furnace_minecart";
		return L"minecraft:minecart";
	}

	if (legacyId == L"Item")             return L"minecraft:item";
	if (legacyId == L"XPOrb")            return L"minecraft:experience_orb";
	if (legacyId == L"Painting")         return L"minecraft:painting";
	if (legacyId == L"Arrow")            return L"minecraft:arrow";
	if (legacyId == L"Snowball")         return L"minecraft:snowball";
	if (legacyId == L"Fireball")         return L"minecraft:fireball";
	if (legacyId == L"SmallFireball")    return L"minecraft:small_fireball";
	if (legacyId == L"ThrownEnderpearl") return L"minecraft:ender_pearl";
	if (legacyId == L"EyeOfEnderSignal") return L"minecraft:eye_of_ender";
	if (legacyId == L"ThrownPotion")     return L"minecraft:splash_potion";
	if (legacyId == L"ThrownExpBottle")  return L"minecraft:experience_bottle";
	if (legacyId == L"ItemFrame")        return L"minecraft:item_frame";
	if (legacyId == L"PrimedTnt")        return L"minecraft:tnt";
	if (legacyId == L"FallingSand")      return L"minecraft:falling_block";
	if (legacyId == L"Boat")             return L"minecraft:oak_boat";
	if (legacyId == L"Creeper")          return L"minecraft:creeper";
	if (legacyId == L"Skeleton")         return L"minecraft:skeleton";
	if (legacyId == L"Spider")           return L"minecraft:spider";
	if (legacyId == L"Giant")            return L"minecraft:giant";
	if (legacyId == L"Zombie")           return L"minecraft:zombie";
	if (legacyId == L"Slime")            return L"minecraft:slime";
	if (legacyId == L"Ghast")            return L"minecraft:ghast";
	if (legacyId == L"PigZombie")        return L"minecraft:zombified_piglin";
	if (legacyId == L"Enderman")         return L"minecraft:enderman";
	if (legacyId == L"CaveSpider")       return L"minecraft:cave_spider";
	if (legacyId == L"Silverfish")       return L"minecraft:silverfish";
	if (legacyId == L"Blaze")            return L"minecraft:blaze";
	if (legacyId == L"LavaSlime")        return L"minecraft:magma_cube";
	if (legacyId == L"EnderDragon")      return L"minecraft:ender_dragon";
	if (legacyId == L"Pig")              return L"minecraft:pig";
	if (legacyId == L"Sheep")            return L"minecraft:sheep";
	if (legacyId == L"Cow")              return L"minecraft:cow";
	if (legacyId == L"Chicken")          return L"minecraft:chicken";
	if (legacyId == L"Squid")            return L"minecraft:squid";
	if (legacyId == L"Wolf")             return L"minecraft:wolf";
	if (legacyId == L"MushroomCow")      return L"minecraft:mooshroom";
	if (legacyId == L"SnowMan")          return L"minecraft:snow_golem";
	if (legacyId == L"Ozelot")           return L"minecraft:ocelot";
	if (legacyId == L"VillagerGolem")    return L"minecraft:iron_golem";
	if (legacyId == L"Villager")         return L"minecraft:villager";
	if (legacyId == L"EnderCrystal")     return L"minecraft:end_crystal";
	if (legacyId == L"DragonFireball")   return L"minecraft:dragon_fireball";

	// "Mob" and "Monster" are abstract base registrations that never appear in a save,
	// and anything else is not something LCE writes.
	return NULL;
}

// Java identifies every entity by a 128-bit UUID stored as four ints. LCE has no such
// field, so one is synthesised from the entity's position and id. This is deterministic,
// which matters: re-saving the same world must not mint new UUIDs every time and leave
// Java thinking the entities are duplicates.
static void ensureUuid(CompoundTag *entityTag, const wchar_t *modernId)
{
	if (entityTag->contains(L"UUID")) return;

	unsigned int hash = 2166136261u;

	const wchar_t *p = modernId;
	while (*p != 0) { hash = (hash ^ (unsigned int)*p) * 16777619u; p++; }

	if (entityTag->contains(L"Pos"))
	{
		ListTag<Tag> *pos = entityTag->getList(L"Pos");
		if (pos != NULL && pos->size() >= 3)
		{
			for (int i = 0; i < 3; i++)
			{
				// Hash the raw bits so two entities a fraction apart still differ.
				const double value = ((DoubleTag *)pos->get(i))->data;
				const unsigned __int64 bits = *(const unsigned __int64 *)&value;

				hash = (hash ^ (unsigned int)(bits & 0xffffffff)) * 16777619u;
				hash = (hash ^ (unsigned int)(bits >> 32)) * 16777619u;
			}
		}
	}

	intArray uuid(4);
	for (int i = 0; i < 4; i++)
	{
		hash = (hash ^ (unsigned int)(i + 1)) * 16777619u;
		uuid[i] = (int)hash;
	}

	entityTag->putIntArray(L"UUID", uuid);
}

bool AnvilEntityMapping::convertEntity(CompoundTag *entityTag)
{
	if (entityTag == NULL || !entityTag->contains(L"id")) return false;

	const wstring legacyId = entityTag->getString(L"id");

	const wchar_t *modernId = toEntityId(legacyId, entityTag);
	if (modernId == NULL) return false;

	entityTag->putString(L"id", modernId);

	// Inventories carried by the entity - minecart chests, and the mob equipment lists.
	AnvilItemMapping::convertItemList(entityTag, L"Items");
	AnvilItemMapping::convertItemList(entityTag, L"Inventory");

	// A dropped item entity holds one stack rather than a list.
	if (entityTag->contains(L"Item"))
	{
		CompoundTag *item = entityTag->getCompound(L"Item");
		if (!AnvilItemMapping::convertItemStack(item)) return false;
	}

	ensureUuid(entityTag, modernId);
	return true;
}

// Derived from toEntityId() so the two directions cannot drift. Minecarts collapse onto
// the single legacy "Minecart" id; convertEntityFromJava restores the Type field.
const wchar_t *AnvilEntityMapping::toLegacyEntityId(const wstring &modernId)
{
	static const wchar_t *legacy[] =
	{
		L"Item", L"XPOrb", L"Painting", L"Arrow", L"Snowball", L"Fireball", L"SmallFireball",
		L"ThrownEnderpearl", L"EyeOfEnderSignal", L"ThrownPotion", L"ThrownExpBottle",
		L"ItemFrame", L"PrimedTnt", L"FallingSand", L"Boat", L"Creeper", L"Skeleton",
		L"Spider", L"Giant", L"Zombie", L"Slime", L"Ghast", L"PigZombie", L"Enderman",
		L"CaveSpider", L"Silverfish", L"Blaze", L"LavaSlime", L"EnderDragon", L"Pig",
		L"Sheep", L"Cow", L"Chicken", L"Squid", L"Wolf", L"MushroomCow", L"SnowMan",
		L"Ozelot", L"VillagerGolem", L"Villager", L"EnderCrystal", L"DragonFireball"
	};

	for (int i = 0; i < (int)(sizeof(legacy) / sizeof(legacy[0])); i++)
	{
		const wchar_t *modern = toEntityId(legacy[i], NULL);
		if (modern != NULL && modernId == modern) return legacy[i];
	}

	if (modernId == L"minecraft:minecart" ||
	    modernId == L"minecraft:chest_minecart" ||
	    modernId == L"minecraft:furnace_minecart") return L"Minecart";

	return NULL;
}

bool AnvilEntityMapping::convertEntityFromJava(CompoundTag *entityTag)
{
	if (entityTag == NULL || !entityTag->contains(L"id")) return false;

	const wstring modernId = entityTag->getString(L"id");

	const wchar_t *legacyId = toLegacyEntityId(modernId);
	if (legacyId == NULL) return false;

	// The minecart variant went into the id on the way out; put it back in Type.
	if (wcscmp(legacyId, L"Minecart") == 0)
	{
		int type = 0;
		if (modernId == L"minecraft:chest_minecart") type = 1;
		else if (modernId == L"minecraft:furnace_minecart") type = 2;

		entityTag->putInt(L"Type", type);
	}

	entityTag->putString(L"id", legacyId);

	AnvilItemMapping::convertItemListFromJava(entityTag, L"Items");
	AnvilItemMapping::convertItemListFromJava(entityTag, L"Inventory");

	if (entityTag->contains(L"Item"))
	{
		CompoundTag *item = entityTag->getCompound(L"Item");
		if (!AnvilItemMapping::fromItemStack(item)) return false;
	}

	// The UUID was synthesised on the way out and means nothing to LCE.
	if (entityTag->contains(L"UUID"))
	{
		Tag *uuid = entityTag->get(L"UUID");
		entityTag->remove(L"UUID");
		delete uuid;
	}

	return true;
}

#endif // _WINDOWS64
