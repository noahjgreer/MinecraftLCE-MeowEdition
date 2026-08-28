#pragma once
// CPM - Custom Player Models
//
// Port of com.tom.cpm.shared.model.Cube / RenderedCube and the part-stream
// parser in com.tom.cpm.shared.definition.ModelDefinitionLoader.
//
// A .cpmmodel's data block is a flat stream of "object blocks": a one-byte part
// type ordinal followed by a varint-length payload. Unknown part types are
// skipped whole, which is how the format stays forward compatible - we only
// implement the part types this port actually renders and everything newer is
// ignored rather than treated as corruption.

#include <vector>
#include <string>
#include "CPMIo.h"

// com.tom.cpm.shared.parts.ModelPartType ordinals. Kept complete (including the
// deprecated entries) because the ordinal IS the wire value.
enum CPMPartType
{
	CPM_PART_END = 0,
	CPM_PART_PLAYER,
	CPM_PART_TEMPLATE,
	CPM_PART_DEFINITION,
	CPM_PART_DEFINITION_LINK,
	CPM_PART_SKIN,
	CPM_PART_SKIN_LINK,
	CPM_PART_PLAYER_PARTPOS,
	CPM_PART_RENDER_EFFECT,
	CPM_PART_UUID_LOCK,
	CPM_PART_ANIMATION_DATA,
	CPM_PART_SKIN_TYPE,
	CPM_PART_MODEL_ROOT,
	CPM_PART_LIST_ICON,
	CPM_PART_DUP_ROOT,
	CPM_PART_CLONEABLE,
	CPM_PART_SCALE,
	CPM_PART_TEXTURE,
	CPM_PART_ANIMATED_TEX,
	CPM_PART_TAGS,
	CPM_PART_PACKAGE_LINK,
	CPM_PART_CUBES,
	CPM_PART_ROOT_INFO,
	CPM_PART_ANIMATION_NEW,
	CPM_PART_COUNT
};

// com.tom.cpm.shared.model.PlayerModelParts. These ordinals are also the
// parentId values a root-parented cube refers to.
enum CPMPlayerPart
{
	CPM_PP_HEAD = 0,
	CPM_PP_BODY,
	CPM_PP_LEFT_ARM,
	CPM_PP_RIGHT_ARM,
	CPM_PP_LEFT_LEG,
	CPM_PP_RIGHT_LEG,
	CPM_PP_CUSTOM_PART,
	CPM_PP_COUNT
};

// com.tom.cpl.util.Direction ordinals. Per-face UV is keyed by these, and the
// order is also the bit order of the mask in PerFaceUV.readFaces.
enum CPMFace
{
	CPM_FACE_UP = 0,
	CPM_FACE_DOWN,
	CPM_FACE_NORTH,
	CPM_FACE_SOUTH,
	CPM_FACE_EAST,
	CPM_FACE_WEST,
	CPM_FACE_COUNT
};

// Per-face UV overrides. A face that is not present is not drawn at all, which
// is how the editor deletes individual faces.
class CPMFaceUV
{
public:
	bool any;
	bool used[CPM_FACE_COUNT];
	int sx[CPM_FACE_COUNT], sy[CPM_FACE_COUNT];
	int ex[CPM_FACE_COUNT], ey[CPM_FACE_COUNT];
	int rot[CPM_FACE_COUNT];      // 0..3, quarter turns

	CPMFaceUV();

	// PerFaceUV.Face.getVertexU / getVertexV
	float u(int face, int vertex) const;
	float v(int face, int vertex) const;
};

// Cube.java flags
#define CPM_CUBE_HAS_MESH    (1 << 0)
#define CPM_CUBE_HAS_TEXTURE (1 << 1)
#define CPM_CUBE_HIDDEN      (1 << 2)
#define CPM_CUBE_MESH_SCALED (1 << 3)
#define CPM_CUBE_UV_SCALED   (1 << 4)
#define CPM_CUBE_MC_SCALED   (1 << 5)
#define CPM_CUBE_SCALED      (1 << 6)

// com.tom.cpm.shared.effects.RenderEffects ordinals. Only the ones this port
// applies are named; the rest are skipped.
#define CPM_FX_GLOW        0
#define CPM_FX_SCALE       1
#define CPM_FX_HIDE        2
#define CPM_FX_COLOR       3
#define CPM_FX_SINGLE_TEX  4
#define CPM_FX_PER_FACE_UV 5
#define CPM_FX_EXTRUDE     10
#define CPM_FX_UV_OVERFLOW 6

// ModelPartRootInfo flags
#define CPM_ROOT_HIDDEN          (1 << 0)
#define CPM_ROOT_CREATE          (1 << 1)
#define CPM_ROOT_MODEL           (1 << 2)
#define CPM_ROOT_TRANSFORM       (1 << 3)
#define CPM_ROOT_DISABLE_VANILLA (1 << 4)

