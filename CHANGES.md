# Flat-mode changes on D2R VR 0.171

These notes are for the D2R VR authors. They describe a local build of the public tree
(commit `51a014c`, `g_info_version` `0.171.0`) played on a monitor, `[mode] platform=0`,
first person on F3. The game build under test was D2R 3.3.0 (93847). D2RLoader is 1.3.1.
Nothing here changes VR headset play on purpose.

The plugin is still `d2rl-vrcam.dll`, id `d2r-vr-vrcam`. On this machine it is loaded from
`d2rloader\plugins` (the 0.169 layout). An official update from 0.170 onward moves that
file to `mods\D2R-VR\d2rloader\plugins` and deletes the global copy.

## 1. Clicking a body, a door, a shrine, or a transition

In flat first person the game still picks a unit where the view ray meets the ground, so
the only spot that selects a character is the feet. Highlight and labels stay at the feet.
There is no ini key that lifts the click spot.

The fix does not draw a new hitbox. Each frame it keeps a short list of vertical capsules.
When the camera ray hits one, `HookScreenToRay` rewrites the ray so it passes through that
unit's feet. The game's own ground test then selects the same unit. The hand-aim ray
(`SetRayOverride`) is not rewritten, and neither are shadow rays.

`[input] flat_body_pick=1` (default on) turns it on in flat first person (F3).
`[input] flat_body_pick_third=1` (default on) turns the same aim on in flat third person
(F2). Either runs only when `platform=0`, the player is in the world, and no menu is open.
Reach is 55 world units from the hero. The 48 nearest bodies and the 32 nearest objects are
kept separately. The third-person camera sits behind the hero; the ray still starts at that
camera, and a hit still aims it at the unit's feet.

Bodies are unit type 1 (NPCs and monsters share that hash). Dead modes 0 and 12 are
skipped. Height comes from the MonStats row (`pixHeight * 0.10` when that byte is in
20..200, otherwise 7). Radius comes from the footprint bytes, clamped to 1.5..3.5.
Feet Y is the hero's ground (the path has no elevation). Capsules sit at the unit's grid
place sheared up by that floor's height, where the picture draws it; see section 9 (the camera's
look-at is NOT on the grid on raised ground, and the ray is handed to the game along the old
isometric line).

Objects are unit type 2. The place is the static path at `+0x10` / `+0x14`, converted the
same way as the torch lights (`2 * tile + 1`). Which ids count is a table generated from
the 1.13 `Objects.txt` ids that D2R kept (`vr/flat_pick_obj.inc`): doors, shrines, chests
and urns, wells, the stash, waypoints, portals, stairs, trap doors, and the level
entrances that are real objects. Barrels, torches, and scenery are left out. An opened
door, a used shrine, or an opened chest (mode above 1) is left out so it does not block
the doorway. A warp, a portal, and a waypoint stay selectable in every mode.

Items, loose floor warps that are not objects, and doors wider than the capped radius can
still miss. The visible highlight can stay at the feet while the cursor is on the chest.

Files: `vr/flat_pick.h`, `vr/flat_pick.cpp`, `vr/flat_pick_obj.inc`,
`cleanroom/camera/d2rcam.h`, `cleanroom/camera/d2rcam.cpp` (`SetRayAdjust`),
`vr/vrcam.cpp` (ini key, `Refresh` each frame, `SetRayAdjust` at load), `CMakeLists.txt`,
`d2r_vr.ini`.

## 2. Sky and fog while a panel is open

Opening a dialog, a chest panel, the inventory, the stash, or Esc used to turn the sky,
the fog, and the cave ceiling off (`MenuFlat`). On a monitor the world stays behind the
panel, so those effects now stay on. In VR a flat menu still pauses them, because there
the menu fills the picture.

The mouse kept turning while the picture looked frozen. The pointer was free over the
panel, and the first sample after the panel closed applied the whole travel as yaw and
pitch, so leaving a dialog could leave the hero looking at the sky. That sample is now
dropped and the pointer is put back in the middle. Mouse look itself stays off for as
long as the menu is open, which was already the case.

## 3. Panels washed out by the sky

Keeping the sky on made every panel look half transparent. The interface layer is what
tells `D2R_DepthFog.fx` which pixels to leave alone, and that copy was skipped whenever
a menu was open, so the effect had nothing to protect.

