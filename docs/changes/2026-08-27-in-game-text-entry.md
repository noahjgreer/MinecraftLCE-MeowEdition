# 2026-08-27 — Text entry happens inside the game

## What changed

Typing text no longer opens a separate window, and menu text fields are no longer
inert. A player with a keyboard now types straight into the field that is already
on screen — the world name, the seed, a sign, the anvil — and watches it change
character by character, with a caret.

Concretely:

1. **`InputManager.RequestKeyboard` works on Windows for the first time.** It is
   the console on-screen keyboard from the prebuilt `4J_Input.lib`, with no PC
   implementation, so all ~15 text fields in the game did nothing on this
   platform. `C_Win64Input` now shadows `RequestKeyboard` and `GetText` and hands
   the request to a new in-place editor.
2. **"Join Server" lost its Win32 popup.** It is now two chained
   `RequestKeyboard` calls (address, then name), the same shape every other
   keyboard caller in the game has. `Win64TextPrompt.{h,cpp}` was deleted.
3. **`UIScene_Keyboard` was finished.** 4J built the scene and its movies and
   never connected them: hardcoded title, hardcoded 15-character limit, a Done
   handler that was a `ToDo` comment. It now serves any request that has no
   on-screen field to type into — a pad player, renaming a save, the main menu.
4. **The keyboard stops driving the game while a field is open**, so typing a
   world name cannot also walk the player and press buttons. The pad is
   unaffected.

## Why

The request was: text entry should be like the world-name field — in the game,
typed with the keyboard, changing as you type — instead of a box separate from
the game.

Both halves of that were broken for different reasons, and both had to be fixed
to get one working answer:

- The world-name field *looked* like an editable field but was not one: it was
  wired to a console API with no implementation here.
- The one prompt that did work was a native window, because at the main menu
  there was nothing else available at the time.

## Design notes worth keeping

**No call site changed.** `RequestKeyboard` returning `EKeyboard_Pending` and the
answer arriving later through `GetText` is the contract every caller was already
written against, because the console keyboard was asynchronous too. Every
`KeyboardComplete*Callback` in the game already handled "cancelled — leave the
field alone", which is what Escape now does.

**The target field is the focused control.** `RequestKeyboard` does not say what
it is editing, and does not need to: it is only ever called in response to the
player activating that field.

**The request is deferred one tick.** `RequestKeyboard` is called from inside an
Iggy callback; altering the scene stack from there is not safe.

**The suppression flag survives the tick it is cleared on.** Otherwise the Return
that commits a field would also be read as a button press by the same input pass.

## Files

New:
- `Minecraft.Client/Common/UI/UITextEdit.h`, `UITextEdit.cpp` — `UITextEditor`
- `docs/systems/windows-text-entry.md` — the full description

Changed:
- `Common/UI/UIPointer.h` — defines `_UI_INLINE_TEXT_ENTRY`
- `Windows64/Win64KeyboardMouse.h`, `.cpp` — `RequestKeyboard` / `GetText`
  shadows; `SetTextInputActive`, `IsTextInputActive`, `EditKeyRepeated`;
  keyboard suppression in the five action queries and in hotbar selection
- `Common/UI/UIScene.h`, `.cpp` — `findFocusedTextInput`; session teardown
- `Common/UI/UIController.h`, `.cpp` — ticks the session before the key pass;
  `GetActiveScene`, `IsSceneLive`
- `Common/UI/UIScene_Keyboard.cpp` — takes its title/text/limit from the request,
  reports its result
- `Common/UI/UIScene_MainMenu.cpp` — "Join Server" rewritten as chained requests
- `Minecraft.Client.vcxproj` — adds `UITextEdit.cpp`, drops `Win64TextPrompt.cpp`

Deleted:
- `Windows64/Win64TextPrompt.h`, `.cpp` — its only caller is gone. `git` still
  has it.

## Verified

`MinecraftPC.sln` at `Release|x64` builds and links clean under VS2022 (v143).
No new compiler warnings. The console targets are unaffected: the whole feature
is behind `_UI_INLINE_TEXT_ENTRY`, which is Windows x64 only, and `UITextEdit.cpp`
preprocesses to nothing elsewhere. They still cannot be built here (no SDKs).

## Not verified

**Nothing about how any of it behaves.** No agent can run the game. Specifically:

- Whether the caret and the live label updates look right in the Iggy fields, and
  whether inserting an underscore into the label is legible at the field's font
  size and width. If it is ugly, `UITextEditor::Refresh` is the one place to
  change it.
- Whether `UIScene_Keyboard`'s ActionScript honours `SetLabel` / `GetLabel` /
  `SetCharLimit` on its text field. That is inferred from the movie's exported
  method names and 4J's own unfinished code, never observed. It is the riskiest
  part of this change. The inline path — typing into the world-name field —
  depends on far less and is the one to try first.
- Whether the fallback path is even reached in practice, which depends on
  `getControlFocus()` reporting the text field's id when the player activates it.
  If inline editing never happens and the on-screen keyboard always appears,
  `UIScene::findFocusedTextInput` is where to look.

## Watch out for

- A field left open when a scene is torn down would hold a pointer into a
  destroyed Iggy movie. Three separate guards exist (`SceneClosing` from the
  scene destructor, an `IsSceneLive` check every tick, and clearing the session
  before the callback runs) — if a crash appears around navigating away from a
  half-typed field, that is the area.
- `_Password` mode is not masked. No caller uses it today; if one appears, that
  is a real gap.
- Tab does not move between fields. Adding that needs a per-scene field order,
  which no scene exposes.
