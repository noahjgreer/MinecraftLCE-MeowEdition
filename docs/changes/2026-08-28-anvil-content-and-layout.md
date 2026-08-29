# 2026-08-28 — Anvil: item/entity/player translation, and a real saves layout

Follows `2026-08-28-anvil-replace-the-blob.md`. The owner ran that build, reported it
"seems pretty okay", and asked for the remaining content translation plus a proper
Java-style `saves/<world>/` layout because the storage location felt uncertain.

That uncertainty was a real bug, and it was worse than it looked.

## Three escaped-backslash bugs, one of which meant nothing was saved

Wide-string path literals had lost a backslash each. MSVC treats an unrecognised escape
as the bare character and only warns, so all three compiled and ran:

1. `AnvilChunkStorage.cpp` built its region path with `L"\region\\"`. That leads with
   `\r` — a **carriage return** — so the path was nonsense and every `_wfopen` failed.
   **No chunk was ever written.** The owner's world folder contained only `data/` and
   `playerdata/`; the terrain they saw came from regeneration, not from a load.
2. `AnvilLevelStorage.cpp` used `L"\level.dat"`, so `level.dat` landed *beside* the world
   folder as `Server Worldlevel.dat` rather than inside it. That file was on disk as
   direct evidence.
3. `Win64SaveFile.cpp` had the same fault in `L"\savegame.dat"` — **pre-existing**, from
   the earlier dedicated-server work, and the reason `Server Worldsavegame.dat` sat
   beside the folder too. Fixed as well; it is unused while `_MEOW_ANVIL_SAVES` is on but
   is the fallback path.

Separately, nothing created the `region/` and `entities/` directories, so those opens
would have failed even with a correct path. `openRegion()` now builds the tree first.

There is now an audit that walks every wide-string literal in the Anvil/Native files and
flags any lone backslash. It is what found bugs 2 and 3 after a first pass missed them —
a naive fixer had treated `\r` and `\p` as intentional escapes.

## What changed

New in `Minecraft.World/`:

- `AnvilSavePaths.{h,cpp}` — the single authority for where worlds live.
- `AnvilItemMapping.{h,cpp}` — numeric item id + aux <-> namespaced item id, both ways.
- `AnvilEntityMapping.{h,cpp}` — legacy entity save id -> namespaced id, plus UUID
  synthesis and nested inventory conversion.

Modified:

- `AnvilChunkStorage` — writes `entities/r.x.z.mca`; block entity payloads now go through
  the item mapping; region/entity folders are created; the region opener is shared.
- `AnvilLevelStorage` — overrides `save`/`load`/`loadPlayerDataTag` for Java-format
  player data at `playerdata/<uuid>.dat`.
- `AnvilLevelStorageSource` — `getLevelList()` enumerates the saves folder for real.
- `Minecraft.cpp` publishes the saves root; `MinecraftServer.cpp` resolves through it.

## The layout

```
<saves root>/<world>/level.dat, region/, entities/, playerdata/, data/, DIM-1/, DIM1/
```

Root defaults to `saves` next to the executable; the client sets it from its working
directory during startup. Previously the world list looked in `<working dir>/saves` while
`loadLevel` wrote to `./<name>` — the two never agreed.

## Why the reverse item mapping had to exist

`ItemInstance::load` does `getShort(L"id")`, and `CompoundTag::getShort` casts
unconditionally — a `StringTag*` read through a `ShortTag*`. Writing Java-format
inventories without a way back would have made the game read garbage out of its own
saves. `fromItemStack` closes that, and like the block table it is derived from the
forward mapping at runtime so the two cannot drift.

## What was verified

- `MinecraftPC.sln` `Release|x64` builds clean, lib and exe.
- Item mapping over the whole id x aux domain: 4816 mapped states, 378 distinct modern
  items, **zero round-trip failures**. Coverage diffed against `Item.h`: all 156
  registered ids present, no strays. The single registered id with no aux-0 mapping is
  the spawn egg, which is correct.
- Entity ids checked against `EntityType.java` in the 26.1.2 source for the ones most
  likely to be wrong: `splash_potion` (not `potion`), `oak_boat`, `snow_golem`,
  `zombified_piglin`, `magma_cube`, `mooshroom`, `end_crystal`, the minecart variants.
- Entities chunk shape confirmed from `EntityStorage.java`
  (`{DataVersion, Entities, Position}`) and `Position` as `int[2]` from `ChunkPos.CODEC`.
- `saveEntities()` confirmed to be on the live save path via `ServerChunkCache`.
- Escape audit clean across all Anvil/Native sources.

## What is unverified

**All runtime behaviour**, as always — an agent cannot launch the game.

Worth knowing: because chunks were never actually written before this, the previous
build's Anvil chunk writer has still never produced a file. Everything downstream of
`AnvilChunkStorage::save` — palette packing, section layout, biome containers — is being
exercised for the first time on the next run.

Item `tag` extras (custom names, **enchantments**, lore) are dropped; that is a real,
deliberate loss, not an oversight.

## Watch out for

- Existing worlds from the previous build have a misplaced `level.dat` (`<name>level.dat`
  beside the folder) and no region data. They will not migrate; delete and recreate.
- The synthesised entity and player UUIDs are derived from LCE identity and position. If
  an entity moves, it gets a new UUID next save. That is acceptable for entities Java
  only ever sees statically, but it is not a stable identity across sessions.