The layer is copied during a menu as well and left in the picture. `UiMaskSolid` then
keeps the game's own colour for any pixel the interface covers. The sky and the fog stay
in the gap between panels. This needs the effect and the DLL together: `UiMaskSolid` was
added to `D2R_DepthFog.fx`, and the plugin sets it only for a menu on the monitor.

The effect the game actually runs on this machine is
`E:\battlenet\d2r\reshade-shaders\Shaders\D2R_DepthFog.fx`. The copy in `reshade/` in this
tree was updated the same way. ReShade reads the effect when the game starts.

Files: `vr/vrcam.cpp` (`EffectsPaused`, `MouseTick`, `SetUiMask`, `UiMaskSolid`),
`vr/hud.cpp` (the layer copy and `UiMaskNow`), `reshade/D2R_DepthFog.fx`.

## 4. Item names behind the player

The game places a ground item's name with the same camera it uses to draw the world. In the
original view that camera looks down, so a point on the ground is in front of the lens. In
flat first person and third person the lens can face away from an item. The game still divides
by that depth, and a negative depth lands the name on the screen in front of the player.

`HookLabelLayout` returns "not shown" unless the item's ground point projects inside the picture.
A point beside the lens used to be clamped onto the screen by the game, so with Alt held the
name climbed over the hero while you walked past. Behind the lens the same divide flips the
name into the view. The test uses the centre-eye view and the projection scales. It runs only
while our camera is on, so the original view is unchanged. A read that fails leaves the name up.

