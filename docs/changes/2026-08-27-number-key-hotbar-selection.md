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

## Follow-up: both were broken by frame-rate vs tick-rate (same day)

Owner testing found the number keys needed roughly a second between presses, and
that the wheel did not cycle the hotbar at all. One cause for both.

`InputManager.Tick()` — which latches the keyboard snapshot and decrements the
wheel hold counters — runs **once per rendered frame**
(`Windows64_Minecraft.cpp`). The hotbar code runs **once per game tick**, 20Hz.
At 60+ fps most frames are not followed by a game tick, so:

- A key edge (`KeyWentDown`) is true for exactly one frame. If no game tick
  follows that frame, the press is gone. Hence the retry-until-it-lines-up feel.
- A wheel notch lasted `WHEEL_HOLD_TICKS` = 2 *frames*, so it almost never
  survived to a game tick at all. Fully dead, not intermittent.

This had nothing to do with the number-key feature specifically; the wheel path
predates it and was broken the same way from the start.

### Fix

Impulses that gameplay reads now pend until consumed rather than decaying on
frame time — the same shape as 4J's own `Player::ullButtonsPressed`, which is
accumulated per frame and consumed per game tick for exactly this reason.

- `s_pendingHotbarSlot`, set in `OnKeyDown` (guarded by `!s_keyDown[vk]` to
  drop the OS auto-repeat), taken by `Win64Input::ConsumeHotbarSlot`.
- `s_wheelPending`, a signed notch count clamped to +/-9, taken by
  `Win64Input::ConsumeWheelNotch` **one notch per call** — `swapPaint` clamps to
  a single step, so passing a whole flick would drop all but one notch.
  `ClearWheelNotches` drops the backlog when the held item is locked.
- `Minecraft.cpp` gameplay wheel now reads `ConsumeWheelNotch` alongside the
  existing `GetValue` query. It *assigns* to `wheel`, so a notch seen by both
  paths in one tick still moves one slot.

`s_wheelUpTicks` / `s_wheelDownTicks` are unchanged and still drive the
`ACTION_MENU_*_SCROLL` menu actions, which are ticked per frame and so were
never affected.

**Sign:** `ConsumeWheelNotch` returns `+1` for wheel-up; `wheel` follows the pad
convention where wheel-up is `RIGHT_SCROLL` = `-1`, so the call site negates. If
wheel direction is inverted in play, flip that negation — chosen by reasoning
about the existing mapping, not observed.

Rebuilt clean at `Release|x64`. Runtime still unverified by the agent.
