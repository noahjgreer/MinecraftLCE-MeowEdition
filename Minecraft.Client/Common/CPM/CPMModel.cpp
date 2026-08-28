#include "stdafx.h"
#include "CPMModel.h"
#include <map>
#include <stdio.h>

//////////////////////////////////////////////////////////////////////////
// CPMFaceUV
//////////////////////////////////////////////////////////////////////////

CPMFaceUV::CPMFaceUV() : any(false)
{
	for (int i = 0; i < CPM_FACE_COUNT; i++)
	{
		used[i] = false;
		sx[i] = sy[i] = ex[i] = ey[i] = 0;
		rot[i] = 0;
	}
}

// PerFaceUV.Face.getVertexRotated: (index + rotation + 3) % 4
static int cpmRotVertex(int vertex, int rot)
{
	return (vertex + rot + 3) & 3;
}

float CPMFaceUV::u(int face, int vertex) const
{
	int i = cpmRotVertex(vertex, rot[face]);
	return (float)((i != 0 && i != 1) ? ex[face] : sx[face]);
}

float CPMFaceUV::v(int face, int vertex) const
{
	int i = cpmRotVertex(vertex, rot[face]);
	return (float)((i != 0 && i != 3) ? ey[face] : sy[face]);
}

//////////////////////////////////////////////////////////////////////////
// CPMCube
//////////////////////////////////////////////////////////////////////////

CPMCube::CPMCube() :
	scale(1, 1, 1), meshScale(1, 1, 1),
	parentId(0), id(0), rgb(0), u(0), v(0), texSize(0),
	mcScale(0), hidden(false), hasMesh(false), recolor(false), singleTex(false), extrude(false)
{
}

// Cube.loadDefinitionCubeV2
bool CPMCube::load(CPMIn &in, CPMCube &c)
{
	signed char flags = in.readByte();
	c.parentId = in.readVarInt();
	c.pos = in.readVarVec3();
	c.rotation = in.readAngle();
	c.hidden = (flags & CPM_CUBE_HIDDEN) != 0;
	c.hasMesh = (flags & CPM_CUBE_HAS_MESH) != 0;

	if (c.hasMesh)
	{
		c.size = in.readVarVec3();
		c.offset = in.readVarVec3();

		if ((flags & CPM_CUBE_HAS_TEXTURE) != 0)
		{
			c.texSize = (flags & CPM_CUBE_UV_SCALED) != 0 ? in.readByte() : 1;
			c.u = in.readVarInt();
			c.v = in.readVarInt();
		}
		else
		{
			int r = in.read();
			int g = in.read();
			int b = in.read();
			if (in.fail()) return false;
			c.rgb = (0xff << 24) | (r << 16) | (g << 8) | b;
			c.texSize = 0;
		}

		if ((flags & CPM_CUBE_MC_SCALED) != 0)
			c.mcScale = in.readFloat2();
	}
	else
	{
		c.size = CPMVec3();
		c.offset = CPMVec3();
	}

	if ((flags & CPM_CUBE_MESH_SCALED) != 0)
		c.meshScale = in.readVarVec3();
	else
		c.meshScale = CPMVec3(1, 1, 1);

	if ((flags & CPM_CUBE_SCALED) != 0)
		c.scale = in.readVarVec3();
	else
		c.scale = CPMVec3(1, 1, 1);

	return !in.fail();
}

// Cube.loadDefinitionCube - the V1 encoding. Fixed layout, no flags byte, and
// u/v are single bytes (values above 255 arrive later as an UV_OVERFLOW effect).
bool CPMCube::loadV1(CPMIn &in, CPMCube &c)
{
	c.size = in.readVec3ub();
	c.pos = in.readVec6b();
	c.offset = in.readVec6b();
	c.rotation = in.readAngle();
	c.meshScale = CPMVec3(1, 1, 1);
	c.scale = CPMVec3(1, 1, 1);
	c.parentId = in.readVarInt();

	int tex = in.readByte();
	if (tex == 0)
	{
		int r = in.read();
		int g = in.read();
		int b = in.read();
		if (in.fail()) return false;
		c.rgb = (0xff << 24) | (r << 16) | (g << 8) | b;
		c.texSize = 0;
	}
	else
	{
		c.texSize = tex;
		c.u = in.readUnsignedByte();
		c.v = in.readUnsignedByte();
	}

	// V1 cubes always carry a mesh; there is no "no mesh" case to encode.
	c.hasMesh = true;
	c.hidden = false;
	c.mcScale = 0;
	return !in.fail();
}