On a monitor the label-size hooks never installed (those run for the VR floor's own size), so
this layout hook is installed on its own for flat first and third person.

The ground point is placed with the renderer's elevation shear: a tile is a grid point, and
ground at height h is drawn shifted by h * sqrt(3/2) along both horizontal axes. Without it the
test was only right at height 0; on lowered or raised ground (stairs, ledges, act 2 lower
levels) names of items that were really on screen were dropped from some viewing directions.
Verified in game on lowered ground: names show from every side with Alt held, as on level 0.

File: `vr/vrcam.cpp` (`ItemLabelOnScreen`, `InstallLabelHooks`).

## 5. Effects on the horizon

Shrine marks, spells and other effects drawn without depth leave the depth of whatever is
behind them. The fog pass uses that depth. On the horizon the ground is at the end of the
fog, so the mix becomes pure fog colour and the effect is erased. The further that
background is, the more of the effect goes. High in the sky there is no depth, and the
effect stays.

Two small blocks in `D2R_DepthFog.fx` put the light back, both keyed to how bright the pixel is
(a peak channel from 0.45 to 0.85, so dim ground, leaves and grass still take the fog):

- At the end of the ordinary fog mix (`DepthFogWorld`, after the cave-halo return):

```
const float light = smoothstep(0.45, 0.85, max(colour.r, max(colour.g, colour.b)));
return lerp(colour, fogColour, t) + colour * (t * light);
```

- In the branch for pixels over the empty depth buffer (the void), the fog amount `tf` taken from
  the ground a few pixels below is lowered for bright pixels, before the colour is added:

```
tf = lerp(tf, 0.0, smoothstep(0.45, 0.85, max(colour.r, max(colour.g, colour.b))));
```

Confirmed in play on 0.185 (2026-10-27): spells at the horizon keep their light; with only the first
block part of them was still cut. Very bright foam on far water can come back on that strip.

`[fog] spare_effects=1` is not needed for this and costs a lot in flat mode: in the Act 2 lowered
level (where the ground patches of section 10 are) it made the game lag badly every time it was
on, and the lag was gone with it off (on, off and on again in the same place). Turning it on needs a
game restart; turning it off works live. Tested with the 0.171-based plugin on the 0.185 effect.

Shader only, no DLL change. ReShade reads the effect at game start. The exact edits against the 0.185
effect, with before and after, are in `SHADER_CHANGES.md`.

## 6. A portal's black centre

Confirmed working in play (2026-10-19 14:03). The stationary opaque black oval is removed and
the animated blue portal ring, ground glow, clickbox, and label remain intact.

### Why the black oval separates from the ring when turning the camera

In flat first-person (`F3`) and third-person (`F2`), turning the camera leaves the black oval
where it was placed (fixed 45° isometric `rotationOverride` on the portal object), while the
animated blue ring shifts to a new position facing the camera (and the previous ring particles
fade out over ~0.5 s). Decoding the binary `CLayerCompileCacheRendererProperty` (`0x1120`)
chunks across all 12 mesh renderers in
`data/hd/vfx/particles/objects/vfx_only/town_portal/vfx_town_portal_newstuff.particles` (and
`vfx_town_portal.particles`) showed why:

- **Renderer #1 (`portalBack`, the opaque black oval)**:
  - `General.Mesh`: `data/hd/vfx/meshes/fx_mesh_portalSphere_01.model` (and in
    `vfx_town_portal.particles`: `fx_mesh_portalSphere.model` and
    `fx_mesh_portalSphere_convex.model`). All three are **solid sphere/oval meshes without a
    hole** (`380` triangles, `230` vertices, `1140` 16-bit indices, stride `20`).
  - `General.Material`: `data/hd/vfx/materials/PopcornFX/Materials/Diablo_Smoke_Mesh.pkma`
  - `Transparency.Type`: `int(4)` (**Opaque**)
  - `Blend.TextureBase`: `data/hd/vfx/textures/default/White.texture` (tinted solid black).
- **Renderers #2, #5, #7, #8, #9, #10, #11 (`Waves OUTRO`, `Portal Energy Glow Card`, sustain
  wavelets, `New Wavelets`, `Overlapping Wavelets` — the animated blue ring)**:
  - `General.Mesh`: the four `long_hole` models:
    - `fx_mesh_portalSphere_convex_long_hole_vertexalpha_smaller_seamfix.model` (`352` tris, `207` verts)
    - `fx_mesh_portalSphere_convex_long_hole_vertexalpha.model` (`361` tris, `211` verts)
    - `fx_mesh_portalSphere_convex_long_hole_vertexalpha_smaller_wider_seamfix_4.model` (`352` tris, `207` verts)
    - `fx_mesh_portalSphere_convex_long_hole_vertexalpha_smaller_redux_2.model` (`360` tris, `210` verts)
  - Despite the `long_hole` substring in their filename, these four models are **not** the black
    hole — they are sphere meshes with the central ~20–28 triangles cut out (leaving a "long
    hole" in the middle) so only the outer ring geometry remains. They use
    `Diablo_Uber_Mesh.pkma` with blue portal gradients (`T_FX_portal_gradient_03.texture`,
    `T_FX_Portal_Gradient02.texture`, `T_FX_towngradient.texture`) and orient via
    `GeometryBillboard` (`scene.orientation`) at particle spawn.
- **Renderers #3, #4, #6, #12 (`portalBack END`, `Russels Waves ENd`)**:
  - Use `fx_mesh_townportal_lip_crawl_v4.model` and `fx_mesh_portalsphere_innerflow_3.model`,
    which only play during the portal closing animation.

### What was changed (the working fix)

Controlled by `[input] portal_face=1` (default on in flat F2/F3 only — not isometric F1).
Hiding toggles `D2RRenderMesh` counts and remembers them so F1 can restore the oval; Granny
vertex buffers are no longer wiped (that made F1 stay empty after a visit to F2/F3).

1. **Hook installation in flat mode (`vr/vrcam.cpp`, `vr/roof_sides.h`, `vr/roof_sides.cpp`)**:
   - `roofsides::Install` is now also installed in flat mode when `[input] portal_face` is
     non-zero (even if `[render] roofs_two_sided` is off).
   - `roofsides::WatchModels(&portalhole::OnModel)` invokes `portalhole::OnModel` at the start
     of `HookBuild` (`RoofMaterialBuild`, `RVA 0xF99C40`), passing `(model, file, mesh, _AddressOfReturnAddress())`.
2. **Filtering only the solid black oval models (`vr/portal_hole.cpp`, `IsHolePath`)**:
   - `IsHolePath` matches `portalsphere` while explicitly excluding `long_hole` (the 4 animated
     blue ring meshes), `innerflow`, and `lip_crawl`:
     ```cpp
     if (has("innerflow") || has("lip_crawl") || has("long_hole")) return false;
     return has("portalsphere");
     ```
   - This selects exclusively the three solid 380-triangle oval backdrop models:
     - `data/hd/vfx/meshes/fx_mesh_portalSphere_01_lod0.model`
     - `data/hd/vfx/meshes/fx_mesh_portalSphere_lod0.model`
     - `data/hd/vfx/meshes/fx_mesh_portalSphere_convex_lod0.model`
3. **Reading `GrannyMesh` and zeroing runtime `D2RRenderMesh` counts (`vr/portal_hole.cpp`)**:
   - `TryCollapse` reads the `GrannyMesh` struct at `r8` (`mesh`): dereferences `const char* Name`
     at `+0x00`, reads `GrannyVertexData*` at `+0x08` (`vertexCount = 230`, `stride = 20`), and
     reads `GrannyTriTopology*` at `+0x1C` (`groupTriCount = 380`, `index16Count = 1140`),
     zeroing the CPU vertex/index buffers and `triCount`.
   - `FindRenderMeshOnStack` locates the caller's `D2RRenderMesh*` (`rm`, held in `r12` in the
     caller `0xF9E820..0xFA19E8`) by scanning the 12 stack slots below `HookBuild`'s return
     address (`_AddressOfReturnAddress()`) for a pair of saved registers `(r12, rbx)` where
     `rbx == r12 + 0x278` (the caller executes `lea rbx, [r12+278h]` before the material loop).
   - `CollapseRenderMesh` scans the non-pointer fields of `D2RRenderMesh` (`rm + 0x04 .. 0x278`)
     and zeros the fields matching the mesh's triangle, vertex, or index counts. On all three
     solid portal sphere models in D2R 3.3.0 (93847), this zeroes three fields (`rmZ=3/0`):
     - `rm + 0x158` (`uint32_t` triangle count = `380` -> `0`)
     - `rm + 0x1CC` (`uint32_t` vertex count = `230` -> `0`)
     - `rm + 0x21C` (`uint32_t` submesh triangle count = `380` -> `0`)
   - `HookBuild` still runs `g_origBuild` and returns a valid material pointer, so the game
     never sees a null material, while the solid black oval mesh submits 0 triangles/vertices.

