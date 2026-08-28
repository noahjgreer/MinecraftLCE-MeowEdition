# Text entry (Windows x64)

How typing into a text field works on the Windows x64 build, and why it had to be
built rather than enabled.

## The starting position

Every text field in LCE is entered the same way:

```cpp
InputManager.RequestKeyboard(title, currentText, pad, maxChars, callback, this, mode);
```

...and later, in the callback:

```cpp
uint16_t pchText[128];
InputManager.GetText(pchText);
```

There are about fifteen such call sites: the world name and the world seed
(`UIScene_CreateWorldMenu`, `UIScene_LaunchMoreOptionsMenu`), sign text
(`UIScene_SignEntryMenu`), the anvil (`UIScene_AnvilMenu`), renaming a save
(`UIScene_LoadOrJoinMenu`), and the debug scenes.

`RequestKeyboard` is the **console on-screen keyboard**. It lives in the prebuilt
`4J_Input.lib`, which has no PC implementation behind it, so on Windows every one
of those fields did nothing at all — the field was drawn, it could be focused, it
could be activated, and then nothing happened.

The one text prompt that did work — "Join Server" on the main menu — worked by
opening a **native Win32 window on top of the game** (`Win64TextPrompt`), because
at the time there was nothing else available: no working `RequestKeyboard`, and
no `Screen` being rendered at the main menu to draw into (see
`dedicated-server-and-direct-connect.md`). It worked, but a modal OS window is
not what typing into a game should feel like.

## What happens now

A player with a keyboard types straight into the field that is already on screen.
The field updates character by character, with a blinking caret, in the Iggy menu
itself.

```
WM_CHAR  (Windows64_Minecraft.cpp)
      │      layout, shift state and dead keys already applied by Windows;
      │      backspace/tab/return/escape arrive as control characters
      ▼
Win64Input::OnChar  ──►  typed-character ring
      │
      ▼
UIController::handleInput
      │      g_UITextEdit.Tick() runs BEFORE the key pass
      ▼
UITextEditor::DrainInput
      │      buffer + caret; Return commits, Escape cancels
      ▼
UIControl_TextInput::setLabel   ──►  the field visibly changes as it is typed
      │
      ▼  on commit
the caller's own KeyboardComplete* callback, which reads InputManager.GetText()
```

## Why no call site changed

The same facade-subclassing trick the rest of the Windows input layer uses.
`C_Win64Input` (see `windows-keyboard-mouse-input.md`) now also shadows
`RequestKeyboard` and `GetText`:

- `RequestKeyboard` records the request and returns `EKeyboard_Pending` — which
  is the contract every caller was already written against, because the console
  keyboard was asynchronous too.
- `GetText` returns whatever the edit session committed.

So all fifteen call sites, and every `KeyboardComplete*Callback` in the game,
work unmodified. They were already written to handle "the keyboard came back with
`bRes == false`, leave the field alone", which is exactly what Escape does.

## Which field does a request mean?

`RequestKeyboard` never says which control it is editing — it takes a title and
the current text, not a control. It does not have to: **it is only ever called in
response to the player activating that field**, so the scene's focused control
*is* the target. `UIScene::findFocusedTextInput` walks the scene's controls for
one of type `eTextInput` that currently has focus.

Two cases have no such control:

| Case | Why | What happens |
|---|---|---|
| Renaming a save | Focus is on the save list, not a text field | falls back |
| A pad player | Nothing to type with | falls back |
| "Join Server" | The main menu has no text field at all | falls back |

The fallback is **`UIScene_Keyboard`** — the game's own in-game on-screen
keyboard scene. 4J built it (the `Keyboard.swf` / `KeyboardSplit.swf` movies ship
in `Common/Media`) and then never connected it to anything: it had a hardcoded
"Enter Sign Text" title, a hardcoded 15-character limit, and a Done handler whose
entire body was a `ToDo` comment. It now takes its title, starting text and
character limit from the pending request and reports its result back, so it is a
real keyboard for the first time. A physical keyboard also types into it, so
either input device works there.

That is also what replaced the "Join Server" Win32 prompt: it is now two chained
`RequestKeyboard` calls (address, then name), which is the shape every other
keyboard caller in the game already has. `Win64TextPrompt` was deleted; `git`
still has it if a native prompt is ever wanted again.

## Deferring the request by one tick

`RequestKeyboard` is called from inside an **Iggy callback** — the scene is in the
middle of dispatching a button press from ActionScript. Pushing a scene from
there is not safe, so the request is only *recorded*; `UITextEditor::Tick`, on the
next UI tick, decides whether it is an inline edit or an on-screen keyboard.

## The keyboard stops being game input while a field is open

The reason this needs saying: `W`, `A`, `S`, `D`, `E`, `Q`, `Space`, `Enter` and
`Escape` are all bound to game and menu actions in `Win64KeyboardMouse.cpp`.
Typing a world name would otherwise walk the player, open the inventory, drop an
item and back out of the menu.

`Win64Input::SetTextInputActive(true)` makes every keyboard contribution to
`GetValue`, `ButtonPressed`, `ButtonDown`, `ButtonReleased`, the movement stick
and hotbar slot selection vanish for as long as a field is open. **The pad is not
affected** — a splitscreen guest can keep playing while player 1 types.

