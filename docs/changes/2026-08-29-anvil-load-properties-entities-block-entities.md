# 2026-08-29 — Reloading a world lost block properties, chests and entities

Reported after the load path started working: reopening a world gave the right terrain but
wall torches and stairs faced the wrong way, chests were empty, and every entity was gone.

All three were the same shape of mistake — **the writer was complete and the reader was
not**. Nothing was ever missing from disk. Confirmed by parsing a real save with an
independent Python reader before changing anything:

```
block_entities by id: {'minecraft:chest': 47, 'minecraft:mob_spawner': 8}
with non-empty Items: 11
sample Items[0] = {'Slot': 1, 'id': 'minecraft:melon_seeds', 'count': 3}

entities total: 31
by id: {'minecraft:item': 13, 'minecraft:sheep': 6, 'minecraft:pig': 4,
        'minecraft:wolf': 4, 'minecraft:squid': 3, 'minecraft:cow': 1}
```

## 1. Block properties

`AnvilBlockMapping`'s reverse table was keyed on the block **name alone**:

```cpp
const wstring name = state.name;
if (s_reverse->find(name) == s_reverse->end()) (*s_reverse)[name] = (id << 4) | meta;
```

First (id, meta) wins, so every `minecraft:oak_stairs` came back as whichever metadata was
generated first — facing east, bottom half. Everything LCE keeps in its 4 metadata bits
(stair and torch facing, slab half, log axis, door hinge, lever orientation) was flattened
on load. The loader never even read the `Properties` compound.

There are now two reverse tables, both still derived from the forward mapping so they
cannot drift:

- `s_reverseByState` keyed on the **full** state — name plus properties.
- `s_reverseByName` as a fallback, for a Java-authored state whose exact property
  combination LCE cannot produce. That keeps the right block and loses only the metadata,
  rather than dropping the block entirely.

`AnvilBlockState::key()` now **sorts** its properties. It has to be canonical: on the way
back in the properties come out of an `unordered_map` inside `CompoundTag`, in arbitrary
order. `stateKey()` builds the same canonical form from what the loader read.

`fromChunkTag` reads the `Properties` compound of each palette entry and passes it through.

## 2. Block entities

`fromChunkTag` never read `block_entities` at all. A chest was still a chest *block*, but
with no `TileEntity` behind it, so it opened empty.

It now reads the list, maps the id back with the new `legacyBlockEntityId()`
(`minecraft:chest` -> `Chest`, which is what `TileEntity::loadStatic` matches on), runs the
contents back through `AnvilItemMapping::convertItemListFromJava`, and calls
`chunk->addTileEntity()`.

## 3. Entities

Entities are written to `entities/r.x.z.mca` by `saveEntities()`, and nothing read them
back. `AnvilChunkStorage::loadEntities()` now does, from `load()` after the chunk is built:
id back to the legacy form via `AnvilEntityMapping::toLegacyEntityId()`, inventories back
through the item mapping, the synthesised `UUID` dropped, then `EntityIO::loadStatic()` and
`chunk->addEntity()`, mirroring `OldChunkStorage::loadEntities`.

Minecarts need care in both directions: LCE has one `Minecart` entity with a `Type` field
and Java has three separate entities, so `convertEntityFromJava` restores `Type` from
which modern id it found.

## Verified

- `MinecraftPC.sln` `Release|x64` builds clean, lib and exe.
- **Block state round trip over the whole 4096-entry space**: 2352 non-air states, 815
  distinct, **0 not-found, 0 state mismatches**. The harness deliberately hands the
  properties back in reverse order to prove the canonical sort works.
- An unknown property set still resolves to the right block via the name fallback.
- Reverse id tables diffed against their forward functions: block entities 20/20, entities
  43/43, nothing missing and nothing stray.
- Item ids actually present in the save reverse correctly — `minecraft:melon_seeds` -> 362,
  `minecraft:chest` -> 54, `minecraft:cobblestone` -> 4.

## Unverified

Not run in game. In particular `TileEntity::loadStatic` and `EntityIO::loadStatic` are
being handed tags that have been through two conversions, and only the id and item fields
were translated — anything else inside a block entity or entity payload is passed through
as LCE wrote it, which is correct only because LCE wrote it in the first place. A world
authored by real Java would carry payloads neither loader understands.

## Still not translated

Item `tag` extras — custom names, **enchantments**, lore — are still dropped on save, so
they cannot come back. Villager trades, mob equipment beyond the standard lists, and map
item data are likewise untranslated.