//////////////////////////////////////////////////////////////////////////
// CPMRenderedCube
//////////////////////////////////////////////////////////////////////////

CPMRenderedCube::CPMRenderedCube() :
	parent(NULL), renderScale(1, 1, 1), color(0xffffff), display(true),
	isRoot(false), rootPart(-1), rootHidden(false), disableVanilla(false)
{
}

CPMRenderedCube::~CPMRenderedCube()
{
	// Children are owned by CPMModelDefinition::allCubes, not by the parent.
}

void CPMRenderedCube::reset()
{
	if (isRoot)
	{
		// RootModelElement::reset - a root's pos/rotation are the *animated*
		// delta and start at zero. Its static offset lives in cube.pos/rotation
		// and is added to the vanilla bone's transform at render time, not
		// applied as a second transform after it.
		pos = CPMVec3();
		rotation = CPMVec3();
		renderScale = CPMVec3(1, 1, 1);
		display = !rootHidden;
		color = 0xffffff;
		return;
	}

	offset = cube.offset;
	rotation = cube.rotation;
	pos = cube.pos;
	renderScale = cube.scale;
	color = (cube.recolor || (cube.texSize == 0 && cube.hasMesh)) ? cube.rgb : 0xffffff;
	display = !cube.hidden;
}

void CPMRenderedCube::addChild(CPMRenderedCube *c)
{
	children.push_back(c);
}

//////////////////////////////////////////////////////////////////////////
// CPMModelDefinition
//////////////////////////////////////////////////////////////////////////

CPMModelDefinition::CPMModelDefinition() :
	skinUvWidth(64), skinUvHeight(64), textureId(-1)
{
	for (int i = 0; i < CPM_PP_COUNT; i++) roots[i] = NULL;
}

CPMModelDefinition::~CPMModelDefinition()
{
	clear();
}

void CPMModelDefinition::clear()
{
	for (size_t i = 0; i < allCubes.size(); i++) delete allCubes[i];
	allCubes.clear();
	for (int i = 0; i < CPM_PP_COUNT; i++) roots[i] = NULL;
}

bool CPMModelDefinition::load(const unsigned char *data, int len)
{
	clear();
	error.clear();

	CPMIn in(data, len);
	if (in.read() != CPM_FILE_HEADER)
	{
		error = "magic number mismatch";
		return false;
	}
	in.beginChecksum();

	prepareRoots();

	std::vector<CPMCube> cubes;
	if (!parsePartStream(in, cubes, false))
	{
		clear();
		return false;
	}

	applyPendingEffects(cubes);

	if (!buildTree(cubes))
	{
		clear();
		return false;
	}

	resetAnimationPos();
	return true;
}

