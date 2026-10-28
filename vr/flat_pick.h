#pragma once
// Flat first person and flat third person: the game picks a unit where the cursor's ray meets the ground, so the
// clickable spot sits on the feet. This keeps a short list of body capsules and, when the
// camera ray passes through one, aims that ray at the unit's feet. The game's own ground
// test then finds the same unit. NPCs and monsters are unit type 1. Doors, shrines, chests,
// wells, waypoints, portals and stairs are unit type 2, with their own capsules.

#include <cstdint>

namespace flatpick {

void SetLog(void (*fn)(const char*));

// [input] flat_iso_ray: the ray handed to the game's ground test runs along the old isometric camera's
// direction -(k, 1, k), k = sqrt(3/2) (melee_bodies.h kGroundShear), through the point meant. Every point
// of that line has the same grid place, so the game's ground test lands there whatever plane it meets the
// ray with. [debug] flat_elev: a log of which frame the camera's look-at is in (render or grid) and which
// of the game's codes ask for rays. Both live (set at each ini load).
void SetModes(bool isoRay, bool elevLog);

// active: flat first or third person, in the world, no menu.
// heroX/Y/Z: camera look-at (same frame as path Place — not F4's sheared pose).
void Refresh(bool active, float heroX, float heroY, float heroZ);

// d2rcam's ray adjust (d2rcam::RayAdjustFn). When the ray hits a cached capsule, aims it at the unit's feet
// (origin stays the eye) or, with flat_iso_ray, replaces it by the isometric ray through the feet. With
// flat_iso_ray and no capsule hit, a ray through the crosshair (the viewport's middle) is replaced by the
// isometric ray through where it meets the hero's floor.
void Adjust(float origin[3], float dir[3], const float px[2], float vpW, float vpH, uintptr_t callerRva);

// [debug] flat_elev: on; and, at a click of the game's mouse code, the game's hover result against the last ray handed to it (type: the click's
// type, hovered: the game found a unit under the pointer).
bool ElevOn();
void LogClick(int type, bool hovered);
// The probe reads the game's own hover (vrcam: HoverUnit of the local player): false when it cannot, else hovered and the unit's type and class.
void SetHoverProbe(bool (*fn)(bool* hovered, uint32_t* type, uint32_t* cls));

// [input] portal_auto (flat F2 / F3): walking into a portal enters it. The hero near a portal with a move key held aims the crosshair's ray at the portal's
// feet; once the game's own hover reports the portal, the left button goes down and, a moment later, up (click(down) -> false: not sent). After a new area
// (areaGen) nothing is entered until the hero has walked away from the portals he stands by. radius: grid (world) units; delayMs: how long he stays near.
void SetPortalClick(bool (*click)(bool down));
void PortalAuto(bool wanted, bool moving, uint32_t areaGen, float hx, float hy, float hz, float radius, int delayMs);
bool PortalForcing();   // the ray is being forced at a portal: a click then is not a shot at the crosshair

// Live monster/NPC: ground place and MonStats body height (world units).
// floorY is the feet height (hero ground). false if not a monster or unread.
// cls (optional): the unit's class, to tell the unit meant from another that took its memory.
bool MonsterPose(uintptr_t unit, float floorY, float* x, float* y, float* z, float* height, uint32_t* cls = nullptr);

// Nearest live monster/NPC within reach of look-at (hx,hz); hy kept for call-site symmetry.
uintptr_t NearestMonster(float hx, float hy, float hz, float reach);

}  // namespace flatpick
