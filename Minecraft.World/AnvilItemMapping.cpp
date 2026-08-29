#include "stdafx.h"

#ifdef _WINDOWS64

#include "AnvilItemMapping.h"
#include "AnvilBlockMapping.h"

#include "CompoundTag.h"
#include "ListTag.h"
#include "Item.h"

// 4J Meow - see AnvilItemMapping.h.

// Legacy dye order (Item::dye_powder aux). This is the *reverse* of the wool order:
// aux 0 is ink sac and aux 15 is bone meal.
static const wchar_t *DYES[16] =
{
	L"minecraft:ink_sac",        L"minecraft:red_dye",     L"minecraft:green_dye",  L"minecraft:cocoa_beans",
	L"minecraft:lapis_lazuli",   L"minecraft:purple_dye",  L"minecraft:cyan_dye",   L"minecraft:light_gray_dye",
	L"minecraft:gray_dye",       L"minecraft:pink_dye",    L"minecraft:lime_dye",   L"minecraft:yellow_dye",
	L"minecraft:light_blue_dye", L"minecraft:magenta_dye", L"minecraft:orange_dye", L"minecraft:bone_meal"
};

// Music discs in LCE id order 2256..2267. Disc 8 ("wait") was added out of sequence and
// sits at the end rather than between 07 and 09, matching the *_Id constants.
static const wchar_t *RECORDS[12] =
{
	L"minecraft:music_disc_13",      L"minecraft:music_disc_cat",  L"minecraft:music_disc_blocks",
	L"minecraft:music_disc_chirp",   L"minecraft:music_disc_far",  L"minecraft:music_disc_mall",
	L"minecraft:music_disc_mellohi", L"minecraft:music_disc_stal", L"minecraft:music_disc_strad",
	L"minecraft:music_disc_ward",    L"minecraft:music_disc_11",   L"minecraft:music_disc_wait"
};

// Skull variant lived in the aux value pre-flattening.
static const wchar_t *SKULLS[6] =
{
	L"minecraft:skeleton_skull", L"minecraft:wither_skeleton_skull",
	L"minecraft:zombie_head",    L"minecraft:player_head",
	L"minecraft:creeper_head",   L"minecraft:dragon_head"
};

// Spawn eggs carry the target entity id in their aux value.
static const wchar_t *spawnEgg(int entityId)
{
	switch (entityId)
	{
	case 50: return L"minecraft:creeper_spawn_egg";
	case 51: return L"minecraft:skeleton_spawn_egg";
	case 52: return L"minecraft:spider_spawn_egg";
	case 54: return L"minecraft:zombie_spawn_egg";
	case 55: return L"minecraft:slime_spawn_egg";
	case 56: return L"minecraft:ghast_spawn_egg";
	case 57: return L"minecraft:zombified_piglin_spawn_egg";
	case 58: return L"minecraft:enderman_spawn_egg";
	case 59: return L"minecraft:cave_spider_spawn_egg";
	case 60: return L"minecraft:silverfish_spawn_egg";
	case 61: return L"minecraft:blaze_spawn_egg";
	case 62: return L"minecraft:magma_cube_spawn_egg";
	case 65: return L"minecraft:bat_spawn_egg";
	case 66: return L"minecraft:witch_spawn_egg";
	case 90: return L"minecraft:pig_spawn_egg";
	case 91: return L"minecraft:sheep_spawn_egg";
	case 92: return L"minecraft:cow_spawn_egg";
	case 93: return L"minecraft:chicken_spawn_egg";
	case 94: return L"minecraft:squid_spawn_egg";
	case 95: return L"minecraft:wolf_spawn_egg";
	case 96: return L"minecraft:mooshroom_spawn_egg";
	case 98: return L"minecraft:ocelot_spawn_egg";
	case 120: return L"minecraft:villager_spawn_egg";
	default: return NULL;
	}
}

