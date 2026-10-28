# Flat mode (monitor, `platform=0`) - short list of fixes

Base: D2R VR 0.171 (`51a014c`). Every item was tested in game. VR headset play is not changed on
purpose. Names like `[input] flat_body_pick` are keys in `d2r_vr.ini` (section `[input]`, key
`flat_body_pick`); all of them are on by default.

## 1. Clicking a body, a door, a shrine, a chest, a portal
**Problem:** in flat first / third person the game picks a unit where the view ray meets the ground,
so only the feet could be clicked. Aiming at the body or the chest did nothing.
**Fix:** the click ray is aimed through a short invisible capsule around the unit, so the body can be
clicked. Keys: `[input] flat_body_pick`, `flat_body_pick_third`.

## 2. Sky and fog switched off when a panel is open
**Problem:** opening a dialogue, inventory, stash or Esc turned the sky, fog and cave ceiling off,
although the world stays visible behind the panel.
**Fix:** the effects stay on on a monitor (VR is unchanged).

## 3. Panels half transparent
**Problem:** with the sky kept on, every panel looked washed out by the sky.
**Fix:** the effect now leaves the pixels the interface covers alone (`UiMaskSolid` in
`D2R_DepthFog.fx`). Needs the DLL and the effect file together.

## 4. View thrown to the sky after leaving a menu
**Problem:** the first mouse movement after closing a panel was applied as one huge turn.
**Fix:** that first sample is dropped and the pointer is put back in the middle.

## 5. Item names (Alt) behind the player, and on lowered ground
**Problem:** with Alt held, names of items beside or behind the camera were clamped onto the screen and
climbed over the hero. After the first fix, on ground below or above level 0 (ledges, stairs in act 2)
names disappeared when looked at from some sides.
**Fix:** a name is shown only if the item's ground point is inside the picture, and that point now
includes the elevation shear, so it works at any height.

## 6. Spells and effects erased on the horizon
**Problem:** shrine marks, spells and other effects drawn without depth vanished in the distance,
because the fog mixed them with pure fog colour.
**Fix:** a bright pixel keeps its light on top of the fog (shader change only, `D2R_DepthFog.fx`).

## 7. Town portal: black oval in the middle
**Problem:** the opaque black oval stood still while the blue ring turned with the camera.
**Fix:** the black oval mesh is not drawn in flat F2/F3; the animated ring stays. Key:
`[input] portal_face`.

## 8. Sky stuck on the act 1 panorama
**Problem:** the sky picture did not change with the act.
**Fix:** the effect uniform was renamed (`VrSkyAct`) so the plugin finds it, and each act has its own
default picture path.

## 9. NPC dialogue: options on the NPC's face (F3)
**Problem:** looking down at an NPC, the dialogue options sat on its face.
**Fix:** when the panel opens, the look pitch is set so the NPC's face is in the upper part of the
screen. Keys: `[input] npc_look_horizon`, `npc_face_y`.

## 10. Raised and lowered ground (stairs, platforms, caves; act 2 and act 4)
**Problem:** above or below level 0 nothing could be selected, and directional / area spells did not
land under the crosshair. Floating units (Tyrael) could not be selected at all.
**Fix:** the renderer shifts raised ground by `sqrt(3/2)` per unit of height; the pick, reach and
aim now take that into account, and the ray handed to the game follows the old isometric direction.
Floating units are aimed at by the body, not the feet. Key: `[input] flat_iso_ray`.

## 11. Ground patches sliding with the camera
**Problem:** on raised or lowered ground, dark mud patches with bumpy "stones" slid over the ground
while the camera turned (looking up moved them away, looking down moved them closer).
**Fix:** the game shears the ground's material layers by a factor taken from the camera direction,
which is only right for its isometric camera. The layers are corrected before the GPU reads them.
Key: `[render] ground_layer_fix`.

## 12. Walking into a portal enters it (F2 / F3)
**Problem:** a portal had to be clicked by hand; with W A S D in the mouse look that means stopping to aim at it.
**Fix:** near a portal, walking, the crosshair is aimed at its feet; when the game itself reports the portal under
the pointer, a real click (button down, then up) is sent. Works both ways, does not bounce back through the portal
you arrived by, and the hero walks on after the teleport. Keys: `[input] portal_auto` (default off),
`portal_auto_radius`, `portal_auto_delay_ms`.

## 13. Level look after a portal or waypoint (F3)
**Problem:** a way portal lies on the ground, so you look down at it, and the hero came out of the teleport still looking down.
**Fix:** when the hero arrives (his place jumps by over 30 units after the game sets a biome), the pitch of the mouse look returns to
the horizon; the heading is kept. Walking inside a town or across a biome border does not touch it, and neither does the NPC dialogue
look. Key: `[input] portal_look_horizon` (default on).

---

How each fix works (techniques, measured values, game addresses, files touched) is described in
`CHANGES.md`, in the section with the same topic.