bool CPMModelDefinition::parsePartStream(CPMIn &in, std::vector<CPMCube> &cubes, bool bNested)
{
	bool sawEnd = false;

	// Guard against a malformed stream that never yields END.
	for (int guard = 0; guard < 65536; guard++)
	{
		int typeId = in.readByte();
		if (in.fail()) { error = "truncated part stream"; return false; }

		std::vector<unsigned char> block;
		if (!in.readNextBlock(block)) { error = "bad part block"; return false; }

		if (typeId < 0 || typeId >= CPM_PART_COUNT)
		{
			// Unknown part type from a newer CPM version - skip it. This is the
			// forward-compatibility rule of the format, not an error.
			continue;
		}

		CPMIn b(block.empty() ? NULL : &block[0], (int)block.size());

		switch (typeId)
		{
		case CPM_PART_END:
			// Only the outermost stream is followed by the two checksum bytes;
			// the END inside a DEFINITION block just terminates that block.
			if (!bNested && !in.checkSum()) { error = "checksum mismatch"; return false; }
			sawEnd = true;
			break;

		case CPM_PART_DEFINITION:
			// The V1 container. This is what the CPM editor writes by default,
			// so most real models arrive this way: the cubes and every other
			// part are nested inside it rather than sitting at the top level.
			if (bNested) { error = "nested definition block"; return false; }
			if (!parseDefinitionV1(b, cubes)) return false;
			break;

		case CPM_PART_PLAYER:
		{
			// A bitmask of which vanilla bones to keep, by PlayerModelParts
			// ordinal. This is the V1 equivalent of ROOT_INFO's hidden flag.
			int keep = b.read();
			if (b.fail()) break;
			for (int i = 0; i < CPM_PP_COUNT && i < 8; i++)
				roots[i]->rootHidden = (keep & (1 << i)) == 0;
			break;
		}

		case CPM_PART_SKIN:
		{
			// V1 texture: a bare TextureProvider, with no sheet-type byte in
			// front of it, and always the SKIN sheet.
			int sx = b.readShort();
			int sy = b.readShort();
			std::vector<unsigned char> png;
			if (!b.readNextBlock(png)) { error = "bad skin block"; return false; }
			if (!png.empty())
			{
				skinPng = png;
				if (sx > 0) skinUvWidth = sx;
				if (sy > 0) skinUvHeight = sy;
			}
			break;
		}

		case CPM_PART_PLAYER_PARTPOS:
		{
			int rootId = b.readVarInt();
			CPMVec3 pos = b.readVec6b();
			CPMVec3 rot = b.readAngle();
			if (!b.fail() && rootId >= 0 && rootId < CPM_PP_COUNT)
			{
				roots[rootId]->cube.pos = pos;
				roots[rootId]->cube.rotation = rot;
			}
			break;
		}

		case CPM_PART_RENDER_EFFECT:
			if (!parseRenderEffect(b)) return false;
			break;

		case CPM_PART_CUBES:
		{
			int count = b.readVarInt();
			if (b.fail() || count < 0 || count > 8192)
			{
				error = "bad cube count";
				return false;
			}
			for (int i = 0; i < count; i++)
			{
				CPMCube c;
				if (!CPMCube::load(b, c)) { error = "bad cube data"; return false; }
				c.id = i + 10;
				cubes.push_back(c);
			}
			break;
		}

		case CPM_PART_TEXTURE:
		{
			int sheet = b.readByte();               // TextureSheetType ordinal
			int sx = b.readShort();                 // UV space width
			int sy = b.readShort();                 // UV space height
			std::vector<unsigned char> png;
			if (!b.readNextBlock(png)) { error = "bad texture block"; return false; }
			// Only the SKIN sheet (ordinal 0) is used by this port; cape,
			// elytra and armor sheets are parsed and discarded.
			if (sheet == 0 && !png.empty())
			{
				skinPng = png;
				if (sx > 0) skinUvWidth = sx;
				if (sy > 0) skinUvHeight = sy;
			}
			break;
		}

		case CPM_PART_ROOT_INFO:
		{
			signed char flags = b.readByte();
			int rootId = b.readByte();
			CPMVec3 pos, rot;
			if ((flags & CPM_ROOT_TRANSFORM) != 0)
			{
				pos = b.readVarVec3();
				rot = b.readAngle();
			}
			int createFrom = 0;
			if ((flags & CPM_ROOT_CREATE) != 0) createFrom = b.readVarInt();

			// Roots created from a custom cube (ROOT_CREATE) and non-player
			// roots (ROOT_MODEL - cape, elytra, armor) are not supported by
			// this port; only the six player bones are redirected.
			if (createFrom == 0 && (flags & CPM_ROOT_MODEL) == 0 &&
			    rootId >= 0 && rootId < CPM_PP_COUNT)
			{
				CPMRenderedCube *r = roots[rootId];
				r->rootHidden = (flags & CPM_ROOT_HIDDEN) != 0;
				r->disableVanilla = (flags & CPM_ROOT_DISABLE_VANILLA) != 0;
				r->cube.pos = pos;
				r->cube.rotation = rot;
			}
			break;
		}

		default:
			// Recognised but unported part type (templates, links, effects,
			// tags, scale, animated textures). Skipped deliberately.
			break;
		}

		if (sawEnd) break;
	}

	if (!sawEnd) { error = "no end marker"; return false; }
	return true;
}

// ModelPartDefinition: a varint cube count, V1 cube records, then a nested
// stream of object blocks terminated by END - all inside this one block.
bool CPMModelDefinition::parseDefinitionV1(CPMIn &in, std::vector<CPMCube> &cubes)
{
	int count = in.readVarInt();
	if (in.fail() || count < 0 || count > 8192)
	{
		error = "bad definition cube count";
		return false;
	}

	for (int i = 0; i < count; i++)
	{
		CPMCube c;
		if (!CPMCube::loadV1(in, c)) { error = "bad definition cube data"; return false; }
		c.id = i + 10;
		cubes.push_back(c);
	}

	return parsePartStream(in, cubes, true);
}