const wchar_t *AnvilItemMapping::toItemId(int itemId, int aux)
{
	// Block items take the name of the block they place. AnvilBlockMapping stays the
	// single source of truth for that, so there is no second table to keep in step.
	if (itemId > 0 && itemId < 256)
	{
		AnvilBlockState state;
		AnvilBlockMapping::toBlockState(itemId, aux, state);

		// Air means the id is not a block LCE registers; a stack of it is meaningless.
		if (wcscmp(state.name, L"minecraft:air") == 0) return NULL;
		return state.name;
	}

	if (itemId >= 2256 && itemId <= 2267) return RECORDS[itemId - 2256];

	switch (itemId)
	{
	case Item::shovel_iron_Id:      return L"minecraft:iron_shovel";
	case Item::pickAxe_iron_Id:     return L"minecraft:iron_pickaxe";
	case Item::hatchet_iron_Id:     return L"minecraft:iron_axe";
	case Item::flintAndSteel_Id:    return L"minecraft:flint_and_steel";
	case Item::apple_Id:            return L"minecraft:apple";
	case Item::bow_Id:              return L"minecraft:bow";
	case Item::arrow_Id:            return L"minecraft:arrow";
	case Item::coal_Id:             return aux == 1 ? L"minecraft:charcoal" : L"minecraft:coal";
	case Item::diamond_Id:          return L"minecraft:diamond";
	case Item::ironIngot_Id:        return L"minecraft:iron_ingot";
	case Item::goldIngot_Id:        return L"minecraft:gold_ingot";
	case Item::sword_iron_Id:       return L"minecraft:iron_sword";
	case Item::sword_wood_Id:       return L"minecraft:wooden_sword";
	case Item::shovel_wood_Id:      return L"minecraft:wooden_shovel";
	case Item::pickAxe_wood_Id:     return L"minecraft:wooden_pickaxe";
	case Item::hatchet_wood_Id:     return L"minecraft:wooden_axe";
	case Item::sword_stone_Id:      return L"minecraft:stone_sword";
	case Item::shovel_stone_Id:     return L"minecraft:stone_shovel";
	case Item::pickAxe_stone_Id:    return L"minecraft:stone_pickaxe";
	case Item::hatchet_stone_Id:    return L"minecraft:stone_axe";
	case Item::sword_diamond_Id:    return L"minecraft:diamond_sword";
	case Item::shovel_diamond_Id:   return L"minecraft:diamond_shovel";
	case Item::pickAxe_diamond_Id:  return L"minecraft:diamond_pickaxe";
	case Item::hatchet_diamond_Id:  return L"minecraft:diamond_axe";
	case Item::stick_Id:            return L"minecraft:stick";
	case Item::bowl_Id:             return L"minecraft:bowl";
	case Item::mushroomStew_Id:     return L"minecraft:mushroom_stew";
	case Item::sword_gold_Id:       return L"minecraft:golden_sword";
	case Item::shovel_gold_Id:      return L"minecraft:golden_shovel";
	case Item::pickAxe_gold_Id:     return L"minecraft:golden_pickaxe";
	case Item::hatchet_gold_Id:     return L"minecraft:golden_axe";
	case Item::string_Id:           return L"minecraft:string";
	case Item::feather_Id:          return L"minecraft:feather";
	case Item::sulphur_Id:          return L"minecraft:gunpowder";
	case Item::hoe_wood_Id:         return L"minecraft:wooden_hoe";
	case Item::hoe_stone_Id:        return L"minecraft:stone_hoe";
	case Item::hoe_iron_Id:         return L"minecraft:iron_hoe";
	case Item::hoe_diamond_Id:      return L"minecraft:diamond_hoe";
	case Item::hoe_gold_Id:         return L"minecraft:golden_hoe";
	case Item::seeds_wheat_Id:      return L"minecraft:wheat_seeds";
	case Item::wheat_Id:            return L"minecraft:wheat";
	case Item::bread_Id:            return L"minecraft:bread";
	case Item::helmet_cloth_Id:     return L"minecraft:leather_helmet";
	case Item::chestplate_cloth_Id: return L"minecraft:leather_chestplate";
	case Item::leggings_cloth_Id:   return L"minecraft:leather_leggings";
	case Item::boots_cloth_Id:      return L"minecraft:leather_boots";
	case Item::helmet_chain_Id:     return L"minecraft:chainmail_helmet";
	case Item::chestplate_chain_Id: return L"minecraft:chainmail_chestplate";
	case Item::leggings_chain_Id:   return L"minecraft:chainmail_leggings";
	case Item::boots_chain_Id:      return L"minecraft:chainmail_boots";
	case Item::helmet_iron_Id:      return L"minecraft:iron_helmet";
	case Item::chestplate_iron_Id:  return L"minecraft:iron_chestplate";
	case Item::leggings_iron_Id:    return L"minecraft:iron_leggings";
	case Item::boots_iron_Id:       return L"minecraft:iron_boots";
	case Item::helmet_diamond_Id:     return L"minecraft:diamond_helmet";
	case Item::chestplate_diamond_Id: return L"minecraft:diamond_chestplate";
	case Item::leggings_diamond_Id:   return L"minecraft:diamond_leggings";
	case Item::boots_diamond_Id:      return L"minecraft:diamond_boots";
	case Item::helmet_gold_Id:      return L"minecraft:golden_helmet";
	case Item::chestplate_gold_Id:  return L"minecraft:golden_chestplate";
	case Item::leggings_gold_Id:    return L"minecraft:golden_leggings";
	case Item::boots_gold_Id:       return L"minecraft:golden_boots";
	case Item::flint_Id:            return L"minecraft:flint";
	case Item::porkChop_raw_Id:     return L"minecraft:porkchop";
	case Item::porkChop_cooked_Id:  return L"minecraft:cooked_porkchop";
	case Item::painting_Id:         return L"minecraft:painting";
	// Aux 1 was the enchanted golden apple, a separate item now.
	case Item::apple_gold_Id:       return aux == 1 ? L"minecraft:enchanted_golden_apple" : L"minecraft:golden_apple";
	case Item::sign_Id:             return L"minecraft:oak_sign";
	case Item::door_wood_Id:        return L"minecraft:oak_door";
	case Item::bucket_empty_Id:     return L"minecraft:bucket";
	case Item::bucket_water_Id:     return L"minecraft:water_bucket";
	case Item::bucket_lava_Id:      return L"minecraft:lava_bucket";
	case Item::minecart_Id:         return L"minecraft:minecart";
	case Item::saddle_Id:           return L"minecraft:saddle";
	case Item::door_iron_Id:        return L"minecraft:iron_door";
	case Item::redStone_Id:         return L"minecraft:redstone";
	case Item::snowBall_Id:         return L"minecraft:snowball";
	case Item::boat_Id:             return L"minecraft:oak_boat";
	case Item::leather_Id:          return L"minecraft:leather";
	case Item::milk_Id:             return L"minecraft:milk_bucket";
	case Item::brick_Id:            return L"minecraft:brick";
	case Item::clay_Id:             return L"minecraft:clay_ball";
	case Item::reeds_Id:            return L"minecraft:sugar_cane";
	case Item::paper_Id:            return L"minecraft:paper";
	case Item::book_Id:             return L"minecraft:book";
	case Item::slimeBall_Id:        return L"minecraft:slime_ball";
	case Item::minecart_chest_Id:   return L"minecraft:chest_minecart";
	case Item::minecart_furnace_Id: return L"minecraft:furnace_minecart";
	case Item::egg_Id:              return L"minecraft:egg";
	case Item::compass_Id:          return L"minecraft:compass";
	case Item::fishingRod_Id:       return L"minecraft:fishing_rod";
	case Item::clock_Id:            return L"minecraft:clock";
	case Item::yellowDust_Id:       return L"minecraft:glowstone_dust";
	case Item::fish_raw_Id:         return L"minecraft:cod";
	case Item::fish_cooked_Id:      return L"minecraft:cooked_cod";
	case Item::dye_powder_Id:       return DYES[aux & 15];
	case Item::bone_Id:             return L"minecraft:bone";
	case Item::sugar_Id:            return L"minecraft:sugar";
	case Item::cake_Id:             return L"minecraft:cake";
	case Item::bed_Id:              return L"minecraft:red_bed";
	case Item::diode_Id:            return L"minecraft:repeater";
	case Item::cookie_Id:           return L"minecraft:cookie";
	case Item::map_Id:              return L"minecraft:filled_map";
	case Item::shears_Id:           return L"minecraft:shears";
	case Item::melon_Id:            return L"minecraft:melon_slice";
	case Item::seeds_pumpkin_Id:    return L"minecraft:pumpkin_seeds";
	case Item::seeds_melon_Id:      return L"minecraft:melon_seeds";
	case Item::beef_raw_Id:         return L"minecraft:beef";
	case Item::beef_cooked_Id:      return L"minecraft:cooked_beef";
	case Item::chicken_raw_Id:      return L"minecraft:chicken";
	case Item::chicken_cooked_Id:   return L"minecraft:cooked_chicken";
	case Item::rotten_flesh_Id:     return L"minecraft:rotten_flesh";
	case Item::enderPearl_Id:       return L"minecraft:ender_pearl";
	case Item::blazeRod_Id:         return L"minecraft:blaze_rod";
	case Item::ghastTear_Id:        return L"minecraft:ghast_tear";
	case Item::goldNugget_Id:       return L"minecraft:gold_nugget";
	case Item::netherStalkSeeds_Id: return L"minecraft:nether_wart";
	// An aux of 0 was the plain water bottle, which is a separate item now.
	case Item::potion_Id:           return aux == 0 ? L"minecraft:glass_bottle" : L"minecraft:potion";
	case Item::glassBottle_Id:      return L"minecraft:glass_bottle";
	case Item::spiderEye_Id:        return L"minecraft:spider_eye";
	case Item::fermentedSpiderEye_Id: return L"minecraft:fermented_spider_eye";
	case Item::blazePowder_Id:      return L"minecraft:blaze_powder";
	case Item::magmaCream_Id:       return L"minecraft:magma_cream";
	case Item::brewingStand_Id:     return L"minecraft:brewing_stand";
	case Item::cauldron_Id:         return L"minecraft:cauldron";
	case Item::eyeOfEnder_Id:       return L"minecraft:ender_eye";
	case Item::speckledMelon_Id:    return L"minecraft:glistering_melon_slice";
	case Item::monsterPlacer_Id:    return spawnEgg(aux);
	case Item::expBottle_Id:        return L"minecraft:experience_bottle";
	case Item::fireball_Id:         return L"minecraft:fire_charge";
	case Item::emerald_Id:          return L"minecraft:emerald";
	case Item::itemFrame_Id:        return L"minecraft:item_frame";
	case Item::flowerPot_Id:        return L"minecraft:flower_pot";
	case Item::carrots_Id:          return L"minecraft:carrot";
	case Item::potato_Id:           return L"minecraft:potato";
	case Item::potatoBaked_Id:      return L"minecraft:baked_potato";
	case Item::potatoPoisonous_Id:  return L"minecraft:poisonous_potato";
	case Item::carrotGolden_Id:     return L"minecraft:golden_carrot";
	case Item::skull_Id:            return SKULLS[(aux < 0 || aux > 5) ? 0 : aux];
	case Item::carrotOnAStick_Id:   return L"minecraft:carrot_on_a_stick";
	case Item::pumpkinPie_Id:       return L"minecraft:pumpkin_pie";
	case Item::enchantedBook_Id:    return L"minecraft:enchanted_book";
	case Item::netherbrick_Id:      return L"minecraft:nether_brick";
	case Item::netherQuartz_Id:     return L"minecraft:quartz";
	default:                        break;
	}

	return NULL;
}

