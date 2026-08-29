# The Anvil (Java Edition) save format in LCE

Status: **implemented and wired in behind `_MEOW_ANVIL_SAVES`; builds clean; never run.**
Nothing here has been verified at runtime — an agent cannot launch the game. With the
macro on, worlds are created in the new layout and existing console-format blob saves
are not read.

## Why this exists

The goal is for LCE worlds to be openable by Java Edition and vice versa. LCE ships its
own save system; Java 26.1.2 uses "Anvil". This document records how the two differ, what
has been built, and the one architectural decision that blocks the final cutover.

## What LCE does today

LCE's storage sits behind three abstract interfaces:

- `LevelStorageSource` (`Minecraft.World/LevelStorageSource.h`) — enumerating, creating,
  renaming and deleting worlds.
- `LevelStorage` (`LevelStorage.h`) — one world; hands out a `ChunkStorage` per dimension.
- `ChunkStorage` (`ChunkStorage.h`) — `load`/`save` of a single `LevelChunk`.

The concrete implementation is `McRegionLevelStorageSource`, constructed in exactly two
places: `Minecraft.Client/Minecraft.cpp:326` and `Minecraft.Client/MinecraftServer.cpp:266`.

Chunks are written as McRegion-era NBT: a `Level` compound holding flat `Blocks` and
`Data` byte arrays (`McRegionChunkStorage.cpp`, via `OldChunkStorage::save`).

### The important subtlety: regions live inside the save blob

`RegionFile` (`Minecraft.World/RegionFile.cpp`) does **not** write to the filesystem. Its
constructor takes a `ConsoleSaveFile *` and does all I/O through `m_saveFile->createFile()`.
Regions are nested inside the single console save file, and the per-chunk payload is
compressed with 4J's LZX/RLE codec (`RegionFile.cpp:281`), not zlib.

So `RegionFile` **cannot be reused** to produce Java-readable `.mca`. Only its sector
allocation *approach* transfers. An early version of this plan assumed otherwise.

## The two formats

| | LCE | Java 26.1.2 |
|---|---|---|
| Block storage | 8-bit tile id + 4-bit metadata | palettized `block_states` per section |
| Height | 0..255, bedrock at 0 | -64..319 |
| Chunk root | `Level` compound | flat, with `DataVersion`, `sections` |
| Entities | in the chunk | separate `entities/` region folder |
| Biomes | per column, 1 byte | 4x4x4 palette per section |
| Compression | 4J LZX/RLE, inside the save blob | zlib, per chunk, in `region/*.mca` |

The block universe is the decisive fact. `CompressedTileStorage` stores 8-bit ids
(`CompressedTileStorage.h:14`), so LCE's whole block space is the 4096-entry pre-1.13
`(id, meta)` space. The registered ids are `Tile.h`'s `*_Id` constants: **1-136, 139-145,
153, 155, 156 and 171** — 147 ids, the 1.6.4 block set. Every one has a known flattened
equivalent, so LCE to Java is total and lossless.

Java to LCE is inherently partial and always will be.

## What has been built

### NBT

LCE's NBT writer was already byte-compatible with Java's: `DataOutputStream` is
big-endian and `writeUTF` emits real modified UTF-8. Two gaps were filled:

- `LongArrayTag.h` — `TAG_Long_Array` (12), which Anvil palettes require. Registered in
  `Tag::newTag`/`getTagName`; `CompoundTag` gained `putLongArray`/`getLongArray`.
- `System::arraycopy` gained an `__int64` instantiation (it is explicitly instantiated
  per type via the `ArrayCopyFunctionDeclaration` macro).

Note `NbtIo` does **not** compress — 4J removed gzip because the save blob was compressed
as a whole. `AnvilRegionFile` therefore does its own zlib framing.

### `AnvilBlockMapping.{h,cpp}`

`(id, meta)` to `"minecraft:name[props]"` and back. All 147 registered ids are covered;
coverage is checked mechanically against `Tile.h` rather than by eye.

The reverse table is *derived from the forward one at runtime* rather than written out a
second time, so the two cannot drift apart.

Unit-tested over the whole 4096-state space: 2336 non-air states, 214 distinct modern
blocks, no null or duplicate properties, no round-trip failures, and unmapped modern
blocks correctly rejected.

**Known approximations** (deliberate, and worth revisiting):

- Huge mushroom faces — legacy metadata encoded which faces show skin; written all-skin.
- Bed and skull variants live in the block entity pre-flattening; bed is written red and
  skull as skeleton.
- Door state is split across halves, so each half fills the other's properties with
  neutral defaults. Java's neighbour update should reconcile them.