// V1 render effects refer to cubes by id, and the cubes are not built until
// parsing finishes, so these are recorded and applied afterwards.
bool CPMModelDefinition::parseRenderEffect(CPMIn &in)
{
	int type = in.readByte();
	if (in.fail()) return true;

	PendingEffect e;
	e.type = type;
	e.id = 0;
	e.mcScale = 0;
	e.rgb = 0;
	e.u = 0;
	e.v = 0;

	switch (type)
	{
	case CPM_FX_SCALE:
		e.id = in.readVarInt();
		e.mcScale = in.readFloat2();
		e.scale = in.readVec6b();
		break;

	case CPM_FX_HIDE:
	case CPM_FX_SINGLE_TEX:
	case CPM_FX_EXTRUDE:
		e.id = in.readVarInt();
		break;

	case CPM_FX_PER_FACE_UV:
	{
		// PerFaceUV.readFaces: a presence mask in Direction order, then only
		// the faces that are present.
		e.id = in.readVarInt();
		int mask = in.read();
		if (in.fail()) return true;
		for (int f = 0; f < CPM_FACE_COUNT; f++)
		{
			if ((mask & (1 << f)) == 0) continue;
			e.faceUV.used[f] = true;
			e.faceUV.any = true;
			e.faceUV.sx[f] = in.readVarInt();
			e.faceUV.sy[f] = in.readVarInt();
			e.faceUV.ex[f] = in.readVarInt();
			e.faceUV.ey[f] = in.readVarInt();
			int r = in.readByte();
			e.faceUV.rot[f] = (r >= 0 && r < 4) ? r : 0;
		}
		break;
	}

	case CPM_FX_COLOR:
	{
		e.id = in.readVarInt();
		int r = in.read();
		int g = in.read();
		int b2 = in.read();
		if (in.fail()) return true;
		e.rgb = (r << 16) | (g << 8) | b2;
		break;
	}

	case CPM_FX_UV_OVERFLOW:
		e.id = in.readVarInt();
		e.u = in.readVarInt();
		e.v = in.readVarInt();
		break;

	default:
		// Every other effect (glow, per-face UV, items, scaling, copy
		// transform, first-person hand) is not ported. Skipping the payload is
		// safe because each effect sits in its own RENDER_EFFECT block.
		return true;
	}

	if (!in.fail()) pendingEffects.push_back(e);
	return true;
}

void CPMModelDefinition::applyPendingEffects(std::vector<CPMCube> &cubes)
{
	for (size_t i = 0; i < pendingEffects.size(); i++)
	{
		const PendingEffect &e = pendingEffects[i];

		for (size_t j = 0; j < cubes.size(); j++)
		{
			if (cubes[j].id != e.id) continue;

			switch (e.type)
			{
			case CPM_FX_SCALE:
				cubes[j].meshScale = e.scale;
				cubes[j].mcScale = e.mcScale;
				break;
			case CPM_FX_HIDE:
				cubes[j].hidden = true;
				break;
			case CPM_FX_SINGLE_TEX:
				cubes[j].singleTex = true;
				break;
			case CPM_FX_EXTRUDE:
				cubes[j].extrude = true;
				break;
			case CPM_FX_PER_FACE_UV:
				cubes[j].faceUV = e.faceUV;
				break;
			case CPM_FX_COLOR:
				// Recolour tints a cube that keeps its texture, so texSize is
				// left alone and the recolor flag is what makes reset() pick
				// the tint up.
				cubes[j].rgb = e.rgb;
				cubes[j].recolor = true;
				break;
			case CPM_FX_UV_OVERFLOW:
				// V1 stores u/v in a single byte each; anything larger is sent
				// separately and replaces what the cube record carried.
				cubes[j].u = e.u;
				cubes[j].v = e.v;
				break;
			default:
				break;
			}
			break;
		}
	}
	pendingEffects.clear();
}