// Static cube description as stored in the file.
class CPMCube
{
public:
	CPMVec3 offset;
	CPMVec3 rotation;      // radians
	CPMVec3 pos;
	CPMVec3 size;
	CPMVec3 scale;
	CPMVec3 meshScale;
	int parentId;
	int id;
	int rgb;
	int u, v, texSize;
	float mcScale;
	bool hidden;
	bool hasMesh;
	// Set by the COLOR effect: tints a cube that still uses its texture, as
	// opposed to a texSize==0 cube which is flat-coloured to begin with.
	bool recolor;
	// "Single texture": every face samples the same square UV patch rather
	// than the unwrapped box layout.
	bool singleTex;
	// Extruded: the flat quad is turned into a slab with a per-texel silhouette.
	bool extrude;
	// Per-face UV overrides, when the model sets them.
	CPMFaceUV faceUV;

	CPMCube();

	// Cube.loadDefinitionCubeV2 - the modern encoding.
	static bool load(CPMIn &in, CPMCube &out);

	// Cube.loadDefinitionCube - the V1 encoding used inside a DEFINITION block.
	// This is what the CPM editor writes by default, so most real models are
	// V1, not V2.
	static bool loadV1(CPMIn &in, CPMCube &out);
};

// Runtime tree node. Holds both the static description and the per-frame
// animated transform, which starts as a copy of the static one each frame.
class CPMRenderedCube
{
public:
	CPMCube cube;
	CPMRenderedCube *parent;
	std::vector<CPMRenderedCube *> children;

	// Animated state, reset from `cube` every frame before animations apply.
	CPMVec3 pos;
	CPMVec3 offset;
	CPMVec3 rotation;
	CPMVec3 renderScale;
	int color;
	bool display;

	// True for the six synthetic root nodes that stand in for vanilla bones.
	bool isRoot;
	int rootPart;          // CPMPlayerPart when isRoot
	bool rootHidden;       // vanilla part suppressed by ROOT_INFO
	bool disableVanilla;

	CPMRenderedCube();
	~CPMRenderedCube();

	void reset();          // restore animated state from the static cube
	void addChild(CPMRenderedCube *c);
};

// A fully parsed, resolved model.
class CPMModelDefinition
{
public:
	// Owns every cube node, roots included.
	std::vector<CPMRenderedCube *> allCubes;

	// The six vanilla-bone roots plus CUSTOM_PART, indexed by CPMPlayerPart.
	CPMRenderedCube *roots[CPM_PP_COUNT];

	// Embedded skin texture (TextureSheetType.SKIN), if the model carries one.
	std::vector<unsigned char> skinPng;
	int skinUvWidth;
	int skinUvHeight;

	// Resolved GL texture id, filled in lazily by CPMManager. -1 = not yet done.
	int textureId;

	std::string name;
	std::string error;

	// V1 render effects arrive as separate parts that refer to cubes by id, so
	// they are collected while parsing and applied once the cubes exist.
	struct PendingEffect
	{
		int type;
		int id;
		float mcScale;
		CPMVec3 scale;
		int rgb;
		int u, v;
		CPMFaceUV faceUV;
	};
	std::vector<PendingEffect> pendingEffects;

	CPMModelDefinition();
	~CPMModelDefinition();

	bool hasError() const { return !error.empty(); }

	// Parse a raw data block (the part stream, starting at the HEADER byte).
	// Returns false and fills `error` on malformed input.
	bool load(const unsigned char *data, int len);

	void resetAnimationPos();

	// Used by both front ends: the binary loader and the .cpmproject loader
	// build the same tree, they just get the cubes from different places.
	void prepareRoots();
	bool buildTree(std::vector<CPMCube> &cubes);

private:
	// Walks a stream of object blocks. A V1 DEFINITION block contains a nested
	// stream of its own, so this recurses one level.
	bool parsePartStream(CPMIn &in, std::vector<CPMCube> &cubes, bool bNested);
	bool parseDefinitionV1(CPMIn &in, std::vector<CPMCube> &cubes);
	bool parseRenderEffect(CPMIn &in);
	void applyPendingEffects(std::vector<CPMCube> &cubes);
	void clear();
};

// The .cpmmodel container: header byte, name, description, data block, then
// optional overflow/link and an icon image. Only the data block matters to us.
// Returns false on a magic-number, checksum, or truncation failure.
// Identifies a file that is not an exported model (editor project, skin PNG,
// JSON) so the caller can say what to do about it.
std::string CPMIdentifyFile(const unsigned char *data, int len);

// `errOut` gets a specific reason on failure - a generic "invalid file" is
// useless when the only way to test is to try it in the game.
bool CPMLoadModelFile(const unsigned char *file, int len,
                      std::string &nameOut, std::string &descOut,
                      std::vector<unsigned char> &dataBlockOut,
                      std::string &errOut);

#define CPM_FILE_HEADER 0x53