Files: `vr/portal_hole.h`, `vr/portal_hole.cpp`, `vr/roof_sides.h`, `vr/roof_sides.cpp`,
`vr/vrcam.cpp`, `CMakeLists.txt`, `d2r_vr.ini`.

## 7. Sky stuck on act 1 panorama

ReShade's addon API could not find the `SkyTex` uniform on the live effect
(`SkyTex not found … value not set`) while `GameBarOn` still worked, so the slot stayed
on act 1's panorama. Fix: rename the uniform to `VrSkyAct` in `D2R_DepthFog.fx`, give
each act its own default `#define` paths (the old effect pointed ACT2–SNOW at act1.png),
and fall back to enumerating uniforms if `find` misses.

## 8. NPC dialogue aims the look by the NPC's face (flat F3)

When `NpcDialogPanel` opens in flat first person and the player was looking down, the
options sit on the NPC's face. `[input] npc_look_horizon=1` (default on) sets the mouse
pitch once, at the moment the panel opens, so that the NPC's face lands at a chosen height of
the screen (`[input] npc_face_y`, default `0.30` = upper third, fraction of the screen height
from the top). `0` for `npc_look_horizon` leaves the pitch alone. F1 / F2 unchanged.

How the pitch is found:

1. **Who the dialogue is with.** `HookMapClick` remembers the unit the game found under the
   pointer at the click (unit type 1, monster / NPC; `HoverUnit` is called on the mouse thread,
   never on the present thread — that crashed on F3). When the panel opens, that unit is used
   if the click was less than 30 s ago and its memory still reads as a live monster. Only
   otherwise the nearest monster within 14 units is taken, so a mercenary or a minion standing
   nearer no longer takes the dialogue. The log says `clicked` or `nearest`.
