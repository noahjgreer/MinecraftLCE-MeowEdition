#include "stdafx.h"
#include "CPMModel.h"
#include "CPMRender.h"
#include "../../Tesselator.h"
#include <math.h>

// Degrees per radian, matching ModelPart::RAD.
static const float CPM_RAD = 180.0f / 3.14159265358979323846f;

namespace
{
	struct CPMVert
	{
		float x, y, z;
		float u, v;
	};

	// Emit one quad. Vertex/UV pairing follows BoxRender.Quad: v[0] takes
	// (u1,v0), v[1] (u0,v0), v[2] (u0,v1), v[3] (u1,v1).
	void emitQuad(Tesselator *t, CPMVert *q,
	              float u0, float v0, float u1, float v1,
	              float sheetW, float sheetH, bool mirror,
	              float nx, float ny, float nz, float scale)
	{
		q[0].u = u1 / sheetW; q[0].v = v0 / sheetH;
		q[1].u = u0 / sheetW; q[1].v = v0 / sheetH;
		q[2].u = u0 / sheetW; q[2].v = v1 / sheetH;
		q[3].u = u1 / sheetW; q[3].v = v1 / sheetH;

		if (mirror)
		{
			CPMVert tmp = q[0]; q[0] = q[3]; q[3] = tmp;
			tmp = q[1]; q[1] = q[2]; q[2] = tmp;
		}

		t->normal(nx, ny, nz);
		for (int i = 0; i < 4; i++)
			t->vertexUV(q[i].x * scale, q[i].y * scale, q[i].z * scale, q[i].u, q[i].v);
	}

	// Emit a quad whose UVs are already normalised, and whose four vertices
	// each carry their own UV. Used by the per-face and extruded paths.
	void emitQuadUV(Tesselator *t, const CPMVert *q,
	                float nx, float ny, float nz, float scale)
	{
		t->normal(nx, ny, nz);
		for (int i = 0; i < 4; i++)
			t->vertexUV(q[i].x * scale, q[i].y * scale, q[i].z * scale, q[i].u, q[i].v);
	}

	int cpmCeil(float f)
	{
		int i = (int)f;
		return f > (float)i ? i + 1 : i;
	}

	// A hostile or careless model could ask for an enormous extrusion; the comb
	// along each edge costs four vertices per texel.
	#define CPM_MAX_EXTRUDE_TEXELS 256

