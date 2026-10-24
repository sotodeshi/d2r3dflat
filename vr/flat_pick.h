#pragma once
// Flat first person and flat third person: the game picks a unit where the cursor's ray meets the ground, so the
// clickable spot sits on the feet. This keeps a short list of body capsules and, when the
// camera ray passes through one, aims that ray at the unit's feet. The game's own ground
// test then finds the same unit. NPCs and monsters are unit type 1. Doors, shrines, chests,
// wells, waypoints, portals and stairs are unit type 2, with their own capsules.

namespace flatpick {

void SetLog(void (*fn)(const char*));

// active: flat first or third person, in the world, no menu.
// heroX/Y/Z: camera look-at (same frame as path Place — not F4's sheared pose).
void Refresh(bool active, float heroX, float heroY, float heroZ);

// Rewrites dir when the ray hits a cached capsule. origin stays the eye.
void Adjust(float origin[3], float dir[3]);

// Live monster/NPC: ground place and MonStats body height (world units).
// floorY is the feet height (hero ground). false if not a monster or unread.
bool MonsterPose(uintptr_t unit, float floorY, float* x, float* y, float* z, float* height);

// Nearest live monster/NPC within reach of look-at (hx,hz); hy kept for call-site symmetry.
uintptr_t NearestMonster(float hx, float hy, float hz, float reach);

}  // namespace flatpick
