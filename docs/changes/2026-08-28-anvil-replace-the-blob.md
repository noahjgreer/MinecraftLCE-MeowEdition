# 2026-08-28 — Anvil save format: replacing the save blob

Follows on from `2026-08-28-anvil-save-format-foundation.md`, which built the block
mapping, region writer and chunk schema but wired none of it in. The open question there
was how to deal with `ConsoleSaveFile`. The owner chose **option 2: replace the blob**.

## The estimate was wrong, in a good way

That note put the cost at "touches all 51 files". It does not. Those 51 files talk to the
**`ConsoleSaveFile` interface**, not to `ConsoleSaveFileOriginal`. Re-implementing the
interface over a directory moves every one of them at once, and none of them changed.

## What changed

New in `Minecraft.World/`:

- `NativeSaveFile.{h,cpp}` — `ConsoleSaveFile` backed by a real directory. A `FileEntry`
  becomes a cached handle (resolved path + lazily-opened stdio handle) rather than a byte
  range in a blob.
- `AnvilLevelStorage.{h,cpp}` — subclasses `DirectoryLevelStorage`; overrides
  `createChunkStorage` (Anvil, with `DIM-1`/`DIM1`) and the `level.dat` read/write.
- `AnvilLevelStorageSource.{h,cpp}` — subclasses `DirectoryLevelStorageSource`; overrides
  `selectLevel` and `getDataTagFor`.

Modified:

- `Minecraft.Client/Minecraft.cpp:329` — `levelSource` becomes `AnvilLevelStorageSource`.
- `Minecraft.Client/MinecraftServer.cpp:426` — `loadLevel` builds `AnvilLevelStorage` over
  a `NativeSaveFile(name)`. Its local was `shared_ptr<McRegionLevelStorage>` and is now
  `shared_ptr<LevelStorage>`; `ServerLevel` already took the base type.
- `AnvilChunkStorage.{h,cpp}` — palette caps, see the bug below.
- Both `.vcxproj` files — `_MEOW_ANVIL_SAVES` added to the nine `_WINDOWS64` configurations.

Everything is behind `_MEOW_ANVIL_SAVES`; removing the define restores the McRegion path
exactly. Console configurations do not define it and the new files are `#ifdef _WINDOWS64`
throughout, so the 7th-gen targets are untouched.

## Why subclassing rather than replacing

`DirectoryLevelStorage` and `DirectoryLevelStorageSource` already carry the world list,
rename, delete, map-data and player-IO machinery. Only three behaviours actually differ
for Anvil, so those are the only three overridden. This is much less code than a fresh
`LevelStorageSource` and keeps the world-select UI working by construction.

## The bug this turned up

The chunk writer picked the palette bit width as `max(ceil(log2(size)), 4)` and stopped
there. Reading `Strategy.getConfigurationForBitCount` in the 26.1.2 source showed Java
switches a paletted container to the **global palette** once it needs more than 8 bits
(blocks) or 3 bits (biomes) — and in that mode the packed indices are global registry ids,
not offsets into the palette written beside them, which this code cannot produce. A
section with more than 256 distinct block states would have been silently corrupt.

Both containers are now capped just below the threshold. A block overflow is logged. The
block cap is effectively unreachable with LCE content (4096 blocks drawn from 2336
possible states); the biome cap is reachable, since a 4x4x4 cell grid samples 16 columns
and a section straddling many biomes can exceed 8 — the surplus cells take the first biome.

Also fixed while wiring: `getFilesWithPrefix()` originally returned a pointer to a member
vector, but the callers `delete` what they get back (`McRegionLevelStorage.cpp:43`). It
now returns a heap-allocated vector.

## What was verified

- `MinecraftPC.sln` at `Release|x64` builds clean, lib and exe, with the cutover active.
- Palette bit widths were checked against Java's `Mth.ceillog2` rule for every legal
  palette size — blocks 2-256, biomes 2-8 — with no mismatches, and both caps confirmed to
  sit exactly at the global-palette threshold.
- `SimpleBitStorage` was read directly to confirm the packing layout: `valuesPerLong =
  64/bits`, `requiredLength = ceil(size/valuesPerLong)`, low-order-first within each long,
  and a wrong `data` length is a hard throw. All three match.
- `PalettedContainer.codec` confirmed `data` is a `lenientOptionalFieldOf`, so omitting it
  for a single-entry palette is correct.
- Region container and block mapping tests from the previous note still pass.

## What is unverified

**All runtime behaviour.** An agent cannot launch the game. Nothing here has ever written
a world, and no chunk this produces has been opened by Java.

The riskiest untested areas, in order:

1. **`WorldGenSettings`.** Hand-built from the 26.1.2 schema. Java fails the whole load if
   it is malformed, so this is the most likely thing to stop a world opening.
2. **`NativeSaveFile` semantics.** The game's expectations of `setFilePointer`/`readFile`/
   `closeHandle` are inferred from the stream wrappers, not documented. In particular
   `readFile` returns TRUE with a zero count at EOF, which is how the wrappers detect it.
3. **Rotational block metadata** — unchanged from the previous note, still the least
   certain part of the mapping table.

## Watch out for

- **This does not read existing console-format saves.** With the macro on, the blob path
  is skipped entirely and `initData->saveData` is ignored. Existing worlds are not
  migrated, and there is no converter yet.
- `NativeSaveFile` keeps file handles open for the lifetime of the world; `closeHandle`
  deliberately only flushes. Worth revisiting if handle counts ever matter.
- `ConsoleSaveFileInputStream::read(byteArray)` and the matching `write` pass `&b.data` —
  the address of the pointer, not the buffer. Pre-existing, and those overloads appear
  unused, but they will corrupt memory if anything starts calling them.
