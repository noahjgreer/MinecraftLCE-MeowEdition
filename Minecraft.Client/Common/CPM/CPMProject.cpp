#include "stdafx.h"
#include "CPMProject.h"
#include "CPMModel.h"
#include "CPMZip.h"
#include "CPMJson.h"
#include <stdlib.h>
#include <string.h>

#ifndef CPM_PI
#define CPM_PI 3.14159265358979323846
#endif

namespace
{
	// Root element ids, in PlayerModelParts order. ElementsLoaderV1 matches on
	// VanillaModelPart.getName(), which is the lowercased enum name.
	const char *ROOT_IDS[CPM_PP_COUNT] =
	{
		"head", "body", "left_arm", "right_arm", "left_leg", "right_leg", "custom_part"
	};

	int rootIdFor(const std::string &id)
	{
		for (int i = 0; i < CPM_PP_COUNT; i++)
		{
			if (id.size() == strlen(ROOT_IDS[i]) &&
			    _stricmp(id.c_str(), ROOT_IDS[i]) == 0) return i;
		}
		return -1;
	}

	float degToRad(float deg)
	{
		return (float)(deg * CPM_PI / 180.0);
	}

	// PerFaceUV(JsonMap): a member per Direction, each {sx,sy,ex,ey,rot}. A
	// direction that is absent means that face is not drawn at all.
	const char *FACE_KEYS[CPM_FACE_COUNT] =
	{
		"up", "down", "north", "south", "east", "west"
	};

	void readFaceUV(const CPMJson &js, int map, CPMFaceUV &out)
	{
		int fuv = js.member(map, "faceUV");
		if (fuv < 0 || js.type(fuv) != CPM_JSON_OBJECT) return;

		for (int f = 0; f < CPM_FACE_COUNT; f++)
		{
			int fm = js.member(fuv, FACE_KEYS[f]);
			if (fm < 0 || js.type(fm) != CPM_JSON_OBJECT) continue;

			out.used[f] = true;
			out.any = true;
			out.sx[f] = (int)js.getNum(fm, "sx", 0);
			out.sy[f] = (int)js.getNum(fm, "sy", 0);
			out.ex[f] = (int)js.getNum(fm, "ex", 0);
			out.ey[f] = (int)js.getNum(fm, "ey", 0);

			// The project stores the rotation as the enum name minus its
			// "ROT_" prefix, so "0" / "90" / "180" / "270" as a string.
			std::string r = js.getStr(fm, "rot", "0");
			if (r == "90")       out.rot[f] = 1;
			else if (r == "180") out.rot[f] = 2;
			else if (r == "270") out.rot[f] = 3;
			else                 out.rot[f] = 0;
		}
	}

	// ElementsLoaderV1.loadElement, field for field.
	void readCube(const CPMJson &js, int map, CPMCube &c)
	{
		bool texture = js.getBool(map, "texture", true);
		int textureSize = (int)js.getNum(map, "textureSize", 1);
		bool mirror = js.getBool(map, "mirror", false);

		// ElementType: texSize = texture ? (mirror ? -textureSize : textureSize) : 0
		c.texSize = texture ? (mirror ? -textureSize : textureSize) : 0;

		js.getVec3(map, "offset", c.offset.x, c.offset.y, c.offset.z, 0, 0, 0);
		js.getVec3(map, "pos", c.pos.x, c.pos.y, c.pos.z, 0, 0, 0);
		js.getVec3(map, "size", c.size.x, c.size.y, c.size.z, 1, 1, 1);

		// "scale" is the mesh scale and "rscale" is the render scale - the two
		// are swapped relative to what the names suggest.
		js.getVec3(map, "scale", c.meshScale.x, c.meshScale.y, c.meshScale.z, 1, 1, 1);
		js.getVec3(map, "rscale", c.scale.x, c.scale.y, c.scale.z, 1, 1, 1);

		float rx = 0, ry = 0, rz = 0;
		js.getVec3(map, "rotation", rx, ry, rz, 0, 0, 0);
		c.rotation = CPMVec3(degToRad(rx), degToRad(ry), degToRad(rz));

		c.u = (int)js.getNum(map, "u", 0);
		c.v = (int)js.getNum(map, "v", 0);
		c.mcScale = (float)js.getNum(map, "mcScale", 0);

		// The colour is a hex string, and is what a non-textured cube renders
		// as. Recolour tints a cube that keeps its texture.
		std::string col = js.getStr(map, "color", "0");
		c.rgb = (int)strtoul(col.c_str(), NULL, 16);
		c.recolor = js.getBool(map, "recolor", false);
		c.singleTex = js.getBool(map, "singleTex", false);
		c.extrude = js.getBool(map, "extrude", false);
		readFaceUV(js, map, c.faceUV);

		// "show" on a child is the editor's visibility toggle, not the model's;
		// "hidden" is what actually hides a cube at render time.
		c.hidden = js.getBool(map, "hidden", false);

		c.hasMesh = !(c.size.x == 0 && c.size.y == 0 && c.size.z == 0);
	}