2. **Where the face is.** MonStats height × 0.90 above the hero's floor, at the NPC's grid place
   (the hero's grid place is `look-at - L.y * k`, section 9). The distance is taken along the
   camera's horizontal heading, so an NPC standing to the side does not change the picture's
   height.
3. **The pitch.** The face is `a = atan((eyeY - faceY) / depth)` below the horizon. A view axis
   pitched `p` down puts it at `ndc = tan(p - a) / tan(fov/2)` (+1 = top), so
   `p = a + atan(ndc · tan(fov/2))` with `ndc = 1 - 2 · npc_face_y`. The vertical field of view
   is the live projection's (`1 / M[5]`), so it holds for any `[camera] fov` and window shape.
   The result is clamped to ±40°.

Unknown pose still looks 6° down. Confirmed in play for most town NPCs (2026-10-26).

Known limits: the NPC's floor is taken as the hero's (an NPC on another level, or a floating one
such as Tyrael, has its face higher or lower than computed). The position of the options panel
itself is the game's: it follows the game's own (isometric) projection of the NPC, so it can stand
left of, over, or right of the NPC depending on where the hero stands, and sometimes covers the
eyes; the dialogue text box is fixed in the middle. Neither is changed here.

Files: `vr/gamestate.cpp`, `vr/flat_pick.cpp`, `vr/flat_pick.h`, `vr/vrcam.cpp`, `d2r_vr.ini`.

## 9. Picking, casting and aiming on raised ground (stairs, platforms, caves)

On ground above height 0 (stairs, a platform, a cave floor: the hero's floor was 33.7 world
units in the Act 2 palace and the Act 4 fortress where this was measured) flat first and third
person could not select a chest, a door or a monster, and a directional or area spell did not
land under the crosshair. Two causes, both measured with `[debug] flat_elev=1`:

1. **The camera's look-at is the hero's RENDER point, not his grid place.** The renderer shears
   raised ground along both horizontal axes: `render = grid + height * k, k = sqrt(3/2) = 1.2247`.
   The log line `look-at (10091.60, 33.73, 10131.44), hero path (10050.26, 10090.10), r = (+41.33,
   +41.33), L.y*k = +41.31` shows `look-at - path place = L.y * k` on both axes, to 0.02. The
   body-pick code treated the two as one frame (v15's reasoning, "LookAt and Place share one frame",
   was only ever tested on flat ground, where the shear is 0). Its capsules therefore stood about
   `L.y * k` (41 units here) beside the units in the picture, the 55-unit reach and the
   nearest-first order were measured from the wrong point, and the NPC dialogue's look-down angle
   (`npc_look_horizon`) used the same mix. Now the hero's grid place is `look-at - L.y * k`, and a
   capsule is tested at `grid + y * k`. In the log the stash's base then projects 32 px from the
   crosshair when the crosshair is on it, and `crosshair-axis floor h` is within 0.4 of the hero's
   floor.
2. **The ray handed to the game.** The game's mouse code (one caller, RVA `0x7E7F60`, always at
   the middle of the screen in the mouse look) meets the ray with its ground and reads a grid
   place. It was built for the old isometric camera, whose every ray runs along `-(k, 1, k)`; a
   perspective ray from the eye aimed at a grid point on the hero's floor lands beside it by
   `height * k`. `[input] flat_iso_ray=1` (default on) hands it the isometric ray instead: through
   the picked body's or object's feet as drawn, and, with nothing picked, through the point where
   the crosshair's ray meets the hero's floor. Every point of that line has the same grid place,
   so the game's ground test lands where the picture is. Confirmed in play: objects are selected
   at any height, directional spells go where the crosshair looks, and area spells are placed
   under it.

`[debug] flat_elev=1` (off by default, live, read only) writes three lines every 2 s: the look-at
against the hero's path place with a verdict (grid or render), which of the game's codes asked
for rays and how many at the middle, and what the aim did; then, for the two nearest bodies and
objects, the pixel of their feet in both frames, their top, and the floor height the
crosshair's ray implies. That last number is meaningless when the camera looks along the
isometric diagonal (about 30 degrees down at 45 degrees of heading): the ray is then nearly
parallel to the axis it is measured against.

`d2rcam::RayAdjustFn` now also receives the pixel, the viewport and the caller's RVA, and
`d2rcam::WorldToScreen` / `ViewportNow` exist for the log.

Files: `vr/flat_pick.h`, `vr/flat_pick.cpp`, `cleanroom/camera/d2rcam.h`,
`cleanroom/camera/d2rcam.cpp`, `vr/vrcam.cpp` (ini keys, NPC dialogue angle), `d2r_vr.ini`.

### Floating units (Tyrael, Act 4 Pandemonium Fortress)

Aiming the isometric ray at a body's feet selected every NPC and monster except Tyrael, who is only
highlighted with the crosshair a little to one side of him and at his feet. The game's hover was read
without clicking (a click on an NPC walks the hero up to him): `[debug] flat_elev` now asks the game's own
`HoverUnit` for the local player at every ray the mouse code asks for, and logs what it found next to the
grid point the previous ray stood for. For Tyrael (class 367, path place `(10041.0, 10041.0)`):

- the ray through his path place: no hover (4 of 4);
- hover for grid points from `(-0.3, -2.3)` to about `(-7.6, -11.5)` away from his path place, along the
  `(-1, -1)` diagonal; none at `(+6.9, -3.2)` or `(-11.6, -17.3)`.