	// BoxRender.createTexturedExtruded + ExtrudedMesh.
	//
	// The reference builds the mesh in a unit space - x,y in [0,1] and z in
	// [-1,0] - and maps it into the box with a matrix. Here the same mapping is
	// applied per vertex instead, in model units, because the caller already
	// multiplies positions by the model scale.
	//
	// The result is a flat quad given real thickness, with a comb of side faces
	// one per texel so the silhouette follows the texture.
	void renderExtruded(Tesselator *t, const CPMCube &c, float scale,
	                    int sheetW, int sheetH)
	{
		float x = c.offset.x, y = c.offset.y, z = c.offset.z;
		float w = c.size.x,   h = c.size.y,   d = c.size.z;
		float delta = c.mcScale;

		int ts = c.texSize < 0 ? -c.texSize : c.texSize;
		if (ts == 0) ts = 1;
		int dx = cpmCeil(w * ts);
		int dy = cpmCeil(h * ts);
		if (dx < 1) dx = 1;
		if (dy < 1) dy = 1;
		if (dx > CPM_MAX_EXTRUDE_TEXELS) dx = CPM_MAX_EXTRUDE_TEXELS;
		if (dy > CPM_MAX_EXTRUDE_TEXELS) dy = CPM_MAX_EXTRUDE_TEXELS;

		x -= delta;
		y -= delta;
		z -= delta;
		w = w * c.meshScale.x + delta * 2;
		h = h * c.meshScale.y + delta * 2;
		d = d * c.meshScale.z + delta * 2;

		float texU = (float)(c.u * ts);
		float texV = (float)(c.v * ts);

		// The unit-space -> model-space mapping, mirrored on X for a negative
		// texture size exactly as the reference's matrix is.
		float tx, ty, tz, sx, sy, sz;
		if (c.texSize < 0)
		{
			tx = x;     ty = y + h; tz = z;
			sx = w;     sy = -h;    sz = -d;
		}
		else
		{
			tx = x + w; ty = y + h; tz = z + d;
			sx = -w;    sy = -h;    sz = d;
		}

		// Axis-aligned normals only need the sign of the scale.
		const float nsx = sx < 0 ? -1.0f : 1.0f;
		const float nsy = sy < 0 ? -1.0f : 1.0f;
		const float nsz = sz < 0 ? -1.0f : 1.0f;

		const float minU = texU / (float)sheetW;
		const float minV = texV / (float)sheetH;
		const float maxU = (texU + dx) / (float)sheetW;
		const float maxV = (texV + dy) / (float)sheetH;

		CPMVert q[4];

		// Places a unit-space vertex into model space.
		#define CPM_EX_V(idx, ux, uy, uz, uu, uv)          			q[idx].x = tx + (ux) * sx;                     			q[idx].y = ty + (uy) * sy;                     			q[idx].z = tz + (uz) * sz;                     			q[idx].u = (uu);                               			q[idx].v = (uv)

		t->begin();

		// Front and back faces.
		CPM_EX_V(0, 0.0f, 0.0f, 0.0f, maxU, maxV);
		CPM_EX_V(1, 1.0f, 0.0f, 0.0f, minU, maxV);
		CPM_EX_V(2, 1.0f, 1.0f, 0.0f, minU, minV);
		CPM_EX_V(3, 0.0f, 1.0f, 0.0f, maxU, minV);
		emitQuadUV(t, q, 0, 0, 1.0f * nsz, scale);

		CPM_EX_V(0, 0.0f, 1.0f, -1.0f, maxU, minV);
		CPM_EX_V(1, 1.0f, 1.0f, -1.0f, minU, minV);
		CPM_EX_V(2, 1.0f, 0.0f, -1.0f, minU, maxV);
		CPM_EX_V(3, 0.0f, 0.0f, -1.0f, maxU, maxV);
		emitQuadUV(t, q, 0, 0, -1.0f * nsz, scale);

		// Half a texel, so each side column samples the middle of its texel
		// rather than the seam between two.
		const float halfU = 0.5f * (maxU - minU) / dx;
		const float halfV = 0.5f * (maxV - minV) / dy;

		for (int k = 0; k < dx; k++)
		{
			const float f7 = (float)k / (float)dx;
			const float f8 = maxU + (minU - maxU) * f7 - halfU;

			CPM_EX_V(0, f7, 0.0f, -1.0f, f8, maxV);
			CPM_EX_V(1, f7, 0.0f,  0.0f, f8, maxV);
			CPM_EX_V(2, f7, 1.0f,  0.0f, f8, minV);
			CPM_EX_V(3, f7, 1.0f, -1.0f, f8, minV);
			emitQuadUV(t, q, -1.0f * nsx, 0, 0, scale);
		}

		for (int k = 0; k < dx; k++)
		{
			const float f7 = (float)k / (float)dx;
			const float f8 = maxU + (minU - maxU) * f7 - halfU;
			const float f9 = f7 + 1.0f / dx;

			CPM_EX_V(0, f9, 1.0f, -1.0f, f8, minV);
			CPM_EX_V(1, f9, 1.0f,  0.0f, f8, minV);
			CPM_EX_V(2, f9, 0.0f,  0.0f, f8, maxV);
			CPM_EX_V(3, f9, 0.0f, -1.0f, f8, maxV);
			emitQuadUV(t, q, 1.0f * nsx, 0, 0, scale);
		}

		for (int k = 0; k < dy; k++)
		{
			const float f7 = (float)k / (float)dy;
			const float f8 = maxV + (minV - maxV) * f7 - halfV;
			const float f9 = f7 + 1.0f / dy;

			CPM_EX_V(0, 0.0f, f9,  0.0f, maxU, f8);
			CPM_EX_V(1, 1.0f, f9,  0.0f, minU, f8);
			CPM_EX_V(2, 1.0f, f9, -1.0f, minU, f8);
			CPM_EX_V(3, 0.0f, f9, -1.0f, maxU, f8);
			emitQuadUV(t, q, 0, 1.0f * nsy, 0, scale);
		}

		for (int k = 0; k < dy; k++)
		{
			const float f7 = (float)k / (float)dy;
			const float f8 = maxV + (minV - maxV) * f7 - halfV;

			CPM_EX_V(0, 1.0f, f7,  0.0f, minU, f8);
			CPM_EX_V(1, 0.0f, f7,  0.0f, maxU, f8);
			CPM_EX_V(2, 0.0f, f7, -1.0f, maxU, f8);
			CPM_EX_V(3, 1.0f, f7, -1.0f, minU, f8);
			emitQuadUV(t, q, 0, -1.0f * nsy, 0, scale);
		}

		t->end();

		#undef CPM_EX_V
	}