	// Recursively walks a children array, assigning ids as it goes.
	bool readChildren(const CPMJson &js, int list, int parentId,
	                  std::vector<CPMCube> &out, int &nextId, int depth,
	                  std::string &errOut)
	{
		if (depth > 32) { errOut = "model nested too deeply"; return false; }

		int n = js.count(list);
		for (int i = 0; i < n; i++)
		{
			int map = js.at(list, i);
			if (js.type(map) != CPM_JSON_OBJECT) continue;

			if ((int)out.size() >= 8192) { errOut = "model has too many cubes"; return false; }

			CPMCube c;
			readCube(js, map, c);
			c.parentId = parentId;
			c.id = nextId++;
			out.push_back(c);

			int myId = c.id;
			int kids = js.member(map, "children");
			if (kids >= 0 && js.type(kids) == CPM_JSON_ARRAY)
			{
				if (!readChildren(js, kids, myId, out, nextId, depth + 1, errOut)) return false;
			}
		}
		return true;
	}
}

bool CPMLoadProject(const unsigned char *file, int len,
                    CPMModelDefinition *def, std::string &errOut)
{
	errOut.clear();
	if (def == NULL) return false;

	std::vector<unsigned char> cfg;
	if (!CPMZipExtract(file, len, "config.json", cfg, errOut)) return false;
	if (cfg.empty()) { errOut = "config.json is empty"; return false; }

	CPMJson js;
	if (!js.parse((const char *)&cfg[0], (int)cfg.size()))
	{
		errOut = "config.json is not valid JSON: " + js.error;
		return false;
	}

	int rootObj = js.root;
	if (js.type(rootObj) != CPM_JSON_OBJECT)
	{
		errOut = "config.json is not an object";
		return false;
	}

	def->prepareRoots();

	// The UV space the editor authored against.
	int skinSize = js.member(rootObj, "skinSize");
	if (skinSize >= 0)
	{
		def->skinUvWidth  = (int)js.getNum(skinSize, "x", 64);
		def->skinUvHeight = (int)js.getNum(skinSize, "y", 64);
	}
	if (def->skinUvWidth  <= 0) def->skinUvWidth = 64;
	if (def->skinUvHeight <= 0) def->skinUvHeight = 64;

	int elements = js.member(rootObj, "elements");
	if (elements < 0 || js.type(elements) != CPM_JSON_ARRAY)
	{
		errOut = "config.json has no elements";
		return false;
	}

	std::vector<CPMCube> cubes;
	int nextId = 10;

	int rootCount = js.count(elements);
	for (int i = 0; i < rootCount; i++)
	{
		int map = js.at(elements, i);
		if (js.type(map) != CPM_JSON_OBJECT) continue;

		// Duplicated roots and custom (cape/elytra/armour) parts are not
		// supported by this port; skipping them leaves the rest intact.
		if (js.getBool(map, "customPart", false)) continue;
		if (js.getBool(map, "dup", false)) continue;

		int rootId = rootIdFor(js.getStr(map, "id", ""));
		if (rootId < 0) continue;

		CPMRenderedCube *r = def->roots[rootId];

		// "show" false means the vanilla bone is hidden, which is the project
		// equivalent of the PLAYER keep bitmask in the binary format.
		r->rootHidden = !js.getBool(map, "show", true);
		r->disableVanilla = js.getBool(map, "disableVanillaAnim", false);

		float px = 0, py = 0, pz = 0, rx = 0, ry = 0, rz = 0;
		js.getVec3(map, "pos", px, py, pz, 0, 0, 0);
		js.getVec3(map, "rotation", rx, ry, rz, 0, 0, 0);
		r->cube.pos = CPMVec3(px, py, pz);
		r->cube.rotation = CPMVec3(degToRad(rx), degToRad(ry), degToRad(rz));

		int kids = js.member(map, "children");
		if (kids >= 0 && js.type(kids) == CPM_JSON_ARRAY)
		{
			if (!readChildren(js, kids, rootId, cubes, nextId, 0, errOut)) return false;
		}
	}

	if (!def->buildTree(cubes))
	{
		errOut = def->error.empty() ? "could not build the model tree" : def->error;
		return false;
	}

	// The texture is a plain PNG entry. A project without one is still valid -
	// the model then draws with the player's normal skin.
	std::string texErr;
	std::vector<unsigned char> png;
	if (CPMZipExtract(file, len, "skin.png", png, texErr) && !png.empty())
	{
		def->skinPng = png;
	}

	def->resetAnimationPos();
	return true;
}
