#pragma once
// CPM - Custom Player Models
//
// Port of com.tom.cpm.shared.model.render.BoxRender plus the cube-tree walk in
// ModelRenderManager.RedirectRenderer.render.
//
// CPM builds a Mesh per cube and draws it through a batched vertex buffer. LCE
// has no such buffer, so this emits quads straight to the Tesselator the same
// way ModelPart/Cube do, one begin/end per quad. Geometry, UV layout and
// transform order all match the Java mod exactly so that models look the same.

class CPMRenderedCube;

// Draws every child of `root` (the root itself is a bone stand-in and has no
// geometry of its own). `scale` is the usual 0.0625f model scale; sheetW/sheetH
// are the model's UV space, normally 64x64.
void CPMRenderCubeTree(CPMRenderedCube *root, float scale, int sheetW, int sheetH);
