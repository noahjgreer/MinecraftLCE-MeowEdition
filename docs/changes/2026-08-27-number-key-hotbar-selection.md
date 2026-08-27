# Number keys select a hotbar slot (Windows x64)

Date: 2026-08-27

## What changed

The number row and numpad `1`-`9` now select hotbar slots 1-9 directly on the
Windows x64 build. Mouse-wheel hotbar cycling already worked — the wheel drives
`MINECRAFT_ACTION_LEFT_SCROLL` / `RIGHT_SCROLL` through `ActionHasWheel` — so no
change was needed there.

## Why it is not a binding table entry

`s_bindings[]` in `Win64KeyboardMouse.cpp` maps a key to a 4J *action*, and
actions are relative/boolean. There is no action id meaning "select slot 5", so
a table entry cannot express this. Instead:

- `Win64Input::GetHotbarSlot(int &iSlot)` — edge query over the existing
  per-tick key snapshot (`KeyWentDown`), returning the 0-based slot. Being an
  edge query against the snapshot it is idempotent within a tick, so it cannot
  fire twice for one keypress even though it is not literally consumed.
- `Minecraft::tickInput` (`Minecraft.cpp`, immediately after the wheel /
  `swapPaint` block) writes `player->inventory->selected` and then does the same
  two side effects the wheel path does: `Tutorial::onSelectedItemChanged` and
  `Player::updateRichPresence`. It does *not* touch `options->flySpeed`, which
  is a wheel-only behaviour and meaningless for an absolute pick.

Writing `selected` directly is equivalent to what the wheel does:
`Inventory::swapPaint` only clamps and assigns `selected`, nothing else.

Gated on `isInputAllowed(MINECRAFT_ACTION_LEFT_SCROLL)` — same input class, so a
game mode that forbids changing the held item forbids both.

## Files

- `Minecraft.Client/Windows64/Win64KeyboardMouse.h` — declares `GetHotbarSlot`
- `Minecraft.Client/Windows64/Win64KeyboardMouse.cpp` — implements it
- `Minecraft.Client/Minecraft.cpp` — applies it, inside `#if defined(_WINDOWS64)`
- `docs/systems/windows-keyboard-mouse-input.md` — controls table + gotcha

7th gen is untouched: the game-side hook is inside `_WINDOWS64` and the rest
lives in the Windows-only input file.

## Verified / unverified

- **Verified:** `MinecraftPC.sln` `Release|x64` builds clean.
- **Unverified:** all runtime behaviour. No agent can run the game.