	// BoxRender.createTextured / createColored. `pos` is the cube offset,
	// `size` its dimensions, `sc` its mesh scale, `delta` its mcScale inflation.
	void renderBox(Tesselator *t, const CPMCube &c, float scale, int sheetW, int sheetH)
	{
		float x = c.offset.x, y = c.offset.y, z = c.offset.z;
		float w = c.size.x,   h = c.size.y,   d = c.size.z;
		float delta = c.mcScale;

		// A texSize of 0 means the cube is flat-coloured rather than textured.
		// createColored forces a texel scale of 1 and never mirrors. It looks
		// like it also ignores meshScale, but the caller (createBox) passes it
		// a size that has already been multiplied by meshScale, so the scaling
		// still applies - getting that wrong shrinks every coloured cube.
		bool colored = (c.texSize == 0);

		if (colored)
		{
			w *= c.meshScale.x;
			h *= c.meshScale.y;
			d *= c.meshScale.z;
		}

		int ts = colored ? 1 : (c.texSize < 0 ? -c.texSize : c.texSize);
		int dx = cpmCeil(w * ts);
		int dy = cpmCeil(h * ts);
		int dz = cpmCeil(d * ts);

		float ex, ey, ez;
		if (colored)
		{
			ex = x + w;
			ey = y + h;
			ez = z + d;
		}
		else
		{
			ex = x + w * c.meshScale.x;
			ey = y + h * c.meshScale.y;
			ez = z + d * c.meshScale.z;
		}

		x -= delta; y -= delta; z -= delta;
		ex += delta; ey += delta; ez += delta;

		float texU = colored ? 0.0f : (float)(c.u * ts);
		float texV = colored ? 0.0f : (float)(c.v * ts);

		bool mirror = !colored && c.texSize < 0;
		if (mirror)
		{
			float s = ex;
			ex = x;
			x = s;
		}

		// Eight corners, named as in BoxRender.
		CPMVert v7 = { x,  y,  z  };
		CPMVert v0 = { ex, y,  z  };
		CPMVert v1 = { ex, ey, z  };
		CPMVert v2 = { x,  ey, z  };
		CPMVert v3 = { x,  y,  ez };
		CPMVert v4 = { ex, y,  ez };
		CPMVert v5 = { ex, ey, ez };
		CPMVert v6 = { x,  ey, ez };

		float f4 = texU;
		float f5 = texU + dz;
		float f6 = texU + dz + dx;
		float f7 = texU + dz + dx + dx;
		float f8 = texU + dz + dx + dz;
		float f9 = texU + dz + dx + dz + dx;
		float f10 = texV;
		float f11 = texV + dz;
		float f12 = texV + dz + dy;

		float sw = (float)sheetW;
		float sh = (float)sheetH;
		CPMVert q[4];

		// Per-face UV replaces the whole unwrapped layout: each face carries its
		// own rectangle and quarter-turn, and a face the model deleted is simply
		// not emitted. BoxRender's per-face path does not mirror.
		if (!colored && c.faceUV.any)
		{
			// {face, four corner vertices, normal}, matching the order in
			// BoxRender.createTextured(..., PerFaceUV, ...).
			const CPMVert *corners[CPM_FACE_COUNT][4] =
			{
				{ &v4, &v3, &v7, &v0 },   // UP
				{ &v1, &v2, &v6, &v5 },   // DOWN
				{ &v0, &v7, &v2, &v1 },   // NORTH
				{ &v3, &v4, &v5, &v6 },   // SOUTH
				{ &v4, &v0, &v1, &v5 },   // EAST
				{ &v7, &v3, &v6, &v2 },   // WEST
			};
			static const float NORMALS[CPM_FACE_COUNT][3] =
			{
				{  0, -1,  0 },           // UP
				{  0,  1,  0 },           // DOWN
				{  0,  0, -1 },           // NORTH
				{  0,  0,  1 },           // SOUTH
				{  1,  0,  0 },           // EAST
				{ -1,  0,  0 },           // WEST
			};

			t->begin();
			for (int f = 0; f < CPM_FACE_COUNT; f++)
			{
				if (!c.faceUV.used[f]) continue;

				for (int i = 0; i < 4; i++)
				{
					q[i] = *corners[f][i];
					q[i].u = c.faceUV.u(f, i) / sw;
					q[i].v = c.faceUV.v(f, i) / sh;
				}
				emitQuadUV(t, q, NORMALS[f][0], NORMALS[f][1], NORMALS[f][2], scale);
			}
			t->end();
			return;
		}

		// A "single texture" cube maps one square UV patch onto every face
		// instead of the unwrapped box layout (BoxRender.createTexturedSingle).
		if (!colored && c.singleTex)
		{
			int txS = dx;
			if (dy > txS) txS = dy;
			if (dz > txS) txS = dz;
			const float su = texU, sv = texV;
			const float eu = texU + txS, ev = texV + txS;

			t->begin();

			q[0] = v4; q[1] = v3; q[2] = v7; q[3] = v0;
			emitQuad(t, q, su, sv, eu, ev, sw, sh, mirror, 0, -1, 0, scale);
			q[0] = v1; q[1] = v2; q[2] = v6; q[3] = v5;
			emitQuad(t, q, su, sv, eu, ev, sw, sh, mirror, 0, 1, 0, scale);
			q[0] = v7; q[1] = v3; q[2] = v6; q[3] = v2;
			emitQuad(t, q, su, sv, eu, ev, sw, sh, mirror, -1, 0, 0, scale);
			q[0] = v0; q[1] = v7; q[2] = v2; q[3] = v1;
			emitQuad(t, q, su, sv, eu, ev, sw, sh, mirror, 0, 0, -1, scale);
			q[0] = v4; q[1] = v0; q[2] = v1; q[3] = v5;
			emitQuad(t, q, su, sv, eu, ev, sw, sh, mirror, 1, 0, 0, scale);
			q[0] = v3; q[1] = v4; q[2] = v5; q[3] = v6;
			emitQuad(t, q, su, sv, eu, ev, sw, sh, mirror, 0, 0, 1, scale);

			t->end();
			return;
		}

		t->begin();

		q[0] = v4; q[1] = v3; q[2] = v7; q[3] = v0;
		emitQuad(t, q, f5, f10, f6, f11, sw, sh, mirror, 0, -1, 0, scale);

		q[0] = v1; q[1] = v2; q[2] = v6; q[3] = v5;
		emitQuad(t, q, f6, f11, f7, f10, sw, sh, mirror, 0, 1, 0, scale);

		q[0] = v7; q[1] = v3; q[2] = v6; q[3] = v2;
		emitQuad(t, q, f4, f11, f5, f12, sw, sh, mirror, -1, 0, 0, scale);

		q[0] = v0; q[1] = v7; q[2] = v2; q[3] = v1;
		emitQuad(t, q, f5, f11, f6, f12, sw, sh, mirror, 0, 0, -1, scale);

		q[0] = v4; q[1] = v0; q[2] = v1; q[3] = v5;
		emitQuad(t, q, f6, f11, f8, f12, sw, sh, mirror, 1, 0, 0, scale);

		q[0] = v3; q[1] = v4; q[2] = v5; q[3] = v6;
		emitQuad(t, q, f8, f11, f9, f12, sw, sh, mirror, 0, 0, 1, scale);

		t->end();
	}

