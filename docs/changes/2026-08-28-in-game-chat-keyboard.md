# 2026-08-28 — In-game chat opens with T and /

There was no way to open chat on the Windows x64 build, which also meant no way to
type the `/cpm` command added by the
[Custom Player Models port](2026-08-28-custom-player-models-port.md).

## Why there was nothing to enable

Three separate gaps, not one:

1. **LCE has no chat action.** `MINECRAFT_ACTION_*` in `Common/App_enums.h` has no
   chat entry — the console builds never had typed chat, so there was nothing for the
   Windows key mapping table to bind a key to.
2. **`ChatScreen` was never reachable.** The only `setScreen(new ChatScreen())` in the
   tree is inside a `#if 0` block of Java-derived code in `Minecraft.cpp` (~line 3800)
   that is not valid C++ and has never compiled.
3. **`Screen::keyboardEvent` was still a TODO.** Nothing on Windows ever called
   `Screen::keyPressed`, so even if `ChatScreen` had been opened it could not have
   been typed into.

`ChatScreen` itself is intact — `render`, `keyPressed` and `mouseClicked` are all live
code, not `#if 0`'d — so this wires up what was already there rather than writing a
chat UI.

## What changed

- `Windows64/Win64KeyboardMouse.{h,cpp}` — `T` and `/` pend a chat request, collected
  by `ConsumeChatRequest`. This follows the existing `ConsumeHotbarSlot` pattern
  because the key edge happens per rendered frame while gameplay input runs at 20Hz.
  Deliberately **not** routed through the `4J_Input` action table: there is no chat
  action to add, and adding one would mean changing an enum the prebuilt
  `4J_Input.lib` has its own copy of.
- `Screen.cpp` — `updateEvents` now drains typed characters and calls `keyPressed`,
  mapping the control characters WM_CHAR delivers onto `Keyboard::KEY_ESCAPE`,
  `KEY_RETURN` and `KEY_BACK`. It stops early if the screen closed itself mid-drain.
- `ChatScreen.{h,cpp}` — claims the keyboard on `init` and releases it on `removed`;
  new constructor taking an initial message, used for the `/` prefill.
- `Minecraft.cpp` — consumes the request in the gameplay input block and opens
  `ChatScreen`.

### Gating

Key events go **only** to a screen that has claimed the keyboard for text
(`Win64Input::IsTextInputActive`). Delivering them to every `Screen` would have been
the more obvious change, but `Screen::keyPressed`'s base implementation closes the
screen on Escape, which would then fight the pause menu and the menu-cancel action on
`TitleScreen` and every other ported screen. Restricting it to text screens means no
existing screen changes behaviour at all.

`SetTextInputActive(true)` also stops the keyboard driving the player and the menus, so
typing "was" while chat is open does not walk the player. Mouse look was already
suppressed — `Win64ApplyMouseLook` returns early when `screen != NULL`.

The request is not recorded while a text field or a menu is up, so a `T` typed
elsewhere cannot sit pending and open chat later.

## Bugs fixed on the way

Two, and the second was caused by fixing the first.

**1. The character filter was always true.** `ChatScreen::keyPressed` filtered with
`allowedChars.find(ch) >= 0`. `wstring::find` returns `npos` (a large unsigned
value), not `-1`, so that test passed every character. Harmless while nothing called
`keyPressed`; with real input it would have appended backspace and return into the
message. Changed to compare against `wstring::npos`.

**2. `allowedChars` was empty for the entire run.** It was a namespace-scope static:

```cpp
const wstring ChatScreen::allowedChars = SharedConstants::acceptableLetters;
```

`SharedConstants::acceptableLetters` is *also* a namespace-scope static, and it is not
filled in until `SharedConstants::staticCtor()` runs from `Minecraft.World.cpp` — long
after this copy is taken. So `allowedChars` was permanently `L""`.

A static initialisation order bug, invisible for as long as bug 1 hid it: the empty
table was never actually consulted. Fixing bug 1 made the empty table authoritative,
and chat then **rejected every keystroke** — the bar opened and typing did nothing.

Both are now fixed by reading `SharedConstants::acceptableLetters` at the point of
use rather than snapshotting it, with control characters excluded explicitly and an
empty table falling back to "accept printable" so this can never present as silent
muteness again.

`TextEditScreen.cpp` had the identical snapshot, already compared against `npos`, so
it would have rejected everything too. It is unreachable — its only call site in
`LocalPlayer.cpp` is commented out — so nothing can regress; fixed the same way.

**Worth internalising:** a namespace-scope static must never be initialised from
another translation unit's namespace-scope static. This codebase builds several
tables in explicit `staticCtor()` functions precisely because of that, and copying
one at static-init time silently gets the pre-`staticCtor` value.

## Verification

**Built:** `MinecraftPC.sln` `Release|x64`, exit 0, no new warnings.

**Tested by the owner:** chat opens on `T` and the input bar renders over the HUD.
The first build could not be typed into — that was the static-initialisation bug
above, now fixed.

**Still unverified after the fix:** that characters now appear, that Return sends and
Escape cancels, and that the 20Hz `updateEvents` drain keeps up with fast typing.

Remaining known limits:

- **`/` is `VK_OEM_2`**, the `/` key on a US layout and something else on several
  others. `T` always works, and the slash can then be typed normally.
- Player 1 only, like the rest of the keyboard/mouse support. `ChatScreen` sends
  through `minecraft->player`, not the pad's player.

## Watch out for

- `Minecraft::setScreen` does **not** call `InputManager.SetMenuDisplayed` — that code
  is commented out. Anything that assumes "a Screen is up" implies "a menu is
  displayed" is wrong; mouse look happens to check `screen != NULL` separately.
- `Minecraft::handleClientSideCommand` is an empty TODO returning false. `ChatScreen`
  already calls it before `player->chat`, so it is the natural home for future
  client-side commands. `/cpm` is currently intercepted in
  `MultiplayerLocalPlayer::chat` instead, which catches every chat path including
  `InBedChatScreen`.