// True when an item's aux value selected a variant rather than tracking durability.
// For these the aux has already been folded into the chosen item id, so carrying it
// over as damage would be wrong.
static bool auxIsVariant(int itemId)
{
	if (itemId > 0 && itemId < 256) return true;		// block items: aux is block metadata
	if (itemId >= 2256 && itemId <= 2267) return true;	// discs

	switch (itemId)
	{
	case Item::coal_Id:
	case Item::dye_powder_Id:
	case Item::potion_Id:
	case Item::monsterPlacer_Id:
	case Item::skull_Id:
	case Item::apple_gold_Id:
		return true;
	default:
		return false;
	}
}

// CompoundTag::remove() unlinks without deleting and put() overwrites without deleting,
// so both leak unless the old tag is disposed of explicitly.
static void dropTag(CompoundTag *owner, wchar_t *name)
{
	if (owner == NULL || !owner->contains(name)) return;

	Tag *old = owner->get(name);
	owner->remove(name);
	delete old;
}

bool AnvilItemMapping::convertItemStack(CompoundTag *tag)
{
	if (tag == NULL || !tag->contains(L"id")) return false;

	const int itemId = tag->getShort(L"id");
	const int aux = tag->contains(L"Damage") ? tag->getShort(L"Damage") : 0;

	const wchar_t *modernId = toItemId(itemId, aux);
	if (modernId == NULL) return false;

	// Java validates count against 1..99 and rejects the whole stack outside it, so an
	// empty or overfull slot has to be dropped rather than written through.
	int count = tag->contains(L"Count") ? (int)(signed char)tag->getByte(L"Count") : 1;
	if (count <= 0) return false;
	if (count > 99) count = 99;

	// Durability moved into the component patch. Variant auxes are already reflected in
	// the id chosen above, so only real durability survives.
	const bool keepDamage = !auxIsVariant(itemId) && aux > 0;

	dropTag(tag, L"id");
	dropTag(tag, L"Count");
	dropTag(tag, L"Damage");

	// LCE's item NBT extras are the pre-1.20.5 "tag" shape, which the component system
	// replaced wholesale; there is no faithful translation, so it is dropped.
	dropTag(tag, L"tag");

	tag->putString(L"id", modernId);
	tag->putInt(L"count", count);

	if (keepDamage)
	{
		CompoundTag *components = new CompoundTag(L"components");
		components->putInt(L"minecraft:damage", aux);
		tag->put(L"components", components);
	}

	return true;
}

