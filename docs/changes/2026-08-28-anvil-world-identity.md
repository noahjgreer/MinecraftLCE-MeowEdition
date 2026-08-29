# 2026-08-28 — Worlds have names again: replacing StorageManager's role on Windows

Three reported symptoms, one root cause:

1. Every world was created as `Server World`, whatever name was typed.
2. Saved worlds never appeared in the select-a-world list.
3. Creating a "new" world loaded the previous one — same seed, items, health, position.

## The cause

`MinecraftServer::initServer` takes its level name from `server.properties`:

```cpp
wstring levelName = settings->getString(L"level-name", L"world");
```

`server.properties` in the build folder says `level-name=Server World`, so **every** world
resolved to the same directory. Loading that directory found the previous world's
`level.dat` and chunks, which is symptom 3 — and confirms the loader now works, since it
was reading a real world back.

The name typed into the create screen does exist, at
`UIScene_CreateWorldMenu.cpp` — but its only destination is:

```cpp
StorageManager.ResetSaveData();
StorageManager.SetSaveTitle((wchar_t *)wWorldName.c_str());
```

On console, C4JStorage owns world identity: the create/load screens set a save title and
the server opens the selected slot. `StorageManager.Init(...)` is inside an `#if 0`
spanning lines 909-956 of `Windows64_Minecraft.cpp`, so **none of that layer runs on
Windows x64**. The name went nowhere, the world list (`StorageManager.ReturnSavesInfo()`)
was always empty, and rename/delete are `#if`-guarded to consoles anyway.

So Windows had no world-identity plumbing at all. This change supplies it.

## What changed

`AnvilSavePaths` gains the concept C4JStorage was providing:

- `setCurrentWorld()` / `getCurrentWorld()` — the world the next server start should open.
  **Empty means "nothing chose one"**, and `MinecraftServer` then falls back to
  `server.properties`, which is exactly what the dedicated server wants. That fallback is
  why this does not disturb the headless path.
- `makeUniqueWorldName()` — turns a typed name into a directory name that is legal on
  Windows and not already taken.

Hooks, all behind `_MEOW_ANVIL_SAVES`:

- `MinecraftServer::initServer` prefers `getCurrentWorld()` over `server.properties`.
- `UIScene_CreateWorldMenu` claims a directory beside the dead `SetSaveTitle` call.
- `UIScene_LoadOrJoinMenu::GetSaveInfo` lists `AnvilSavePaths::listWorlds()` instead of
  the empty StorageManager result, and selecting one sets the current world and hosts.
  The console path is kept intact under `#else`.

`m_anvilWorlds` holds the directory names in list order. The index arithmetic reuses the
existing `childId - m_iDefaultButtonsC` convention; `m_iDefaultButtonsC` is
1 ("Create New World") plus the level generators, and worlds are appended after those.

## Name sanitising

World names are user text going straight into a directory name, so
`makeUniqueWorldName()` strips `\ / : * ? " < > |` and control characters, trims leading
and trailing spaces and dots (Windows silently mangles those), substitutes `World` for an
empty result, and suffixes reserved DOS device names (`CON`, `NUL`, `COM1`...) which
cannot be directory names. Then it appends ` (2)`, ` (3)` … until the directory is free.

Verified in a standalone harness:

```
My World          -> "My World"
bad/name\here     -> "badnamehere"
con               -> "con_"
  spaced          -> "spaced"
...               -> "World"
(empty)           -> "World"
a:b*c?d"e<f>g|h   -> "abcdefgh"
Ends with dot.    -> "Ends with dot"

Dup -> "Dup", then "Dup (2)", then "Dup (3)"
getCurrentWorld() defaults to empty
```

## Verified

- `MinecraftPC.sln` `Release|x64` builds clean, lib and exe.
- Name sanitising, uniqueness and the empty-default above, in a standalone harness.
- `m_iDefaultButtonsC` accounting read directly from `AddDefaultButtons()` rather than
  assumed, since a wrong offset would load the wrong world.
- Escape audit clean.

## Unverified

Not run. In particular the load-menu changes are in a Flash/Iggy-driven scene that cannot
be exercised here, and the rewritten chunk loader still has not been round-tripped in
game.

Renaming and deleting worlds from that menu still route through StorageManager and remain
dead on Windows; only listing and selecting were replaced.
