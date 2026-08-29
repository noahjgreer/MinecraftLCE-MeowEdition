# 2026-08-28 — Anvil (Java Edition) save format: foundation

## Goal

Move LCE off its own save system and onto the format Java Edition 26.1.2 uses, so worlds
can be carried between the two. Owner's decisions up front: **native Anvil, export-first**
(Java-to-LCE import is best-effort), and **LCE y maps unchanged to Java y** so bedrock
stays at y=0.

This change lands the foundation and the chunk format. It does **not** switch the running
game over — see "What is left" below, which includes a decision for the owner.

## What changed

New files in `Minecraft.World/`:

- `LongArrayTag.h` — `TAG_Long_Array` (12). Anvil section palettes store their bit-packed
  block indices as a `long[]` and LCE's NBT had no such tag.
- `AnvilBlockMapping.{h,cpp}` — `(tileId, meta)` to modern namespaced block state and back.
- `AnvilRegionFile.{h,cpp}` — spec-compliant `.mca` on the real filesystem, zlib per chunk.
- `AnvilChunkStorage.{h,cpp}` — `ChunkStorage` implementation writing the 26.1.2 chunk
  schema (`DataVersion` 4790).

Modified:

- `Tag.h` / `Tag.cpp` — register `TAG_Long_Array` in `newTag` and `getTagName`.
- `CompoundTag.h` — `putLongArray` / `getLongArray`.
- `ArrayWithLength.h` — `longArray` typedef.
- `System.h` / `System.cpp` — `__int64` instantiation of `arraycopy` (it is explicitly
  instantiated per type, so `longArray` copies did not compile without it).
- `Minecraft.World.vcxproj` — the three new `.cpp` files.

Nothing existing changed behaviour: the new code is additive and unreferenced by the game.

## Why these shapes

**The block mapping is the heart of it.** `CompressedTileStorage` stores 8-bit tile ids,
so LCE's entire block universe is the 4096-entry pre-1.13 `(id, meta)` space, and the
registered ids (`Tile.h`'s `*_Id` constants) are the 147-strong 1.6.4 block set. That makes
LCE-to-Java a total, lossless function rather than an open-ended problem.

The **reverse table is derived from the forward one at runtime**, not hand-written twice,
so the two cannot drift apart as the table is corrected.

**`RegionFile` could not be reused.** It writes into the `ConsoleSaveFile` blob and
compresses with 4J's LZX/RLE codec, so it cannot produce Java-readable `.mca`. An earlier
version of this plan assumed it could; `AnvilRegionFile` is a fresh implementation.

**Heightmaps and `isLightOn` are omitted on purpose** so Java recomputes them, rather than
risking a subtly wrong 9-bit heightmap packing.

## What was verified

- `MinecraftPC.sln` at `Release|x64` builds clean — both `Minecraft.World.lib` and
  `Minecraft.Client.exe`. (The `LNK4099 vc110.pdb` warning is pre-existing VS2012
  middleware noise.)
- The block mapping was unit-tested over all 4096 states in a standalone harness: 2336
  non-air states, 214 distinct modern blocks, no null or duplicate properties, no
  round-trip failures, unmapped modern blocks correctly rejected. Case coverage was also
  diffed mechanically against `Tile.h` — 147 of 147, no typos, no strays.
- `AnvilRegionFile` was tested standalone: 8 chunks survive close/reopen; shrinking one
  chunk and growing another preserves every neighbour; absent chunks read empty. The
  `.mca` it produced was then parsed by an independent Python reader — no overlapping
  sector allocations, every payload inflates, whole number of sectors.

## What is unverified

**Everything about how this behaves in the game.** An agent cannot launch it. In
particular no chunk produced by `AnvilChunkStorage` has ever been opened by Java, because
nothing calls it yet.

The **rotational metadata** for repeaters, levers, buttons and trapdoors is the least
confident part of the mapping — it follows the usual flattening conventions but has not
been seen in game. If something faces the wrong way, look there first.

## What is left

Not started: `level.dat` (needs a modern `WorldGenSettings` with a `dimensions` map),
entities (separate `entities/` region folder), and **item id translation** — block entity
ids are converted but their payloads still hold LCE's numeric item ids, so chests will
come out wrong in Java.

**Decision needed before the cutover.** The plan assumed swapping the storage source at
its two construction sites would be enough. It is not: `ConsoleSaveFile` appears 299 times
across 51 files, including the load menus, thumbnails and debug overlay — it is the
"a world is one blob" abstraction the UI is built on. `docs/systems/anvil-world-format.md`
lays out the three options; the short version is that option 1 (Anvil chunks bypass the
blob, UI keeps its blob) is the cheapest path to something Java can open, and option 2
(replace the blob outright) is what a full format switch actually costs.

## Watch out for

- `NbtIo` does not compress — 4J removed gzip because the whole blob was compressed.
  Anything writing Java-facing NBT must do its own zlib framing, as `AnvilRegionFile` does.
- `System::arraycopy` is explicitly instantiated per type. A new `arrayWithLength<T>` needs
  a matching `ArrayCopyFunctionDeclaration`/`Definition` pair or it will not link.
- `AnvilChunkStorage::load` returns chunks with lighting unset, relying on the caller to
  relight. That is fine for Java's loader but is a real consideration if this ever becomes
  the live backend.
