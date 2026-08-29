# 2026-08-28 — Anvil: the saves went to C:\home, and loading a world crashed

Two separate faults behind one report ("world doesn't save, and no save folder").
Neither was what it looked like: **saving worked perfectly the whole time.**

## The saves were real, just at the root of the drive

Diagnostic logging (new, see below) gave it away immediately:

```
[anvil] saves root set to "\home\minecraft\saves" (cwd "...\bin\x64\Release")
```

That leading backslash makes it an absolute path from the root of the current drive.
`Minecraft::getWorkingDirectory()` returns `File(L"home", L"minecraft")`, and
`File::File(const wstring& parent, const wstring& child)` builds

```cpp
m_abstractPathName = pathRoot + pathSeparator + parent + pathSeparator + child;
```

(`File.cpp:72`). `pathRoot` is `"GAME:"` on Xbox and **empty everywhere else**, so on
Windows that leading separator turns a relative path into `\home\minecraft`. Worlds were
landing in `C:\home\minecraft\saves\<name>` — complete and correct, just nowhere anyone
would look.

The saves root is now resolved by `AnvilSavePaths::executableDirectory()` from
`GetModuleFileNameW(NULL, ...)`, giving a `saves` folder **beside
`Minecraft.Client.exe`** - the executable's own directory, not the process working
directory, so worlds do not move about depending on whether the game was launched by
double-click, from a debugger, or from a shortcut with its own "start in" folder.
`anvil.log` is written beside the executable for the same reason.
`getWorkingDirectory()` is deliberately not used; the comment at the call site explains why.

Verified with a standalone harness: run normally and run again with the working directory
forced to `C:\Windows`, the resolved root is identical and the log lands next to the
executable both times.

## Everything that was written is valid

The world at `C:\home\minecraft\saves\Server World` was parsed with an independent Python
NBT/region reader:

- `level.dat` — gzipped, `DataVersion` 4790, `version` 19133, `Version` compound,
  `WorldGenSettings` with all three dimensions and the seed. Correct.
- `region/r.0.0.mca` — 1023 chunks. Root keys `xPos/yPos/zPos` (yPos -4),
  `Status: minecraft:full`, sections Y 0-4, block entities present.
- Section palettes decode cleanly and the packed `data` length matches the
  `ceillog2` bit width exactly (11 entries -> 4 bits -> 256 longs).
- Single-entry biome palette correctly omits its `data` array.
- Decoding a vertical column gives textbook terrain: bedrock 0-2, stone, dirt,
  grass_block at 62, air above, gravel and lava pockets in between.

So the writer — block mapping, ordering, palette packing, section layout, region
container — is correct end to end. That column dump is the strongest evidence yet, since
a wrong block ordering would have produced visible nonsense.

`entities/`, `playerdata/<uuid>.dat`, `DIM-1/`, `DIM1/` and `data/` were all written too.

## The crash: loading a world had never run before

The previous build could not read a world back, because `level.dat` had been written to a
broken path — so `prepareLevel()` always returned NULL and every launch generated fresh.
Fixing the path meant the **load** path executed for the first time, and it crashed.

`LevelChunk`'s constructor leaves `lowerData`, `lowerSkyLight` and `lowerBlockLight` as
NULL; they are only allocated by the bulk setters (`setDataData`, `setSkyLightData`,
`setBlockLightData`). `fromChunkTag` was calling `setTileAndData()` per block, which
writes straight through those null pointers.

The loader now decodes the section palettes into flat arrays and hands them over in one
go, the way `OldChunkStorage::load` does. LCE's linear index is documented in
`CompressedTileStorage::getIndex` as the bit layout `xxxxzzzzyyyyyyy`, i.e.
`x * 2048 + z * 128 + y` within each 128-block half, upper half offset by
`COMPRESSED_CHUNK_SECTION_TILES`.

Three other things the loader was missing, each of which would have caused its own
problem once the null deref was gone:

- **Lighting arrays** were never allocated. Now zeroed, then `setSkyLightDataAllBright()`.
- **The heightmap** was left zeroed. Now `recalcHeightmap()`.
- **`terrainPopulated` was 0**, so the generator would have treated every loaded chunk as
  new and populated it a second time — a fresh set of ores and structures dropped into an
  already-built world. Now set to
  `sTerrainPopulatedAllNeighbours | sTerrainPostPostProcessed`.

Biomes are deliberately left at their unset marker so the biome source recomputes them,
which is exactly what `OldChunkStorage` does for a chunk with no `Biomes` array.

## Diagnostics

`AnvilSavePaths::log()` appends to `anvil.log` in the working directory and mirrors to
`OutputDebugString`. The save path had no visible tracing on a client build —
`app.DebugPrintf` only reaches a console when the dedicated server is running — which is
why a silent misdirected write was impossible to locate. It logs the saves root and real
cwd, whether `loadLevel` was reached, the resolved world directory, whether that directory
was created (with `GetLastError`), the first few chunk writes, and the `level.dat` write.

Cheap and clearly marked; delete once the save path is trusted.

## Also worth knowing: the world list cannot work yet

The saved-worlds list is fed by `StorageManager.ReturnSavesInfo()`
(`UIScene_LoadOrJoinMenu.cpp`), the vendored 4J save library — **not** by
`LevelStorageSource::getLevelList()`. `StorageManager.Init(...)` sits inside an `#if 0`
spanning lines 909-956 of `Windows64_Minecraft.cpp`, so it is never initialised on
Windows x64 and that list is structurally always empty.

This is pre-existing and unrelated to the Anvil work.
`AnvilLevelStorageSource::getLevelList()` does enumerate the saves folder correctly, but
nothing on the Flash/Iggy UI path calls it. Pointing the load menu at it is a separate
piece of work and needs the owner's call on how much of that UI to touch.

## Verified

- `MinecraftPC.sln` `Release|x64` builds clean, lib and exe.
- The on-disk world was validated with an independent Python reader (above).
- The `File.cpp:72` path construction was read directly, not inferred.
- The null members were confirmed in `LevelChunk::LevelChunk` and `LevelChunk::init`.

## Unverified

The rewritten load path has not been run. The block ordering it reverses is taken from
the `getIndex` bit-layout comment rather than from a round trip, so **a load that produces
shifted or scrambled terrain would point straight at that index formula.**