bool CPMModelDefinition::buildTree(std::vector<CPMCube> &cubes)
{
	// Cube.resolveCubesV2 - ids below 10 address a root, ids >= 10 another cube.
	std::map<int, CPMRenderedCube *> byId;
	size_t firstCube = allCubes.size();

	for (size_t i = 0; i < cubes.size(); i++)
	{
		CPMRenderedCube *rc = new CPMRenderedCube();
		rc->cube = cubes[i];
		allCubes.push_back(rc);
		byId[cubes[i].id] = rc;
	}

	for (size_t i = firstCube; i < allCubes.size(); i++)
	{
		CPMRenderedCube *rc = allCubes[i];
		int pid = rc->cube.parentId;
		CPMRenderedCube *parent = NULL;

		if (pid < 10)
		{
			if (pid < 0 || pid >= CPM_PP_COUNT)
			{
				error = "cube with invalid root parent";
				return false;
			}
			parent = roots[pid];
		}
		else
		{
			std::map<int, CPMRenderedCube *>::iterator it = byId.find(pid);
			if (it == byId.end())
			{
				error = "cube without parent";
				return false;
			}
			parent = it->second;
		}

		rc->parent = parent;
		parent->addChild(rc);
	}

	return true;
}

// Roots stand in for the vanilla bones. They exist whether or not the model
// references them, because cubes address them by ordinal.
void CPMModelDefinition::prepareRoots()
{
	clear();
	for (int i = 0; i < CPM_PP_COUNT; i++)
	{
		CPMRenderedCube *r = new CPMRenderedCube();
		r->isRoot = true;
		r->rootPart = i;
		r->cube.id = i;
		r->cube.parentId = -1;
		allCubes.push_back(r);
		roots[i] = r;
	}
}

void CPMModelDefinition::resetAnimationPos()
{
	for (size_t i = 0; i < allCubes.size(); i++) allCubes[i]->reset();
}

//////////////////////////////////////////////////////////////////////////
// .cpmmodel container
//////////////////////////////////////////////////////////////////////////

// Best-effort identification of a file that is not an exported model, so the
// error can say what to do about it.
std::string CPMIdentifyFile(const unsigned char *d, int len)
{
	if (d == NULL || len < 4) return "file is empty or too short to be a model";

	if (d[0] == 'P' && d[1] == 'K' && (d[2] == 3 || d[2] == 5 || d[2] == 7))
	{
		// .cpmproject files are supported, and are dispatched to the project
		// loader before this is ever reached - so a zip arriving here is one
		// that loader already rejected, or an unrelated archive.
		return "this is a zip archive but not a usable CPM project "
		       "(it needs a config.json inside)";
	}

	if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G')
	{
		// CPM can hide model data inside a skin PNG. That path is not ported.
		return "this is a PNG skin, not a .cpmmodel - models embedded in skins "
		       "are not supported by this port";
	}

	if (d[0] == '{' || d[0] == '[')
	{
		return "this is a JSON file (Blockbench .bbmodel or similar), not a "
		       ".cpmmodel - export it through the CPM editor first";
	}

	if (d[0] == 0x1F && d[1] == 0x8B) return "this file is gzip compressed, not a .cpmmodel";

	char buf[96];
	sprintf_s(buf, sizeof(buf),
	          "not a .cpmmodel file (starts with 0x%02X, expected 0x53)", d[0]);
	return std::string(buf);
}

bool CPMLoadModelFile(const unsigned char *file, int len,
                      std::string &nameOut, std::string &descOut,
                      std::vector<unsigned char> &dataBlockOut,
                      std::string &errOut)
{
	CPMIn in(file, len);
	if (in.read() != CPM_FILE_HEADER)
	{
		// Naming the format the file actually is saves a guessing round: the
		// usual mistake is handing over an editor project or a skin PNG rather
		// than an exported model.
		errOut = CPMIdentifyFile(file, len);
		return false;
	}
	in.beginChecksum();

	nameOut = in.readUTF();
	descOut = in.readUTF();
	if (in.fail()) { errOut = "truncated header"; return false; }

	if (!in.readByteArray(dataBlockOut)) { errOut = "truncated model data"; return false; }

	std::vector<unsigned char> overflow;
	if (!in.readByteArray(overflow)) { errOut = "truncated overflow block"; return false; }
	if (!overflow.empty())
	{
		// The model was too large for a single file and the rest lives behind a
		// Link (gist, pastebin, the CPM CDN). Fetching those is not ported.
		errOut = "model is split across an external link, which is not supported - "
		         "re-export it from the CPM editor without skin compatibility";
		return false;
	}

	std::vector<unsigned char> icon;
	if (!in.readNextBlock(icon)) { errOut = "truncated icon block"; return false; }

	if (!in.checkSum()) { errOut = "checksum mismatch (file corrupt or truncated)"; return false; }
	if (dataBlockOut.empty()) { errOut = "model contains no data"; return false; }

	return true;
}
