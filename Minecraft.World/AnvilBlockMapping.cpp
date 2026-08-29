#include "stdafx.h"
#include "AnvilBlockMapping.h"
#include "Tile.h"

// 4J Meow - see AnvilBlockMapping.h for the rationale. The tables below follow the
// same flattening Mojang applied in 1.13, restricted to the 1.6.4 block set LCE ships.

static const wchar_t *NUM[17] =
{
	L"0", L"1", L"2",  L"3",  L"4",  L"5",  L"6",  L"7",
	L"8", L"9", L"10", L"11", L"12", L"13", L"14", L"15", L"16"
};

static const wchar_t *TRUEFALSE[2] = { L"false", L"true" };

// Legacy wood order (planks / sapling / log / leaves metadata & 3).
static const wchar_t *PLANKS[4] =
{
	L"minecraft:oak_planks", L"minecraft:spruce_planks",
	L"minecraft:birch_planks", L"minecraft:jungle_planks"
};

static const wchar_t *SAPLINGS[4] =
{
	L"minecraft:oak_sapling", L"minecraft:spruce_sapling",
	L"minecraft:birch_sapling", L"minecraft:jungle_sapling"
};

static const wchar_t *LOGS[4] =
{
	L"minecraft:oak_log", L"minecraft:spruce_log",
	L"minecraft:birch_log", L"minecraft:jungle_log"
};

static const wchar_t *WOODS[4] =
{
	L"minecraft:oak_wood", L"minecraft:spruce_wood",
	L"minecraft:birch_wood", L"minecraft:jungle_wood"
};

static const wchar_t *LEAVES[4] =
{
	L"minecraft:oak_leaves", L"minecraft:spruce_leaves",
	L"minecraft:birch_leaves", L"minecraft:jungle_leaves"
};

// Legacy horizontal facing used by furnaces, chests, ladders, wall signs (meta 2..5).
static const wchar_t *FACING_2345[6] =
{
	L"north", L"north", L"north", L"south", L"west", L"east"
};

// Legacy stair facing (meta & 3).
static const wchar_t *STAIR_FACING[4] = { L"east", L"west", L"south", L"north" };

// Legacy torch / button / lever wall facing (meta 1..4).
static const wchar_t *WALL_FACING[5] = { L"north", L"east", L"west", L"south", L"north" };

// The rotation order shared by beds, gates, cocoa, anvils and portal frames.
static const wchar_t *FACING_SWNE[4] = { L"south", L"west", L"north", L"east" };

static const wchar_t *RAIL_SHAPE[10] =
{
	L"north_south", L"east_west",
	L"ascending_east", L"ascending_west", L"ascending_north", L"ascending_south",
	L"south_east", L"south_west", L"north_west", L"north_east"
};

static const wchar_t *STONE_SLABS[8] =
{
	L"minecraft:smooth_stone_slab", L"minecraft:sandstone_slab",
	L"minecraft:petrified_oak_slab", L"minecraft:cobblestone_slab",
	L"minecraft:brick_slab", L"minecraft:stone_brick_slab",
	L"minecraft:nether_brick_slab", L"minecraft:quartz_slab"
};

static const wchar_t *WOOD_SLABS[4] =
{
	L"minecraft:oak_slab", L"minecraft:spruce_slab",
	L"minecraft:birch_slab", L"minecraft:jungle_slab"
};

static const wchar_t *STONE_BRICKS[4] =
{
	L"minecraft:stone_bricks", L"minecraft:mossy_stone_bricks",
	L"minecraft:cracked_stone_bricks", L"minecraft:chiseled_stone_bricks"
};

static const wchar_t *INFESTED[4] =
{
	L"minecraft:infested_stone", L"minecraft:infested_cobblestone",
	L"minecraft:infested_stone_bricks", L"minecraft:infested_mossy_stone_bricks"
};

static const wchar_t *WOOLS[16] =
{
	L"minecraft:white_wool", L"minecraft:orange_wool", L"minecraft:magenta_wool", L"minecraft:light_blue_wool",
	L"minecraft:yellow_wool", L"minecraft:lime_wool", L"minecraft:pink_wool", L"minecraft:gray_wool",
	L"minecraft:light_gray_wool", L"minecraft:cyan_wool", L"minecraft:purple_wool", L"minecraft:blue_wool",
	L"minecraft:brown_wool", L"minecraft:green_wool", L"minecraft:red_wool", L"minecraft:black_wool"
};