Other NPCs (classes 405 and 246) have the hover exactly at their path place. The game's hover is built for
the isometric picture, where a pixel of a unit's body stands for the ground point `grid - height * k` on both
axes, so a model that floats above the ground has its hover region moved back along that diagonal and the
feet are outside it. Fix: for a body (not an object) the isometric ray goes through the point of its axis at
the height the crosshair meets it, kept between 25 and 60 % of its height, instead of through its feet. Objects
stay on the feet (chests, doors and the stash were confirmed there). Confirmed in play: Tyrael is highlighted
with the crosshair on his body, and other NPCs and monsters are still selected.

Files: `vr/flat_pick.cpp` (`Adjust`), `vr/vrcam.cpp` (`FlatHoverProbe`), `vr/flat_pick.h`.

## 10. Ground patches sliding with the camera on raised / lowered ground (2026-10-27)

In flat F2 / F3, on ground below or above level 0 (the act 2 Far Oasis stairs between rock shelves were
the test place), dark mud patches with cracked earth and bumpy "stones" slid over the ground while the
camera turned: looking up moved them away, looking down moved them closer, turning left moved them to the
right. They stood still while the camera stood still and when the hero walked. The stones were visible
only inside the patches. F1 was fine.

What the patches are: not lighting, shadows or decals drawn over the picture. They are material layers of
the ground's own colour / normal map (a 3888 × 3888 toroidal texture read by the terrain pixel shader
`a11d8d1c6efd3f33` as t35 / t36). A compute shader (`775c45982728780a`) composes that map one 10-unit tile
per dispatch, and only while the camera moves. The CPU gives it each layer as a world -> layer matrix in b7
(rows: x, `(0, 1, 0, 0)`, z, translation with `w = 1`; row 0 `.xy` is the tile, row 24 `.y` its texels a
side, 216). The "stones" are the normal map of such a layer.

Measured (556 camera poses in F2 matched to the same layers in F1, `[debug] layer_log`): every layer of a
frame is shifted by the same vector

    offset = h * (k - 4 * fwd.y * fwd.xz)          (x and z apart)

with `h` the ground's height (here the hero's ground, `LookAt.y = -22.97`), `fwd` the camera's view
direction as the same dispatch gets it (b10 row 2), and `k = 1.224745` the isometric shear the terrain
geometry uses. z axis: `-28.02 + 92.48 * fy * fz`, rms 1.0 unit (`h * k = -28.13`). The game derives the
layers' shear from the camera direction and assumes its own isometric camera: for `fwd = (-0.612, -0.5,
-0.612)` the bracket is exactly 0. With a free camera it is not, so on any ground with `h != 0` the layers
move with every turn of the view. On level 0 nothing moves, which is why the patches were seen only on
stairs, ledges and lowered areas.

Fix: `[render] ground_layer_fix=1` (default on). When the game binds a compute root constant buffer
(camfix's existing `SetComputeRootConstantBufferView` hook, the one that already writes the depth range),
the register is looked up through the root signature: b10 is remembered for the list, and a b7 that has the
ground map's shape gets every layer's translation moved back by `-offset` (`tu += a * ox + b * oz`, the same
for `tv`), in the game's upload memory, before the GPU reads it. A buffer already put right (hash of its
contents) is left alone. Only while our camera is on and `h` is not 0. In play the patches stay put while
the camera turns; logged after the fix, the layers lie where the game's camera has them within 0.1 unit.

Known limit: `h` is the ground under the hero. A layer on another level than the hero's (a ledge seen from
below) is moved by the hero's height, not its own. The clean fix would give the game's layer code the
isometric direction, but that code does not go through the camera functions we hook (`GetProjection` /
`RebuildView` callers were logged: the same seven with the camera still and turning).

Files: `vr/vrcam.cpp` (`groundfix`, `camfix::GroundLayers`, `VrFrame` height, ini key), `vr/uitrace.cpp`
(`CbRegister`; investigation tools: `ui_trace_srv`, `layer_log`, per-pass copies, compute constants),
`cleanroom/camera/d2rcam.{h,cpp}` (diagnostics: `keep_game_pose`, `cull_scale`, `cull_fov`,
`cam_fields_log`, `cam_callers_log`), `d2r_vr.ini`. Investigation notes and the ruled-out list:
`PRZEKAZANIE.md`; diagnostic keys: `INI_ADDED.md`.