void AnvilItemMapping::convertItemList(CompoundTag *owner, const wchar_t *listName)
{
	if (owner == NULL || !owner->contains((wchar_t *)listName)) return;

	ListTag<CompoundTag> *items = (ListTag<CompoundTag> *)owner->getList((wchar_t *)listName);
	if (items == NULL) return;

	// Stacks that cannot be represented are dropped, so the list is rebuilt rather than
	// edited in place. The survivors are copied because the original ListTag owns its
	// children and deletes them when it goes.
	ListTag<CompoundTag> *kept = new ListTag<CompoundTag>();

	for (int i = 0; i < items->size(); i++)
	{
		CompoundTag *stack = items->get(i);
		if (stack == NULL) continue;

		// Slot survives untouched; only the stack itself is rewritten.
		if (convertItemStack(stack)) kept->add((CompoundTag *)stack->copy());
	}

	dropTag(owner, (wchar_t *)listName);
	owner->put((wchar_t *)listName, kept);
}

// The reverse table is derived from the forward mapping rather than written out again,
// exactly as AnvilBlockMapping does, so the two directions cannot drift apart. The first
// (id, aux) producing a name wins, which keeps the canonical low-aux variant.
static map<wstring, int> *s_reverseItems = NULL;

static void buildReverseItemTable()
{
	if (s_reverseItems != NULL) return;

	s_reverseItems = new map<wstring, int>();

	for (int id = 1; id <= 2267; id++)
	{
		// Skip the empty stretch between the items and the music discs.
		if (id > 406 && id < 2256) continue;

		for (int aux = 0; aux < 16; aux++)
		{
			const wchar_t *name = AnvilItemMapping::toItemId(id, aux);
			if (name == NULL) continue;

			const wstring key = name;
			if (s_reverseItems->find(key) == s_reverseItems->end())
			{
				(*s_reverseItems)[key] = (id << 4) | aux;
			}
		}
	}
}