static const wchar_t *CARPETS[16] =
{
	L"minecraft:white_carpet", L"minecraft:orange_carpet", L"minecraft:magenta_carpet", L"minecraft:light_blue_carpet",
	L"minecraft:yellow_carpet", L"minecraft:lime_carpet", L"minecraft:pink_carpet", L"minecraft:gray_carpet",
	L"minecraft:light_gray_carpet", L"minecraft:cyan_carpet", L"minecraft:purple_carpet", L"minecraft:blue_carpet",
	L"minecraft:brown_carpet", L"minecraft:green_carpet", L"minecraft:red_carpet", L"minecraft:black_carpet"
};

// Clamp a legacy facing nibble into the 2..5 range the FACING_2345 table covers.
static inline int facingIndex(int meta)
{
	const int f = meta & 7;
	return (f < 2 || f > 5) ? 3 : f;
}

// Shared helper: a stair block, which in LCE always carries facing + half in its metadata.
static void stairs(AnvilBlockState &out, const wchar_t *name, int meta)
{
	out.set(name);
	out.prop(L"facing", STAIR_FACING[meta & 3]);
	out.prop(L"half", (meta & 4) ? L"top" : L"bottom");
	out.prop(L"shape", L"straight");
	out.prop(L"waterlogged", L"false");
}

// Shared helper: a slab. LCE stores the double-slab variant as a separate tile id.
static void slab(AnvilBlockState &out, const wchar_t *name, bool isDouble, int meta)
{
	out.set(name);
	out.prop(L"type", isDouble ? L"double" : ((meta & 8) ? L"top" : L"bottom"));
	out.prop(L"waterlogged", L"false");
}

// Shared helper: a door half. Legacy doors split their state across the two halves,
// so each half only knows part of the modern state; the rest takes a neutral default.
static void door(AnvilBlockState &out, const wchar_t *name, int meta)
{
	static const wchar_t *doorFacing[4] = { L"east", L"south", L"west", L"north" };

	out.set(name);
	if (meta & 8)
	{
		out.prop(L"half", L"upper");
		out.prop(L"hinge", (meta & 1) ? L"right" : L"left");
		out.prop(L"powered", TRUEFALSE[(meta & 2) ? 1 : 0]);
		out.prop(L"facing", L"east");
		out.prop(L"open", L"false");
	}
	else
	{
		out.prop(L"half", L"lower");
		out.prop(L"facing", doorFacing[meta & 3]);
		out.prop(L"open", TRUEFALSE[(meta & 4) ? 1 : 0]);
		out.prop(L"hinge", L"left");
		out.prop(L"powered", L"false");
	}
}

// A four-sided connectable block (fences, bars, panes) - LCE derives the connections at
// render time rather than storing them, so they are written unconnected and Java's own
// neighbour update fixes them on load.
static void connectable(AnvilBlockState &out, const wchar_t *name, const wchar_t *none)
{
	out.set(name);
	out.prop(L"north", none); out.prop(L"south", none);
	out.prop(L"east", none);  out.prop(L"west", none);
	out.prop(L"waterlogged", L"false");
}