	void renderNode(CPMRenderedCube *node, float scale, int sheetW, int sheetH)
	{
		Tesselator *t = Tesselator::getInstance();

		for (size_t i = 0; i < node->children.size(); i++)
		{
			CPMRenderedCube *c = node->children[i];

			glPushMatrix();

			// Transform order matches RedirectRenderer.translateRotate:
			// translate, then rotate Z-Y-X, then the per-cube render scale.
			glTranslatef(c->pos.x * scale, c->pos.y * scale, c->pos.z * scale);
			if (c->rotation.z != 0) glRotatef(c->rotation.z * CPM_RAD, 0, 0, 1);
			if (c->rotation.y != 0) glRotatef(c->rotation.y * CPM_RAD, 0, 1, 0);
			if (c->rotation.x != 0) glRotatef(c->rotation.x * CPM_RAD, 1, 0, 0);

			float sx = c->renderScale.x < 0.01f ? 0.01f : c->renderScale.x;
			float sy = c->renderScale.y < 0.01f ? 0.01f : c->renderScale.y;
			float sz = c->renderScale.z < 0.01f ? 0.01f : c->renderScale.z;
			if (sx != 1.0f || sy != 1.0f || sz != 1.0f) glScalef(sx, sy, sz);

			// A hidden cube still transforms its children; it just draws nothing.
			if (c->display && c->cube.hasMesh)
			{
				if (c->color != 0xffffff)
				{
					// The Tesselator forces vertex colours to white unless a
					// colour was supplied for this batch, so glColor alone would
					// be ignored here - the tint has to go through the Tesselator.
					t->color(((c->color >> 16) & 0xff) / 255.0f,
					         ((c->color >> 8) & 0xff) / 255.0f,
					         (c->color & 0xff) / 255.0f);
				}
				if (c->cube.extrude && c->cube.texSize != 0)
					renderExtruded(t, c->cube, scale, sheetW, sheetH);
				else
					renderBox(t, c->cube, scale, sheetW, sheetH);
			}

			renderNode(c, scale, sheetW, sheetH);

			glPopMatrix();
		}
	}
}

void CPMRenderCubeTree(CPMRenderedCube *root, float scale, int sheetW, int sheetH)
{
	if (root == NULL) return;
	if (sheetW <= 0) sheetW = 64;
	if (sheetH <= 0) sheetH = 64;
	renderNode(root, scale, sheetW, sheetH);
}