bool AnvilItemMapping::fromItemStack(CompoundTag *tag)
{
	if (tag == NULL || !tag->contains(L"id")) return false;

	buildReverseItemTable();

	const wstring name = tag->getString(L"id");

	AUTO_VAR(it, s_reverseItems->find(name));
	if (it == s_reverseItems->end()) return false;

	const int itemId = (it->second >> 4) & 0xfff;
	int aux = it->second & 15;

	int count = tag->contains(L"count") ? tag->getInt(L"count") : 1;
	if (count <= 0) count = 1;
	if (count > 64) count = 64;

	// Durability was moved into the component patch on the way out; bring it back.
	if (tag->contains(L"components"))
	{
		CompoundTag *components = tag->getCompound(L"components");
		if (components != NULL && components->contains(L"minecraft:damage"))
		{
			aux = components->getInt(L"minecraft:damage");
		}
	}

	dropTag(tag, L"id");
	dropTag(tag, L"count");
	dropTag(tag, L"components");

	tag->putShort(L"id", (short)itemId);
	tag->putByte(L"Count", (byte)count);
	tag->putShort(L"Damage", (short)aux);

	return true;
}

void AnvilItemMapping::convertItemListFromJava(CompoundTag *owner, const wchar_t *listName)
{
	if (owner == NULL || !owner->contains((wchar_t *)listName)) return;

	ListTag<CompoundTag> *items = (ListTag<CompoundTag> *)owner->getList((wchar_t *)listName);
	if (items == NULL) return;

	ListTag<CompoundTag> *kept = new ListTag<CompoundTag>();

	for (int i = 0; i < items->size(); i++)
	{
		CompoundTag *stack = items->get(i);
		if (stack == NULL) continue;

		if (fromItemStack(stack)) kept->add((CompoundTag *)stack->copy());
	}

	dropTag(owner, (wchar_t *)listName);
	owner->put((wchar_t *)listName, kept);
}

#endif // _WINDOWS64