void AnvilBlockMapping::toBlockState(int tileId, int metaData, AnvilBlockState &out)
{
	const int m = metaData & 15;

	switch (tileId)
	{
	case 0:                    out.set(L"minecraft:air"); return;
	case Tile::rock_Id:        out.set(L"minecraft:stone"); return;
	case Tile::grass_Id:       out.set(L"minecraft:grass_block"); out.prop(L"snowy", L"false"); return;

	case Tile::dirt_Id:
		if ((m & 3) == 1) out.set(L"minecraft:coarse_dirt");
		else if ((m & 3) == 2) { out.set(L"minecraft:podzol"); out.prop(L"snowy", L"false"); }
		else out.set(L"minecraft:dirt");
		return;

	case Tile::stoneBrick_Id:  out.set(L"minecraft:cobblestone"); return;
	case Tile::wood_Id:        out.set(PLANKS[m & 3]); return;

	case Tile::sapling_Id:
		out.set(SAPLINGS[m & 3]);
		out.prop(L"stage", NUM[(m >> 3) & 1]);
		return;

	case Tile::unbreakable_Id: out.set(L"minecraft:bedrock"); return;

	// Java no longer distinguishes flowing from still liquids; the level property carries it.
	case Tile::water_Id:       out.set(L"minecraft:water"); out.prop(L"level", NUM[m]); return;
	case Tile::calmWater_Id:   out.set(L"minecraft:water"); out.prop(L"level", L"0"); return;
	case Tile::lava_Id:        out.set(L"minecraft:lava");  out.prop(L"level", NUM[m]); return;
	case Tile::calmLava_Id:    out.set(L"minecraft:lava");  out.prop(L"level", L"0"); return;

	case Tile::sand_Id:        out.set((m & 1) ? L"minecraft:red_sand" : L"minecraft:sand"); return;
	case Tile::gravel_Id:      out.set(L"minecraft:gravel"); return;
	case Tile::goldOre_Id:     out.set(L"minecraft:gold_ore"); return;
	case Tile::ironOre_Id:     out.set(L"minecraft:iron_ore"); return;
	case Tile::coalOre_Id:     out.set(L"minecraft:coal_ore"); return;

	case Tile::treeTrunk_Id:
		{
			const int axis = (m >> 2) & 3;
			if (axis == 3)
			{
				// Legacy "all bark" logs became the dedicated wood blocks.
				out.set(WOODS[m & 3]);
				out.prop(L"axis", L"y");
			}
			else
			{
				out.set(LOGS[m & 3]);
				out.prop(L"axis", axis == 0 ? L"y" : (axis == 1 ? L"x" : L"z"));
			}
		}
		return;

	case Tile::leaves_Id:
		out.set(LEAVES[m & 3]);
		out.prop(L"persistent", TRUEFALSE[(m & 4) ? 1 : 0]);
		out.prop(L"distance", L"7");
		out.prop(L"waterlogged", L"false");
		return;

	case Tile::sponge_Id:      out.set(L"minecraft:sponge"); return;
	case Tile::glass_Id:       out.set(L"minecraft:glass"); return;
	case Tile::lapisOre_Id:    out.set(L"minecraft:lapis_ore"); return;
	case Tile::lapisBlock_Id:  out.set(L"minecraft:lapis_block"); return;

	case Tile::dispenser_Id:
		out.set(L"minecraft:dispenser");
		out.prop(L"facing", FACING_2345[facingIndex(m)]);
		out.prop(L"triggered", TRUEFALSE[(m & 8) ? 1 : 0]);
		return;

	case Tile::sandStone_Id:
		if ((m & 3) == 1) out.set(L"minecraft:chiseled_sandstone");
		else if ((m & 3) == 2) out.set(L"minecraft:cut_sandstone");
		else out.set(L"minecraft:sandstone");
		return;

	case Tile::musicBlock_Id:
		out.set(L"minecraft:note_block");
		out.prop(L"instrument", L"harp");
		out.prop(L"note", L"0");
		out.prop(L"powered", L"false");
		return;

	case Tile::bed_Id:
		// Bed colour lives in the block entity pre-flattening; red is the only 1.6 bed.
		out.set(L"minecraft:red_bed");
		out.prop(L"facing", FACING_SWNE[m & 3]);
		out.prop(L"occupied", TRUEFALSE[(m & 4) ? 1 : 0]);
		out.prop(L"part", (m & 8) ? L"head" : L"foot");
		return;

	case Tile::goldenRail_Id:
	case Tile::detectorRail_Id:
		out.set(tileId == Tile::goldenRail_Id ? L"minecraft:powered_rail" : L"minecraft:detector_rail");
		out.prop(L"shape", RAIL_SHAPE[(m & 7) > 5 ? 0 : (m & 7)]);
		out.prop(L"powered", TRUEFALSE[(m & 8) ? 1 : 0]);
		out.prop(L"waterlogged", L"false");
		return;

	case Tile::pistonStickyBase_Id:
	case Tile::pistonBase_Id:
		out.set(tileId == Tile::pistonStickyBase_Id ? L"minecraft:sticky_piston" : L"minecraft:piston");
		out.prop(L"facing", FACING_2345[facingIndex(m)]);
		out.prop(L"extended", TRUEFALSE[(m & 8) ? 1 : 0]);
		return;

	case Tile::pistonExtensionPiece_Id:
		out.set(L"minecraft:piston_head");
		out.prop(L"facing", FACING_2345[facingIndex(m)]);
		out.prop(L"type", (m & 8) ? L"sticky" : L"normal");
		out.prop(L"short", L"false");
		return;

	case Tile::pistonMovingPiece_Id:
		out.set(L"minecraft:moving_piston");
		out.prop(L"facing", FACING_2345[facingIndex(m)]);
		out.prop(L"type", (m & 8) ? L"sticky" : L"normal");
		return;

	case Tile::web_Id:         out.set(L"minecraft:cobweb"); return;

	case Tile::tallgrass_Id:
		if ((m & 3) == 2) out.set(L"minecraft:fern");
		else if ((m & 3) == 0) out.set(L"minecraft:dead_bush");
		else out.set(L"minecraft:short_grass");
		return;

	case Tile::deadBush_Id:    out.set(L"minecraft:dead_bush"); return;
	case Tile::cloth_Id:       out.set(WOOLS[m]); return;
	case Tile::flower_Id:      out.set(L"minecraft:dandelion"); return;
	case Tile::rose_Id:        out.set(L"minecraft:poppy"); return;
	case Tile::mushroom1_Id:   out.set(L"minecraft:brown_mushroom"); return;
	case Tile::mushroom2_Id:   out.set(L"minecraft:red_mushroom"); return;
	case Tile::goldBlock_Id:   out.set(L"minecraft:gold_block"); return;
	case Tile::ironBlock_Id:   out.set(L"minecraft:iron_block"); return;

	case Tile::stoneSlab_Id:       slab(out, STONE_SLABS[m & 7], true,  m); return;
	case Tile::stoneSlabHalf_Id:   slab(out, STONE_SLABS[m & 7], false, m); return;
	case Tile::woodSlab_Id:        slab(out, WOOD_SLABS[m & 3],  true,  m); return;
	case Tile::woodSlabHalf_Id:    slab(out, WOOD_SLABS[m & 3],  false, m); return;

	case Tile::redBrick_Id:    out.set(L"minecraft:bricks"); return;

	case Tile::tnt_Id:
		out.set(L"minecraft:tnt");
		out.prop(L"unstable", TRUEFALSE[(m & 1) ? 1 : 0]);
		return;

	case Tile::bookshelf_Id:   out.set(L"minecraft:bookshelf"); return;
	case Tile::mossStone_Id:   out.set(L"minecraft:mossy_cobblestone"); return;
	case Tile::obsidian_Id:    out.set(L"minecraft:obsidian"); return;

	case Tile::torch_Id:
		if (m >= 1 && m <= 4) { out.set(L"minecraft:wall_torch"); out.prop(L"facing", WALL_FACING[m]); }
		else out.set(L"minecraft:torch");
		return;

	case Tile::fire_Id:
		out.set(L"minecraft:fire");
		out.prop(L"age", NUM[m]);
		return;

	case Tile::mobSpawner_Id:  out.set(L"minecraft:spawner"); return;

	case Tile::stairs_wood_Id:             stairs(out, L"minecraft:oak_stairs", m); return;
	case Tile::stairs_stone_Id:            stairs(out, L"minecraft:cobblestone_stairs", m); return;
	case Tile::stairs_bricks_Id:           stairs(out, L"minecraft:brick_stairs", m); return;
	case Tile::stairs_stoneBrickSmooth_Id: stairs(out, L"minecraft:stone_brick_stairs", m); return;
	case Tile::stairs_netherBricks_Id:     stairs(out, L"minecraft:nether_brick_stairs", m); return;
	case Tile::stairs_sandstone_Id:        stairs(out, L"minecraft:sandstone_stairs", m); return;
	case Tile::stairs_sprucewood_Id:       stairs(out, L"minecraft:spruce_stairs", m); return;
	case Tile::stairs_birchwood_Id:        stairs(out, L"minecraft:birch_stairs", m); return;
	case Tile::stairs_junglewood_Id:       stairs(out, L"minecraft:jungle_stairs", m); return;
	case Tile::stairs_quartz_Id:           stairs(out, L"minecraft:quartz_stairs", m); return;

	case Tile::chest_Id:
		out.set(L"minecraft:chest");
		out.prop(L"facing", FACING_2345[facingIndex(m)]);
		out.prop(L"type", L"single");
		out.prop(L"waterlogged", L"false");
		return;

	case Tile::enderChest_Id:
		out.set(L"minecraft:ender_chest");
		out.prop(L"facing", FACING_2345[facingIndex(m)]);
		out.prop(L"waterlogged", L"false");
		return;

	case Tile::redStoneDust_Id:
		out.set(L"minecraft:redstone_wire");
		out.prop(L"power", NUM[m]);
		out.prop(L"north", L"none");
		out.prop(L"south", L"none");
		out.prop(L"east", L"none");
		out.prop(L"west", L"none");
		return;

	case Tile::diamondOre_Id:   out.set(L"minecraft:diamond_ore"); return;
	case Tile::diamondBlock_Id: out.set(L"minecraft:diamond_block"); return;
	case Tile::workBench_Id:    out.set(L"minecraft:crafting_table"); return;

	case Tile::crops_Id:
		out.set(L"minecraft:wheat");
		out.prop(L"age", NUM[m & 7]);
		return;

	case Tile::farmland_Id:
		out.set(L"minecraft:farmland");
		out.prop(L"moisture", NUM[m & 7]);
		return;

	case Tile::furnace_Id:
	case Tile::furnace_lit_Id:
		out.set(L"minecraft:furnace");
		out.prop(L"facing", FACING_2345[facingIndex(m)]);
		out.prop(L"lit", TRUEFALSE[tileId == Tile::furnace_lit_Id ? 1 : 0]);
		return;

	case Tile::sign_Id:
		out.set(L"minecraft:oak_sign");
		out.prop(L"rotation", NUM[m]);
		out.prop(L"waterlogged", L"false");
		return;

	case Tile::wallSign_Id:
		out.set(L"minecraft:oak_wall_sign");
		out.prop(L"facing", FACING_2345[facingIndex(m)]);
		out.prop(L"waterlogged", L"false");
		return;

	case Tile::door_wood_Id:  door(out, L"minecraft:oak_door", m); return;
	case Tile::door_iron_Id:  door(out, L"minecraft:iron_door", m); return;

	case Tile::ladder_Id:
		out.set(L"minecraft:ladder");
		out.prop(L"facing", FACING_2345[facingIndex(m)]);
		out.prop(L"waterlogged", L"false");
		return;

	case Tile::rail_Id:
		out.set(L"minecraft:rail");
		out.prop(L"shape", RAIL_SHAPE[m > 9 ? 0 : m]);
		out.prop(L"waterlogged", L"false");
		return;

	case Tile::lever_Id:
		{
			const int o = m & 7;
			out.set(L"minecraft:lever");
			// 0/7 are ceiling mounts, 5/6 floor mounts, 1-4 wall mounts.
			if (o == 0 || o == 7)      { out.prop(L"face", L"ceiling"); out.prop(L"facing", o == 0 ? L"west" : L"north"); }
			else if (o == 5 || o == 6) { out.prop(L"face", L"floor");   out.prop(L"facing", o == 5 ? L"north" : L"west"); }
			else                       { out.prop(L"face", L"wall");    out.prop(L"facing", WALL_FACING[o]); }
			out.prop(L"powered", TRUEFALSE[(m & 8) ? 1 : 0]);
		}
		return;

	case Tile::button_stone_Id:
	case Tile::button_wood_Id:
		{
			const int o = m & 7;
			out.set(tileId == Tile::button_stone_Id ? L"minecraft:stone_button" : L"minecraft:oak_button");
			if (o == 0)      { out.prop(L"face", L"ceiling"); out.prop(L"facing", L"north"); }
			else if (o >= 5) { out.prop(L"face", L"floor");   out.prop(L"facing", L"north"); }
			else             { out.prop(L"face", L"wall");    out.prop(L"facing", WALL_FACING[o]); }
			out.prop(L"powered", TRUEFALSE[(m & 8) ? 1 : 0]);
		}
		return;

	case Tile::pressurePlate_stone_Id:
	case Tile::pressurePlate_wood_Id:
		out.set(tileId == Tile::pressurePlate_stone_Id ? L"minecraft:stone_pressure_plate" : L"minecraft:oak_pressure_plate");
		out.prop(L"powered", TRUEFALSE[(m & 1) ? 1 : 0]);
		return;

	case Tile::redStoneOre_Id:
	case Tile::redStoneOre_lit_Id:
		out.set(L"minecraft:redstone_ore");
		out.prop(L"lit", TRUEFALSE[tileId == Tile::redStoneOre_lit_Id ? 1 : 0]);
		return;

	case Tile::notGate_off_Id:
	case Tile::notGate_on_Id:
		if (m >= 1 && m <= 4)
		{
			out.set(L"minecraft:redstone_wall_torch");
			out.prop(L"facing", WALL_FACING[m]);
		}
		else out.set(L"minecraft:redstone_torch");
		out.prop(L"lit", TRUEFALSE[tileId == Tile::notGate_on_Id ? 1 : 0]);
		return;

	case Tile::topSnow_Id:
		out.set(L"minecraft:snow");
		out.prop(L"layers", NUM[(m & 7) + 1]);
		return;

	case Tile::ice_Id:      out.set(L"minecraft:ice"); return;
	case Tile::snow_Id:     out.set(L"minecraft:snow_block"); return;

	case Tile::cactus_Id:
		out.set(L"minecraft:cactus");
		out.prop(L"age", NUM[m]);
		return;

	case Tile::clay_Id:     out.set(L"minecraft:clay"); return;

	case Tile::reeds_Id:
		out.set(L"minecraft:sugar_cane");
		out.prop(L"age", NUM[m]);
		return;

	case Tile::recordPlayer_Id:
		out.set(L"minecraft:jukebox");
		out.prop(L"has_record", TRUEFALSE[(m & 1) ? 1 : 0]);
		return;

	case Tile::fence_Id:        connectable(out, L"minecraft:oak_fence", L"false"); return;
	case Tile::netherFence_Id:  connectable(out, L"minecraft:nether_brick_fence", L"false"); return;
	case Tile::ironFence_Id:    connectable(out, L"minecraft:iron_bars", L"false"); return;
	case Tile::thinGlass_Id:    connectable(out, L"minecraft:glass_pane", L"false"); return;

	case Tile::pumpkin_Id:
	case Tile::litPumpkin_Id:
		out.set(tileId == Tile::pumpkin_Id ? L"minecraft:carved_pumpkin" : L"minecraft:jack_o_lantern");
		out.prop(L"facing", FACING_SWNE[m & 3]);
		return;

	case Tile::hellRock_Id:  out.set(L"minecraft:netherrack"); return;
	case Tile::hellSand_Id:  out.set(L"minecraft:soul_sand"); return;
	case Tile::lightGem_Id:  out.set(L"minecraft:glowstone"); return;

	case Tile::portalTile_Id:
		out.set(L"minecraft:nether_portal");
		out.prop(L"axis", (m & 3) == 2 ? L"z" : L"x");
		return;

	case Tile::cake_Id:
		out.set(L"minecraft:cake");
		out.prop(L"bites", NUM[m & 7]);
		return;

	case Tile::diode_off_Id:
	case Tile::diode_on_Id:
		{
			static const wchar_t *repeaterFacing[4] = { L"north", L"east", L"south", L"west" };
			out.set(L"minecraft:repeater");
			out.prop(L"facing", repeaterFacing[m & 3]);
			out.prop(L"delay", NUM[((m >> 2) & 3) + 1]);
			out.prop(L"powered", TRUEFALSE[tileId == Tile::diode_on_Id ? 1 : 0]);
			out.prop(L"locked", L"false");
		}
		return;

	// The 1.6 "locked chest" April Fools block has no modern counterpart.
	case Tile::aprilFoolsJoke_Id: out.set(L"minecraft:air"); return;

	case Tile::trapdoor_Id:
		{
			static const wchar_t *trapFacing[4] = { L"north", L"south", L"west", L"east" };
			out.set(L"minecraft:oak_trapdoor");
			out.prop(L"facing", trapFacing[m & 3]);
			out.prop(L"open", TRUEFALSE[(m & 4) ? 1 : 0]);
			out.prop(L"half", (m & 8) ? L"top" : L"bottom");
			out.prop(L"powered", L"false");
			out.prop(L"waterlogged", L"false");
		}
		return;

	case Tile::monsterStoneEgg_Id:  out.set(INFESTED[m & 3]); return;
	case Tile::stoneBrickSmooth_Id: out.set(STONE_BRICKS[m & 3]); return;

	case Tile::hugeMushroom1_Id:
	case Tile::hugeMushroom2_Id:
		// Legacy metadata encoded which faces show skin; approximate with all-skin.
		out.set(tileId == Tile::hugeMushroom1_Id ? L"minecraft:brown_mushroom_block" : L"minecraft:red_mushroom_block");
		out.prop(L"north", L"true"); out.prop(L"south", L"true");
		out.prop(L"east", L"true");  out.prop(L"west", L"true");
		out.prop(L"up", L"true");    out.prop(L"down", L"true");
		return;

	case Tile::melon_Id:  out.set(L"minecraft:melon"); return;

	case Tile::pumpkinStem_Id:
	case Tile::melonStem_Id:
		out.set(tileId == Tile::pumpkinStem_Id ? L"minecraft:pumpkin_stem" : L"minecraft:melon_stem");
		out.prop(L"age", NUM[m & 7]);
		return;

	case Tile::vine_Id:
		out.set(L"minecraft:vine");
		out.prop(L"south", TRUEFALSE[(m & 1) ? 1 : 0]);
		out.prop(L"west",  TRUEFALSE[(m & 2) ? 1 : 0]);
		out.prop(L"north", TRUEFALSE[(m & 4) ? 1 : 0]);
		out.prop(L"east",  TRUEFALSE[(m & 8) ? 1 : 0]);
		out.prop(L"up",    L"false");
		return;

	case Tile::fenceGate_Id:
		out.set(L"minecraft:oak_fence_gate");
		out.prop(L"facing", FACING_SWNE[m & 3]);
		out.prop(L"open", TRUEFALSE[(m & 4) ? 1 : 0]);
		out.prop(L"powered", TRUEFALSE[(m & 8) ? 1 : 0]);
		out.prop(L"in_wall", L"false");
		return;

	case Tile::mycel_Id:       out.set(L"minecraft:mycelium"); out.prop(L"snowy", L"false"); return;
	case Tile::waterLily_Id:   out.set(L"minecraft:lily_pad"); return;
	case Tile::netherBrick_Id: out.set(L"minecraft:nether_bricks"); return;

	case Tile::netherStalk_Id:
		out.set(L"minecraft:nether_wart");
		out.prop(L"age", NUM[m & 3]);
		return;

	case Tile::enchantTable_Id: out.set(L"minecraft:enchanting_table"); return;

	case Tile::brewingStand_Id:
		out.set(L"minecraft:brewing_stand");
		out.prop(L"has_bottle_0", TRUEFALSE[(m & 1) ? 1 : 0]);
		out.prop(L"has_bottle_1", TRUEFALSE[(m & 2) ? 1 : 0]);
		out.prop(L"has_bottle_2", TRUEFALSE[(m & 4) ? 1 : 0]);
		return;

	case Tile::cauldron_Id:
		// The empty cauldron and the water-filled one are separate blocks post-flattening.
		if ((m & 3) == 0) out.set(L"minecraft:cauldron");
		else { out.set(L"minecraft:water_cauldron"); out.prop(L"level", NUM[m & 3]); }
		return;

	case Tile::endPortalTile_Id: out.set(L"minecraft:end_portal"); return;

	case Tile::endPortalFrameTile_Id:
		out.set(L"minecraft:end_portal_frame");
		out.prop(L"facing", FACING_SWNE[m & 3]);
		out.prop(L"eye", TRUEFALSE[(m & 4) ? 1 : 0]);
		return;

	case Tile::whiteStone_Id: out.set(L"minecraft:end_stone"); return;
	case Tile::dragonEgg_Id:  out.set(L"minecraft:dragon_egg"); return;

	case Tile::redstoneLight_Id:
	case Tile::redstoneLight_lit_Id:
		out.set(L"minecraft:redstone_lamp");
		out.prop(L"lit", TRUEFALSE[tileId == Tile::redstoneLight_lit_Id ? 1 : 0]);
		return;

	case Tile::cocoa_Id:
		out.set(L"minecraft:cocoa");
		out.prop(L"facing", FACING_SWNE[m & 3]);
		out.prop(L"age", NUM[(m >> 2) & 3]);
		return;

	case Tile::emeraldOre_Id:   out.set(L"minecraft:emerald_ore"); return;
	case Tile::emeraldBlock_Id: out.set(L"minecraft:emerald_block"); return;

	case Tile::tripWireSource_Id:
		out.set(L"minecraft:tripwire_hook");
		out.prop(L"facing", FACING_SWNE[m & 3]);
		out.prop(L"powered", TRUEFALSE[(m & 8) ? 1 : 0]);
		out.prop(L"attached", TRUEFALSE[(m & 4) ? 1 : 0]);
		return;

	case Tile::tripWire_Id:
		out.set(L"minecraft:tripwire");
		out.prop(L"powered",  TRUEFALSE[(m & 1) ? 1 : 0]);
		out.prop(L"attached", TRUEFALSE[(m & 4) ? 1 : 0]);
		out.prop(L"disarmed", TRUEFALSE[(m & 8) ? 1 : 0]);
		out.prop(L"north", L"false"); out.prop(L"south", L"false");
		return;

	case Tile::cobbleWall_Id:
		connectable(out, (m & 1) ? L"minecraft:mossy_cobblestone_wall" : L"minecraft:cobblestone_wall", L"none");
		out.prop(L"up", L"true");
		return;

	case Tile::flowerPot_Id:  out.set(L"minecraft:flower_pot"); return;

	case Tile::carrots_Id:
		out.set(L"minecraft:carrots");
		out.prop(L"age", NUM[m & 7]);
		return;

	case Tile::potatoes_Id:
		out.set(L"minecraft:potatoes");
		out.prop(L"age", NUM[m & 7]);
		return;

	case Tile::skull_Id:
		// The skull variant and its rotation live in the block entity pre-flattening,
		// so only the floor/wall split can be recovered from metadata here.
		if ((m & 7) <= 1) { out.set(L"minecraft:skeleton_skull"); out.prop(L"rotation", L"0"); }
		else { out.set(L"minecraft:skeleton_wall_skull"); out.prop(L"facing", FACING_2345[facingIndex(m)]); }
		return;

	case Tile::anvil_Id:
		{
			static const wchar_t *anvils[4] =
			{
				L"minecraft:anvil", L"minecraft:chipped_anvil",
				L"minecraft:damaged_anvil", L"minecraft:anvil"
			};
			out.set(anvils[(m >> 2) & 3]);
			out.prop(L"facing", FACING_SWNE[m & 3]);
		}
		return;

	case Tile::netherQuartz_Id: out.set(L"minecraft:nether_quartz_ore"); return;

	case Tile::quartzBlock_Id:
		if ((m & 7) == 1) out.set(L"minecraft:chiseled_quartz_block");
		else if ((m & 7) >= 2)
		{
			out.set(L"minecraft:quartz_pillar");
			out.prop(L"axis", (m & 7) == 3 ? L"x" : ((m & 7) == 4 ? L"z" : L"y"));
		}
		else out.set(L"minecraft:quartz_block");
		return;

	case Tile::woolCarpet_Id: out.set(CARPETS[m]); return;

	default:
		out.set(L"minecraft:air");
		return;
	}
}

