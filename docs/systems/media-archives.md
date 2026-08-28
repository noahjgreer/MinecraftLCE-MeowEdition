# Media archives (`Common/media/Media*.arc`)

## The thing to know first

**The game reads the archive, not the loose files next to it.** `Common/media/`
contains both `MediaWindows64.arc` and a few hundred loose `.swf`/`.png` files.
The loose files are build inputs. Editing them changes nothing at runtime, and
they can be stale or differ in name from what the archive holds.

`CMinecraftApp::loadMediaArchive()` (`Minecraft.Client/Common/Consoles_App.cpp`,
around line 4099) picks one per platform:

| Platform | Archive |
|---|---|
| Windows x64 | `Common\Media\MediaWindows64.arc` |
| PS3 | `MediaPS3.arc` |
| Orbis (PS4) | `MediaOrbis.arc` |
| Durango (Xbox One) | `MediaDurango.arc` |
| PS Vita | `MediaPSVita.arc` |

All five ship in this tree, so a Windows-only change still leaves the console
archives untouched.

## Names inside the archive differ from names on disk

This is the trap. The platform skin is on disk as `platformskinHD.swf` but is
stored in the archive as **`skinHDWin.swf`** (and `platformskin.swf` as
`skinWin.swf`). Grepping the tree for the on-disk name finds nothing in the
archive. Always list the archive rather than assuming.

## Format

Defined by `Minecraft.Client/ArchiveFile.cpp`. Big-endian throughout — it is a
Java `DataInputStream` layout carried over from the Java port:

```
int32   file count
per file:
    uint16  name length
    bytes   UTF-8 name        -- a leading '*' means the payload is compressed
    int32   offset from start of archive
    int32   size
```

Payloads follow the header. In `MediaWindows64.arc` (348 files, header ends at
10844) they are **contiguous, in index order, with no padding, and none are
compressed** — no `'*'` names at all. `compression.h` exists for the `'*'` case
but nothing in this archive exercises it.

Because payloads are contiguous and addressed by absolute offset, **changing any
file's size means rewriting every later offset.** There is no free space to grow
into and no slack to shrink into.

On `_WINDOWS64` (and Durango/Orbis) `ArchiveFile` reads the whole archive into
memory once and serves `getFile` out of that buffer; the console path seeks the
file per request.

## Tool

`tools/media_arc.py` does the three useful operations and handles the offset
rewrite:

```
python tools/media_arc.py list    Minecraft.Client/Common/media/MediaWindows64.arc
python tools/media_arc.py extract <archive> skinHDWin.swf out.swf
python tools/media_arc.py replace <archive> skinHDWin.swf new.swf
```

`replace` repacks the whole archive in place. It asserts that the rebuilt header
is the same length as the original — that holds as long as no name changes, and
is the cheapest check that the index was rebuilt correctly.

## None of this is in git

`Common/media/` is **untracked**, archives and loose media alike. `git checkout --`
will not undo an edit here. Back up anything you modify before you modify it;
`extract` the original entry first.

## Where the menu art actually lives

The menu `.swf` files carry layout, not pictures — see
`docs/systems/native-ui-migration.md`. Shared bitmaps live in the platform skin
and are pulled in by SWF `ImportAssets2`. For the main-menu logo the chain is:

```
UIComponent_Logo::getMoviePath()  ->  "ComponentLogo1080"
  ComponentLogo1080.swf   (447 bytes, no bitmaps of its own)
    -> ImportAssets2: symbol "MenuTitle" from "platformskinHD.swf"
       -> archive entry skinHDWin.swf  (HD)  /  skinWin.swf  (SD)
```

**Resolve the symbol name through `SymbolClass`; never guess a character id from
its dimensions.** A skin holds several sizes of the same art and only the
`SymbolClass` tag (code 76) says which is which:

| Skin | `MenuTitle` | `MenuTitleSmall` |
|---|---|---|
| `skinHDWin.swf` | id 180, 857x207 | id 100, 571x138 |
| `skinWin.swf` | id 107, 571x138 | — |

The loose `Common/Media/Graphics/MenuTitle.png` is 571x138, which makes the
*small* variant look like the match. At 1080p the game draws the 857x207 one.

`DefineBitsLossless2` format 5 is 32-bit **premultiplied** ARGB, zlib-deflated,
stride = width * 4. Straight-alpha art must be premultiplied before it goes in.

A worked example of editing one of these bitmaps — including the SWF tag rewrite
and the archive repack — is in
[`docs/changes/2026-08-27-title-logo-swap.md`](../changes/2026-08-27-title-logo-swap.md).