One subtlety: the flag stays effective for the **remainder of the tick it is
cleared on** (`s_textInputTick`). Otherwise the Return that commits a field would
be read, later in the very same input pass, as "press the focused button".

## Editing keys

| Key | Effect |
|---|---|
| any character | inserted at the caret, up to the field's character limit |
| Backspace | deletes before the caret |
| Delete | deletes at the caret |
| Left / Right | move the caret (auto-repeats when held) |
| Home / End | caret to start / end |
| Return | commit — fires the caller's callback with `true` |
| Escape | cancel — fires it with `false`; the field keeps its old text |
| Tab | swallowed |

Characters come from `WM_CHAR`, not from virtual key codes, because `WM_CHAR` is
where Windows has already applied the keyboard layout, the shift state and any
dead keys — and it delivers backspace, tab, return and escape as control
characters too. Caret movement and Delete are the exception: they produce no
character, so they are the one thing read from the key state.

Tab does not move between fields because no scene exposes a field order. Adding
that means teaching each multi-field scene (create world, sign entry) what its
order is.

`EKeyboardMode` is honoured for filtering: `_Numeric` / `_Phone` accept digits
only, `_IP_Address` accepts digits, `.` and `:`. Everything else accepts any
printable character. (`_Password` is *not* masked — no caller uses it.)

## The caret

The caret is an underscore inserted into the **label**, never into the buffer, so
it can never be committed as text. It blinks on a 20-tick duty cycle, and Flash
is only touched on the frames the display actually changes — a `SetLabel` every
tick is a string marshal every tick for nothing.

In `UIScene_Keyboard` no caret is drawn: the movie draws its own, and there the
*control* is authoritative rather than the buffer, because the on-screen keys
edit the text through ActionScript. The buffer is re-read from the control before
every keystroke so the two input devices interleave correctly.

## Lifetime

An edit session holds a scene pointer and a control pointer, both of which belong
to an Iggy movie that can be destroyed underneath it.

- `UIScene::~UIScene` calls `g_UITextEdit.SceneClosing(this)`.
- Every tick, the session checks `ui.IsSceneLive(m_pScene)` and abandons itself if
  the scene has been navigated away from or buried under a message box.
- The session is cleared *before* the completion callback runs, because the
  callback is free to open the next field — the sign entry screen walks its four
  lines exactly that way.

## Gotchas

- **None of this is verified at runtime.** It compiles and links; no agent can run
  the game. In particular, whether `UIScene_Keyboard`'s ActionScript actually
  honours `SetLabel` / `GetLabel` / `SetCharLimit` on its text field is *inferred*
  from the movie's exported method names and from 4J's own unfinished code, not
  observed. The inline path (typing into a world name) depends on far less and is
  the one to try first.
- **Fields do not open on their own.** The player still activates the field the
  way they always did — the change is what happens after that, not before.
- **A failed "Join Server" connect still shows a native `MessageBoxW`.** That is
  an error path, not text entry; it stayed because there is no localised string
  resource for it and the Iggy message box takes string ids.
- **`_UI_INLINE_TEXT_ENTRY` is Windows x64 only** and is defined in `UIPointer.h`
  alongside the other UI feature macros, so it is reachable from every UI header.
  `UITextEdit.cpp` is `#ifdef`-ed out entirely on the console targets, so their
  preprocessed output is unchanged.

## Files

| File | Role |
|---|---|
| `Common/UI/UITextEdit.h` / `.cpp` | `UITextEditor` — the edit session |
| `Common/UI/UIPointer.h` | defines `_UI_INLINE_TEXT_ENTRY` |
| `Windows64/Win64KeyboardMouse.h` / `.cpp` | `RequestKeyboard` / `GetText` shadows, `SetTextInputActive`, `EditKeyRepeated` |
| `Common/UI/UIScene.cpp` | `findFocusedTextInput`, session teardown in the destructor |
| `Common/UI/UIController.cpp` | ticks the session before the key pass; `GetActiveScene` / `IsSceneLive` |
| `Common/UI/UIScene_Keyboard.cpp` | the on-screen keyboard scene, finished |
| `Common/UI/UIScene_MainMenu.cpp` | "Join Server" as two chained requests |

## In-game chat

Chat is a separate path from everything above. It does not go through
`RequestKeyboard` or `UITextEditor` at all: it opens the game's own `ChatScreen`
(the Java-derived `Screen` stack — see `native-ui-migration.md`), which owns its own
message buffer and draws its own line.

    T  /  (Win64KeyboardMouse: pends a chat request)
        -> Minecraft::tick collects it
        -> setScreen(new ChatScreen(prefill))
        -> ChatScreen::init calls Win64Input::SetTextInputActive(true)
        -> Screen::updateEvents drains typed chars -> ChatScreen::keyPressed

What the two paths share is the `SetTextInputActive` flag and the WM_CHAR queue.
The flag is what makes `Screen::updateEvents` deliver key events at all: only a
screen that has claimed the keyboard for text receives them, because the base
`Screen::keyPressed` closes the screen on Escape and delivering that to every screen
would fight the pause menu.

See `changes/2026-08-28-in-game-chat-keyboard.md`.