// The reverse tables are derived from the forward mapping rather than written out a
// second time, so the two directions cannot drift apart.
//
// Two of them, because a block state has to be matched on its *whole* identity. Keying
// only on the name loses everything that lives in the properties - every oak_stairs would
// come back facing east, every wall torch pointing north - which is exactly what happened
// before this existed. The name-only table stays as a fallback for states Java wrote that
// LCE cannot produce exactly.
//
// First (id, meta) wins in both, which keeps the canonical low-metadata variant.
static map<wstring, int> *s_reverseByState = NULL;
static map<wstring, int> *s_reverseByName = NULL;

static void buildReverseTable()
{
	if (s_reverseByState != NULL) return;

	s_reverseByState = new map<wstring, int>();
	s_reverseByName = new map<wstring, int>();

	AnvilBlockState state;
	for (int id = 0; id < 256; id++)
	{
		for (int meta = 0; meta < 16; meta++)
		{
			AnvilBlockMapping::toBlockState(id, meta, state);

			// Air is the fallback for every unregistered id; do not let it claim a slot.
			if (id != 0 && wcscmp(state.name, L"minecraft:air") == 0) continue;

			const int packed = (id << 4) | meta;

			const wstring full = state.key();
			if (s_reverseByState->find(full) == s_reverseByState->end())
			{
				(*s_reverseByState)[full] = packed;
			}

			const wstring name = state.name;
			if (s_reverseByName->find(name) == s_reverseByName->end())
			{
				(*s_reverseByName)[name] = packed;
			}
		}
	}
}

