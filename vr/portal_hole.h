#pragma once
// The portal's black centre (`portalBack`, `Diablo_Smoke_Mesh.pkma`, Opaque) uses the solid
// sphere models `fx_mesh_portalSphere_01`, `fx_mesh_portalSphere`, and
// `fx_mesh_portalSphere_convex` (380 triangles, 230 vertices). The animated blue wavelet ring
// uses the four `fx_mesh_portalSphere_convex_long_hole_vertexalpha*` models (sphere meshes with
// a long hole cut out of the middle), while `fx_mesh_townportal_lip_crawl` and
// `fx_mesh_portalsphere_innerflow` play when the portal closes.
//
// [input] portal_face, flat F2/F3 only (not isometric F1): when one of the three solid centre
// models hits the material builder (vr/roof_sides.h), we remember its runtime `D2RRenderMesh`
// and zero triangle/vertex counts (`rm+0x158`, `rm+0x1CC`, `rm+0x21C`). Switching back to F1
// restores those counts so the oval draws again. Granny vertex buffers are left intact.

namespace portalhole {

enum Action { kBuild = 0, kCollapsed = 3 };

// A model path (any case, '/' or '\') of the black centre. The inner swirl matches
// "portalsphere" too, and is left out.
bool IsHolePath(const char* path);

// mode: 0 off (restore ovals), non-zero hide ovals (flat F2/F3 and [input] portal_face).
void Configure(int mode, void (*log)(const char*));

// The model material builder, before it runs. model is the file path, mesh the GrannyMesh*.
int OnModel(const char* model, void* file, void* mesh, const void* retAddr);

// Log a message through the portal hole logger.
void LogLine(const char* s);

}  // namespace portalhole