- Fences, panes, bars and walls are written unconnected; Java recomputes connections.
- **Rotational metadata for repeaters, levers, buttons and trapdoors is the least certain
  part of the table.** It follows the usual flattening conventions but has not been seen
  in game. If something faces the wrong way after an export, look here first.

### `AnvilRegionFile.{h,cpp}`

A spec-compliant region file on the real filesystem: 4KiB offset table, 4KiB timestamp
table, 4KiB sectors, per-chunk big-endian length + compression byte + zlib payload. Writes
type 2 (zlib); reads 1/2/3 so externally-produced regions load.

Tested standalone: 8 chunks survive a close/reopen cycle; shrinking one chunk and growing
another preserves every neighbour; absent chunks read empty. The resulting `.mca` was then
parsed by an independent Python reader — header sane, no overlapping sector allocations,
every payload inflates, file a whole number of sectors.

Chunks needing more than 255 sectors are refused rather than silently corrupting the
region (Java spills those to an external `.mcc`; LCE chunks cannot get close).

### `AnvilChunkStorage.{h,cpp}`

Implements `ChunkStorage`. Serialises to the 26.1.2 schema — `DataVersion` 4790
(`SharedConstants.WORLD_VERSION`), `xPos`/`yPos`/`zPos`, `Status: minecraft:full`,
`sections` with palettized `block_states` and `biomes`, and `block_entities`.

