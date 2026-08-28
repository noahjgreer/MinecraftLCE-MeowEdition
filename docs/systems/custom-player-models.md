# Custom Player Models (CPM)

A partial port of [CustomPlayerModels](https://github.com/tom5454/CustomPlayerModels)
(Tom5454, MIT) from Java to this tree. Players wear `.cpmmodel` files authored in the
Java/desktop CPM editor or Blockbench, and models are shared with everyone on the
server so each player sees the others' models.

Reference source is vendored at `References/CustomPlayerModels/`. When something here
looks arbitrary, it is almost always because the Java original does it that way and
the wire format forces our hand.

## What is and is not ported

| Area | Status |
|---|---|
| `.cpmmodel` container + part stream | Ported |
| Cube tree, parenting, per-cube UV / colour / scale | Ported |
| Embedded skin texture | Ported |
| `ROOT_INFO` (hide or offset a vanilla bone) | Ported for the six player bones |
| Animations: pose triggers, all 7 interpolators, 13 channels/cube | Ported — **V2 `ANIMATION_NEW` only**. V1 models carry the older `ANIMATION_DATA`, which is a different encoding and is not ported, so a default-exported model renders but does not animate |
| Model sync between players | Ported (see "Networking") |
| **In-game model editor** | **Not ported.** ~25k lines of Java on a bespoke GUI toolkit; LCE's UI is Iggy/Flash. This is a rewrite, not a port |
| Gestures, named/parameter/staged triggers, layer control | Not ported — need input UI or server-synced parameters. Parsed and ignored |
| Cape / elytra / armour roots, render effects, templates, tags | Not ported. Parsed and ignored |
| Texture stitching, animated textures | Not ported |
| Per-face UV, `singleTex`, `extrude` cubes | Ported |
| HTTP model loaders (gist, pastebin, CDN) | Not ported. A model carrying a `Link` is rejected |
| Skin-embedded model data | Not ported. LCE's console skin pipeline is nothing like Java's |
| **`.cpmproject` (editor working format)** | **Ported.** Geometry, texture and hidden parts. Its animation JSONs are not read |

Unported part types are *skipped*, not treated as errors — that is the format's own
forward-compatibility rule (`readObjectBlock` returns null for an unknown enum
ordinal and the block is consumed anyway). A model using unported features loads and
renders whatever it can.

## Files

All under `Minecraft.Client/Common/CPM/`, platform-independent except where noted.

| File | Responsibility |
|---|---|
| [CPMIO.h](../../Minecraft.Client/Common/CPM/CPMIO.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMIO.cpp) | Byte-exact port of `IOHelper` + `ChecksumInputStream`: varints, signed varints, fixed-point vectors, angles, length-prefixed blocks |
| [CPMModel.h](../../Minecraft.Client/Common/CPM/CPMModel.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMModel.cpp) | `Cube` / `RenderedCube`, the part-stream parser, the container reader |
| [CPMAnim.h](../../Minecraft.Client/Common/CPM/CPMAnim.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMAnim.cpp) | `ANIMATION_NEW` parsing, interpolators, per-cube drivers |
| [CPMRender.h](../../Minecraft.Client/Common/CPM/CPMRender.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMRender.cpp) | `BoxRender` port; emits quads to the `Tesselator` |
| [CPMManager.h](../../Minecraft.Client/Common/CPM/CPMManager.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMManager.cpp) | Model registry keyed by player name, texture upload, pose detection, `/cpm` command |
| [CPMNet.h](../../Minecraft.Client/Common/CPM/CPMNet.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMNet.cpp) | Chunked model sync over `CustomPayloadPacket` |
| [CPMFiles.h](../../Minecraft.Client/Common/CPM/CPMFiles.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMFiles.cpp) | **The only platform-dependent file.** Lists/reads the model folder; consoles get a stub |
| [CPMZip.h](../../Minecraft.Client/Common/CPM/CPMZip.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMZip.cpp) | Minimal in-memory ZIP reader for `.cpmproject`, on the client's existing zlib |
| [CPMJson.h](../../Minecraft.Client/Common/CPM/CPMJson.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMJson.cpp) | Small read-only JSON parser |
| [CPMProject.h](../../Minecraft.Client/Common/CPM/CPMProject.h) / [.cpp](../../Minecraft.Client/Common/CPM/CPMProject.cpp) | `config.json` → the same cube tree the binary loader builds |

## The file format

A `.cpmmodel` is:

```
0x53                        magic (ModelDefinitionLoader.HEADER)
  UTF  name
  UTF  description
  varint+bytes  data block      <- the actual model
  varint+bytes  overflow        <- empty unless the model uses a Link
  varint+bytes  icon PNG
  int16 checksum                <- sum of every byte since the magic
```

The data block is itself a header byte, a stream of *object blocks*, and its own
checksum. Each object block is `[byte partType][varint length][payload]`.

### Two encodings, and the one that actually matters

There are **two** model encodings, and the one the CPM editor writes by default is
the *deprecated* one. `Exporter.prepareExport` picks `ExporterImpl.prepareExport`
(V2) only when the `EDITOR_EXPERIMENTAL_EXPORT` config flag is set; otherwise it
calls `prepareDefinition`, which builds a V1 `ModelPartDefinition`.

So a normal exported model looks like this:

```
SKIN_TYPE(11)
DEFINITION(3)              <- everything is nested inside here
    varint cube count
    V1 cube records        (Cube.loadDefinitionCube - fixed layout, no flags)
    PLAYER(1)              vanilla-bone keep bitmask
    SKIN(5)                the texture, as a bare TextureProvider
    PLAYER_PARTPOS(7)      per-root transform
    RENDER_EFFECT(8) * n   hide / scale / colour / UV overflow
    END(0)
END(0)
```

rather than the flat V2 stream of `CUBES` / `TEXTURE` / `ROOT_INFO`. Both are
supported; `parsePartStream` recurses one level into a `DEFINITION` block.

Differences that matter when reading V1:

- Cubes use `Cube.loadDefinitionCube`: `vec3ub` size, `vec6b` pos and offset, angle,
  varint parent, then a texture byte — and **`u`/`v` are single bytes**. A cube whose
  UV exceeds 255 gets a separate `UV_OVERFLOW` render effect carrying the real values.
- V1 cubes have no mesh-scale, mc-scale or hidden flag of their own. Those arrive as
  `SCALE` and `HIDE` render effects referring to the cube by id, so effects are
  collected during parsing and applied once the cubes exist.
- The `END` inside a `DEFINITION` block terminates only that block. Only the
  outermost `END` is followed by the two checksum bytes.

The important part types (ordinals are the wire values — see `CPMPartType`):

- `CUBES` (21) — a varint count then `loadDefinitionCubeV2` records. Cube ids are
  assigned by position, starting at **10**.
- `TEXTURE` (17) — sheet type, UV-space size, then a PNG block. Only sheet 0 (`SKIN`)
  is used here.
- `ROOT_INFO` (22) — flags, root ordinal, optional transform.
- `ANIMATION_NEW` (23) — a nested object-block stream of its own (`TagType`).
- `END` (0) — followed by the two checksum bytes.

### Parenting

`Cube.resolveCubesV2` encodes the tree in `parentId`:

- `parentId < 10` → the cube hangs off a **vanilla bone**, and the value is a
  `PlayerModelParts` ordinal (HEAD 0, BODY 1, LEFT_ARM 2, RIGHT_ARM 3, LEFT_LEG 4,
  RIGHT_LEG 5, CUSTOM_PART 6).
- `parentId >= 10` → the cube is a child of another cube with that id.

We create the seven roots up front as synthetic `CPMRenderedCube`s so both cases are
uniform.

### Encoding gotchas

These bit the port and are worth knowing before touching `CPMIO`:

- `DIV` is `Short.MAX_VALUE / Vec3f.MAX_POS` in **Java integer division** = `32767/256`
  = **127**, not 127.996. Fixed-point values are `raw / 127`.
- `readAngle` returns **radians** but `writeAngle` consumes **degrees**. That
  asymmetry is in the original.
- `readAngle` reads a *signed* short. Angles over 180° were written as values above
  32767 and come back negative. That wrap is part of the format.
- The frame-count multiply lives in the *driver*, not the interpolator:
  `ConstantTimeFloatDriver` calls `interpolator(step * frameCount)`. Our
  `CPMTrack::apply` takes normalised `step` and does the multiply internally.
- Boolean keyframes are LSB-first within each byte.

## The project format (`.cpmproject`)

A second, completely separate front end. `.cpmproject` is the editor's working
format — a ZIP holding `config.json`, `skin.png` and optionally `animations/*.json`.
`CPMLoadProject` reads it and builds the **same** `CPMModelDefinition` cube tree the
binary loader produces; none of the binary format is involved.

This exists so a model can be used without installing Minecraft Java and the CPM mod
purely to export it.

`CPMManager::loadFromBytes` dispatches on the magic bytes: `PK` goes to the
project loader, `0x53` to the binary one. Both `/cpm list` and `/cpm set` accept
either extension; when a project and an export share a base name the export wins,
because it is the one that can carry animations.

Field mapping (from `ElementsLoaderV1`), with the traps:

| JSON | Cube field | Note |
|---|---|---|
| `scale` | `meshScale` | **Not** the render scale, despite the name |
| `rscale` | `scale` | The render scale |
| `rotation` | `rotation` | **Degrees** in the project, radians internally |
| `textureSize` + `mirror` + `texture` | `texSize` | `texture ? (mirror ? -textureSize : textureSize) : 0` |
| `color` | `rgb` | A **hex string**, not a number |
| `hidden` | `hidden` | `show` on a child is the editor's own visibility toggle, not the model's |
| `show` (root) | `rootHidden` | Inverted: `show:false` hides the vanilla bone |
| `skinSize` | `skinUvWidth/Height` | Often not 64x64 — both example projects use 128 wide |

Two things the ZIP reader has to get right: Java's `ZipOutputStream` sets the
data-descriptor flag, so the **local headers carry a zero compressed size** and the
central directory must be used instead; and entries are raw deflate, so zlib is
initialised with negative window bits. Entry CRCs are verified, because deflate
tolerates a lot of corruption without failing outright and these archives can arrive
from another player.

## Rendering

CPM's Java renderer swaps each vanilla `ModelPart` field for a redirect object that
walks the cube tree instead. LCE's `ModelPart::render` is not virtual and the parts
are shared between players, so we hook differently:

[HumanoidModel::render](../../Minecraft.Client/HumanoidModel.cpp) asks
`CPMManager::renderPart` for each of the six bones. That draws the subtree under the
bone's own transform and returns whether the vanilla geometry should be suppressed —
which is passed straight into `ModelPart::render`'s existing `bHideParentBodyPart`
argument, so a hidden bone still transforms its children.

Only `PlayerRenderer`'s main humanoid has `cpmEnabled` set; the two armour layers are
also `HumanoidModel`s and must not draw the model again.

**The hat layer is suppressed outright.** CPM registers the vanilla hat with a *null*
part (`RedirectHolderPlayer`: `model.hat` -> `null`, `setCopyFrom(head)`), and
`ModelRenderManager.render` returns without drawing anything for a null part whenever
a model is loaded:

```java
if(part == null) {
    if(dh.copyFrom != null) holder.copyModel((P) dh.copyFrom, tp);
    return;                     // draws nothing
}
```

So a custom model hides the hat *unconditionally*, not only when it hides the head.
LCE's `hair` is that layer - the same box inflated by 0.5 at UV (32,0) on the head
pivot - so leaving it on puts a second, slightly larger head over the model wearing
the model's own texture.

`renderEars` and `renderCloak` are separate conditional paths and are left vanilla;
the cape is a `RootModelType`, which this port does not redirect. `HumanoidModel`'s
`young` branch has no CPM path, which is fine because players are never `young`.

Per-cube transform order matches `RedirectRenderer.translateRotate` exactly:
translate, rotate **Z then Y then X**, scale. Same order LCE's `ModelPart::render`
already uses, which is why this lines up at all.

### Root transforms combine, they do not compose

`RootModelElement.setPosAndRot` / `getPos` / `getRot` **add** the root's own offset
and its animated delta to the vanilla bone's pivot and rotation, and the result is
applied as a *single* translate and a *single* rotate:

    translate(bonePos + rootOffset + animPos)
    rotate   (boneRot + rootRot    + animRot)      // Z, then Y, then X

Applying them as two transforms in sequence — bone, then root — rotates about the
bone's pivot and only then shifts, which puts the pivot in the wrong place for any
model that moves a root. That is a joint that swings about the wrong point.

For the same reason a root's `pos`/`rotation` fields hold the **animated delta only**
and reset to zero each frame (`RootModelElement::reset`); its static offset lives in
`cube.pos`/`cube.rotation`. `CPMRenderedCube::reset` special-cases roots for this.

### Three rendering gotchas

- **The `Tesselator` ignores `glColor`.** When `hasColor` is false it overwrites every
  vertex colour with `0xffffffff` (see `Tesselator::end`). A tinted cube must go
  through `Tesselator::color`, not `glColor4f`.
- **Coloured cubes use different geometry rules.** `BoxRender.createColored` forces a
  texel scale of 1, never mirrors, and samples UV (0,0). It *looks* like it ignores
  `meshScale` too, but `createBox` hands it a size already multiplied by `meshScale` —
  missing that shrinks every coloured cube.
- **A cube can have fewer than six faces.** With per-face UV, only the directions
  present in the map are drawn — deleting a face in the editor removes it from the
  map. Falling back to the box layout draws all six with the wrong UVs, which looks
  like a texturing bug rather than a missing feature.
- **`singleTex` is common, not exotic.** A "single texture" cube maps one square UV
  patch onto all six faces instead of the unwrapped box layout. 82 of the 119 cubes in
  the vendored `animation_status_test` project use it, so treating it as the ordinary
  box layout misplaces the UVs on most of a model.
  Because we do not port the `TextureStitcher`, coloured cubes sample the skin's
  top-left texel rather than a stitched white patch — they are tinted, but by
  whatever is at (0,0). This is a known visual deviation.

### The first-person hand

`PlayerRenderer::renderHand` draws `humanoidModel->arm0` — the **right** arm, per the
pivots set in `HumanoidModel::_init` — at 1/16 scale. It now runs the same
`beginEntity` / `bindActiveTexture` / `renderPart` sequence the third-person path
uses, against `CPM_PP_RIGHT_ARM`, so the hand you see is the model's arm.

`setupAnim` has already placed `arm0` for the hand pose by that point, and
`renderPart` positions the custom cubes from that same transform, so the two stay
together. If the model hides the vanilla right arm, only the custom cubes draw.

Rebinding the texture inside `renderHand` is safe: `ItemInHandRenderer` binds the
player skin and calls `clearLastBoundId()` immediately before calling it, and the
held item rebinds its own texture afterwards.

### Cube UV variants

Four different box builders, picked in `ModelRenderManager.createBox`:

| Condition | Builder | Behaviour |
|---|---|---|
| `texSize == 0` | `createColored` | Flat colour, texel scale 1, samples UV (0,0), no mirror |
| `singleTex` | `createTexturedSingle` | One square patch of side `max(dx,dy,dz)` on every face |
| `faceUVs != null` | `createTextured(PerFaceUV)` | Per-face rectangle and quarter-turn; **absent faces are not drawn**; no mirror |
| `extrude` | `createTexturedExtruded` | Flat quad given thickness, with a per-texel comb along each edge |
| otherwise | `createTextured` | The usual unwrapped box layout |

Per-face UV corner order comes from `PerFaceUV.Face.getVertexU/V`, which rotates the
vertex index by `(index + rot + 3) % 4` and then picks `sx`/`ex` and `sy`/`ey`.

The extruded mesh is built in a unit space (x,y in [0,1], z in [-1,0]) and mapped into
the box. The reference does that with a matrix; this port applies the same mapping per
vertex instead, since the caller already scales positions. Normals only need the sign
of the scale because they are axis-aligned. The comb is capped at 256 texels per edge
so a careless model cannot ask for millions of quads.

## Animation

Only **pose-triggered** animations play. `CPMManager::detectPose` maps LCE player
state onto a `VanillaPose` ordinal; animations whose trigger names that pose (or
`GLOBAL`) run, sorted by priority ascending so the highest priority writes last.

`CUBES_TO_CHANNELS` creates channels implicitly, 13 per cube in a fixed order:
POS xyz, ROT xyz, COLOR rgb, SCALE xyz, then visibility. Keyframe blocks reference
channels by that flat index. If `intChCount != 12` only the visibility channel exists.

A `Float3Driver` fires only when at least one of its three components has keyframes;
components without them contribute the cube's current value (or 0 when additive).

Because the cube tree is shared, `beginEntity` re-runs `resetAnimationPos()` and
re-applies the animation for **every entity drawn**, not once per tick.

## Networking

Model sync rides on `CustomPayloadPacket` (id 250) over channel `CPM|Data` — the same
mechanism CPM uses on Java, where it is a plugin channel.

```
client  --CPM_OP_MODEL (chunked)-->  server
                                     validates by parsing, assigns to sender
        <--CPM_OP_MODEL (chunked)--  broadcast to everyone
```

`CustomPayloadPacket` carries a **16-bit** length, so models are split into 8000-byte
chunks. Each chunk repeats the total size, its own index and the chunk count, so a
receiver never has to trust a previous packet.

The **server ignores the name in the packet** and uses the name of the connection the
packet arrived on, so a client cannot publish a model as someone else. The server also
parses a model before relaying it, so a malformed model does not reach everyone.

A joining player is brought up to date by `CPMNet::buildJoinSync`, called from the
`PlayerConnection` constructor; a joining client announces its own model from
`ClientConnection::handleLogin`.

### Untrusted input

Model bytes arrive from other players. Everything in `CPMIO` is bounds-checked and
short reads set a failure flag rather than throwing. Two specific hazards:

- `BufferedImage(BYTE*, DWORD)` calls `app.FatalLoadError()` — i.e. kills the process —
  if D3DX cannot decode the data. `CPMManager` validates the PNG signature, the IHDR
  chunk and the dimensions *before* constructing one.
- Block lengths are capped (16 MB per block, 8192 cubes, 1 MB per model, 256 chunks).

## Usage

Put **exported** `.cpmmodel` files in a `cpm` folder in the game's working directory.

`.cpmproject` is *not* a model — it is the editor's working format and is a ZIP.
Open it in the CPM editor and use **Export > Model**. `CPMIdentifyFile` detects a
project file, a skin PNG and JSON and says so by name, and `/cpm list` flags them in
the listing.

Then:

```
/cpm list           show available models
/cpm set <name>     wear one (and publish it to the server)
/cpm clear          go back to your normal skin
```

The command is intercepted client-side in `MultiplayerLocalPlayer::chat` and never
reaches the server as chat.

## Known limitations

- No editor. Models must be authored elsewhere.
- Coloured cubes sample skin (0,0) — see above.
- The GL texture id from `Textures::getTexture` is never released when a model is
  replaced; LCE exposes no per-texture free. Switching models repeatedly leaks texture
  ids.
- Armour renders vanilla over a custom model; CPM's armour roots are not ported.
- Pose detection is approximate: LCE has no direct equivalent for several
  `VanillaPose` values, and swimming/falling/jumping are inferred from
  `isInWater`/`onGround`/`fallDistance`.
- **V1 models do not animate.** Geometry, texture and hidden parts all work, but the
  legacy `ANIMATION_DATA` encoding is not ported. Exporting with the editor's
  experimental (V2) export enables animation.
- A model whose data spilled into an external `Link` cannot be loaded — the HTTP
  loaders are not ported. The editor produces one when the model exceeds its export
  buffer, which is only 2KB when "skin compatible" is ticked and 30KB otherwise.