wstring AnvilBlockMapping::stateKey(const wstring &name, const vector<AnvilBlockProperty> &properties)
{
	// Same canonical form as AnvilBlockState::key(): properties sorted by name.
	vector<AnvilBlockProperty> sorted = properties;

	for (unsigned int i = 1; i < sorted.size(); i++)
	{
		const AnvilBlockProperty held = sorted[i];
		int j = (int)i - 1;

		while (j >= 0 && sorted[j].key > held.key)
		{
			sorted[j + 1] = sorted[j];
			j--;
		}
		sorted[j + 1] = held;
	}

	wstring key = name;
	for (unsigned int i = 0; i < sorted.size(); i++)
	{
		key += L'\x1f';
		key += sorted[i].key;
		key += L'=';
		key += sorted[i].value;
	}
	return key;
}

bool AnvilBlockMapping::fromBlockState(const wstring &name, const vector<AnvilBlockProperty> &properties,
                                       int &tileId, int &metaData)
{
	buildReverseTable();

	AUTO_VAR(exact, s_reverseByState->find(stateKey(name, properties)));
	if (exact != s_reverseByState->end())
	{
		tileId = (exact->second >> 4) & 0xff;
		metaData = exact->second & 15;
		return true;
	}

	// The exact combination is not one LCE produces - a Java-authored state with extra or
	// different properties. Keep the block and lose only the properties.
	return fromBlockState(name, tileId, metaData);
}

bool AnvilBlockMapping::fromBlockState(const wstring &name, int &tileId, int &metaData)
{
	buildReverseTable();

	AUTO_VAR(it, s_reverseByName->find(name));
	if (it == s_reverseByName->end()) return false;

	tileId = (it->second >> 4) & 0xff;
	metaData = it->second & 15;
	return true;
}