- **Height mapping:** LCE y is written unchanged as Java y, so bedrock stays at y=0 and
  terrain lands where a Java player expects it. `yPos` is -4 (the overworld's min section);
  sections -4..-1 and 16..19 are simply absent and Java fills them with air.
- **Palette packing** uses the 1.16+ "no straddling" layout: `floor(64/bits)` whole indices
  per long, spare high bits zero. Minimum 4 bits for blocks, 1 for biomes. A single-entry
  palette omits the `data` array entirely, as Java does.
- **Heightmaps and `isLightOn` are deliberately omitted** so Java recomputes them. LCE's
  light arrays cover a different vertical range, and getting the 9-bit heightmap packing
  subtly wrong is worse than letting Java prime them.
- **Biomes:** LCE's 23 biome ids are the vanilla 1.6 ids. Biomes deleted in later versions
  are folded onto their nearest survivor (see `biomeName`). Each 4x4x4 cell takes the
  biome of the column at its centre.

## Replacing the blob

The owner chose to replace the blob outright. That is done, and it turned out far cheaper
than the 51-file estimate, because those 51 files talk to the **`ConsoleSaveFile`
interface**, not to `ConsoleSaveFileOriginal`. Re-implementing the interface moves all of
them at once without any of them changing.

### `NativeSaveFile.{h,cpp}`

A `ConsoleSaveFile` backed by a directory. A `FileEntry` becomes a handle holding a
resolved path and a lazily-opened stdio handle, cached per virtual path so repeated
`createFile()` calls return the same entry - which is what the callers assume.

The virtual paths LCE already used line up with a Java world almost exactly:

| LCE virtual path | on disk |
|---|---|
| `level.dat` | `<root>/level.dat` |
| `data/<id>.dat` | `<root>/data/<id>.dat` |
| `players/<uid>.dat` | `<root>/playerdata/<uid>.dat` |

Only the player folder is renamed, and that translation lives in `resolve()` so the rest
of the game keeps its own naming.

`getFilesWithPrefix()` scans the folder so it can see files this process did not create.
It returns a **heap-allocated** vector because the callers delete it - see
`McRegionLevelStorage.cpp:43`. `getRegionFilesByDimension()` returns an empty one:
region data no longer lives in the save file at all.

### `AnvilLevelStorage.{h,cpp}` and `AnvilLevelStorageSource.{h,cpp}`

Both subclass their `Directory*` equivalents so world enumeration, renaming, deletion,
map data and player IO keep working untouched. Only three things are overridden:

- `createChunkStorage()` returns an `AnvilChunkStorage`, with the nether and end in
  Java's `DIM-1` and `DIM1` folders.
- `level.dat` is written gzipped and Java-shaped: `DataVersion`, a `Version` compound,
  `version` 19133 (Anvil, not McRegion's 19132) and a `WorldGenSettings` block declaring
  all three dimensions. A missing or malformed `WorldGenSettings` fails Java's load.
- `prepareLevel()` sniffs the gzip magic and falls back to reading uncompressed NBT, so a
  world written before this change still opens.

`level.dat` is written to a `.tmp` and renamed over the target, so an interrupted save
cannot leave a half-written world.

### Wiring

Behind `_MEOW_ANVIL_SAVES`, defined in the nine `_WINDOWS64` configurations of both
projects. Two sites:

- `Minecraft.cpp:329` - `levelSource` becomes an `AnvilLevelStorageSource`.
- `MinecraftServer.cpp:426` - `loadLevel` builds `AnvilLevelStorage` over a
  `NativeSaveFile(name)`. Note this function always built its storage directly and only
  ever ignored its `storageSource` argument; that has not changed.

The local there was `shared_ptr<McRegionLevelStorage>` and is now `shared_ptr<LevelStorage>`;
`ServerLevel` already took the base type.

## A bug worth recording

The first version of the chunk writer picked the palette bit width from the palette size
with a minimum of 4, and stopped there. Reading `Strategy.getConfigurationForBitCount`
showed that Java switches a container to the **global palette** once it needs more than 8
bits (blocks) or 3 bits (biomes), and in that mode the packed indices are global registry
ids rather than offsets into the palette written beside them - which this code cannot
produce. A section with more than 256 distinct states would have been silently corrupt.

Both containers are now capped (`ANVIL_MAX_BLOCK_PALETTE`, `ANVIL_MAX_BIOME_PALETTE`) just
below the threshold, and a block overflow is logged rather than passed over in silence.
The block cap is essentially unreachable with LCE content - 4096 blocks drawn from 2336
possible states - but the biome cap is not: a 4x4x4 cell grid samples 16 columns, so a
section straddling many biomes can cross 8, and the surplus cells take the first biome.

The bit widths were then checked against Java's `Mth.ceillog2` rule for every legal
palette size (blocks 2-256, biomes 2-8): no mismatches.

## Where worlds live

One authority, `AnvilSavePaths`, resolves everything:

```
<saves root>/<world name>/level.dat
<saves root>/<world name>/region/r.0.0.mca
<saves root>/<world name>/entities/r.0.0.mca
<saves root>/<world name>/playerdata/<uuid>.dat
<saves root>/<world name>/data/<id>.dat
<saves root>/<world name>/DIM-1/region/...      (nether)
<saves root>/<world name>/DIM1/region/...       (end)
```

The root is a `saves` folder **beside `Minecraft.Client.exe`**, resolved from
`GetModuleFileNameW(NULL, ...)` in `AnvilSavePaths::executableDirectory()`.

It is the executable's directory rather than the process working directory on purpose:
those differ when the game is launched from a debugger or from a shortcut with its own
"start in" folder, and worlds should not move about depending on how it was started.
`anvil.log` is written beside the executable for the same reason.

It is also deliberately **not** derived from `Minecraft::getWorkingDirectory()`.
`File::File(const wstring& parent, const wstring& child)` builds
`pathRoot + separator + parent + separator + child` (`File.cpp:72`), an Xbox convention
where `pathRoot` is `"GAME:"`. On Windows `pathRoot` is empty, so that leading separator
makes the result absolute from the root of the current drive: `getWorkingDirectory()`
yields `\home\minecraft`, and worlds landed in `C:\home\minecraft\saves`.

Before that, the two halves disagreed entirely: `Minecraft.cpp` pointed the level source
at `<working directory>/saves` while `MinecraftServer::loadLevel` built its save file as
`<level name>` relative to the current directory.

`setSavesRoot()` still exists and overrides the default, but nothing calls it now.

### World identity

On console, C4JStorage (`StorageManager`) owns which world is which: the create and load
screens set a save title, and the server opens the selected slot. `StorageManager.Init()`
is inside an `#if 0` spanning lines 909-956 of `Windows64_Minecraft.cpp`, so that whole
layer is dead on Windows x64 — the typed world name went nowhere and the saved-world list
was always empty.

`AnvilSavePaths` supplies the missing piece:

- `setCurrentWorld()` / `getCurrentWorld()` — the world the next server start opens.
  `MinecraftServer::initServer` prefers it over `server.properties`' `level-name`, and
  **falls back to that when it is empty**, which is how the dedicated server still works.
- `makeUniqueWorldName()` — sanitises a typed name into a legal, unused directory name.

`UIScene_CreateWorldMenu` claims a directory next to the dead `SetSaveTitle` call, and
`UIScene_LoadOrJoinMenu` lists `listWorlds()` and sets the current world on selection.
Both are behind `_MEOW_ANVIL_SAVES` with the console path kept under `#else`.

Renaming and deleting worlds from that menu still route through `StorageManager` and are
still dead on Windows.

## Content translation

### Items - `AnvilItemMapping.{h,cpp}`

LCE stores a stack as a numeric id plus an aux value, where the aux is *either*
durability *or* a variant selector (dyes, potions, spawn eggs, discs, coal/charcoal,
golden apples, skulls). Modern Java has a distinct item per variant and keeps durability
in the component patch, so the split is made explicitly by `auxIsVariant()`.

Ids below 256 are block items and resolve through `AnvilBlockMapping`, so there is no
second block table to keep in step. Ids 256-406 are items and 2256-2267 music discs
(note disc 8, "wait", sits at the end of that range rather than in sequence).

The stack shape changed too: `{id: short, Count: byte, Damage: short}` became
`{id: string, count: int, components: {...}}`. Java validates `count` against 1..99 and
rejects the whole stack outside it, so empty slots are dropped rather than written.

The reverse direction exists and matters: the game reads back its own saves, and
`ItemInstance::load` calls `getShort(L"id")`, which casts a `StringTag*` to `ShortTag*`
and reads through it. Without `fromItemStack` a loaded inventory would be garbage. As
with blocks, the reverse table is derived from the forward one at runtime.

Verified over the whole id x aux domain: 4816 mapped states, 378 distinct modern items,
zero round-trip failures. Coverage was diffed against `Item.h` - all 156 registered ids
present, no strays. The only registered id with no mapping at aux 0 is the spawn egg,
which is correct: an egg with no entity is meaningless.

**Not carried over:** LCE's `tag` compound (custom names, enchantments, lore). That is
the pre-1.20.5 shape which the component system replaced wholesale, and there is no
faithful translation. Enchantments in particular are silently lost.

### Entities - `AnvilEntityMapping.{h,cpp}`

LCE writes the pre-1.11 save ids from `EntityIO.cpp` ("Creeper", "PigZombie",
"LavaSlime"). All 43 registrations are mapped, minus the two abstract base entries
("Mob", "Monster") that never appear in a save.

Two wrinkles:

- LCE keeps one `Minecart` entity with a `Type` field; Java has a separate entity per
  variant, so the id is chosen from that field.
- Java identifies every entity by a 128-bit UUID and LCE has none. One is synthesised
  from the entity's id and position bits. This is **deterministic on purpose**: minting
  fresh UUIDs on every save would leave Java thinking the entities are duplicates.

Entities go to `entities/r.x.z.mca` as `{DataVersion, Position: int[2], Entities: [...]}`,
written from `saveEntities()`, which `ServerChunkCache` already calls on the real save
paths. Players are skipped - they belong in `playerdata/`.

### Player data

`AnvilLevelStorage` overrides `save`/`load`/`loadPlayerDataTag` to write gzipped Java
player NBT at `playerdata/<uuid>.dat`. The UUID is derived from LCE's 64-bit XUID by a
pair of mixes, with the version and variant nibbles set so it is a well-formed type-4
UUID rather than something Java's parser rejects - and it is deterministic, so a player
keeps their file across saves.

`Inventory` and `EnderItems` go through the item mapping in both directions, and
`Dimension` converts between LCE's int and Java's namespaced string.

## Reading a world back

The writer was complete well before the reader was, so for a while a reloaded world had
correct terrain but no block metadata, no chest contents and no entities. Three things had
to be added, and they are worth knowing about together:

- **Block properties.** The reverse block table is keyed on the *full* state - name plus
  properties - because everything LCE keeps in its 4 metadata bits (stair and torch
  facing, slab half, log axis, door hinge) lives in the properties. A name-only table
  flattens all of it. `AnvilBlockState::key()` sorts its properties so the key is
  canonical, since on the way back in they arrive from an `unordered_map` in arbitrary
  order. A name-only table remains as a fallback for Java-authored states LCE cannot
  produce exactly: better to keep the block and lose the metadata than drop it.
- **Block entities** come back through `legacyBlockEntityId()` and
  `TileEntity::loadStatic`, with their contents run back through the item mapping.
- **Entities** are read from `entities/r.x.z.mca` by `AnvilChunkStorage::loadEntities()`,
  called from `load()` after the chunk is built.

Both reverse id tables are derived from their forward counterparts so the two directions
cannot drift, and both were diffed against them (20/20 block entities, 43/43 entities).

The block state round trip is checked over the whole 4096-entry space: 2352 non-air
states, 815 distinct, zero failures.

## What is still NOT done

- **Item NBT extras.** Custom names, enchantments and lore are dropped on save - LCE's
  `tag` compound has no faithful translation into the component system - so they cannot
  come back either.
- **A world authored by real Java** loads at the block level, but its entity and block
  entity payloads are not something these loaders understand; only ids and item lists are
  translated, and the rest is assumed to be what LCE itself wrote.
- **Block/fluid ticks and structures** are written as empty.
- **Villager trades, mob equipment beyond the standard lists, and map item data** are
  untranslated.
