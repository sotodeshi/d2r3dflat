# Extra `d2r_vr.ini` controls (flat monitor work on 0.171)

Keys and value changes added on top of stock D2R VR `51a014c` / `0.171.0`
for `[mode] platform=0` (monitor). Live file: `d2r_vr.ini` (copy under
`d2rloader\plugins` when you play).

## New keys (not in upstream ini)

| Section | Key | Default here | Role |
|---------|-----|--------------|------|
| `[input]` | `flat_body_pick` | `1` | F3: cursor on body/object capsule selects that unit (ray aimed at feet). |
| `[input]` | `flat_body_pick_third` | `1` | Same aim in F2. |
| `[input]` | `portal_face` | `1` | F2/F3: hide portal black centre mesh (`0` game / `1` collapse / `2` empty name). Not F1. |
| `[input]` | `portal_axis` | `0` | Test only: effects born from fixed 45° camera quat. Leave `0` (did nothing useful 2026-10-19). |
| `[input]` | `npc_look_horizon` | `1` | F3: on NPC dialogue, set look pitch from nearest MonStats height (+6°). |
| `[input]` | `flat_iso_ray` | `1` | Confirmed 2026-10-26: game's ground test gets the isometric ray `-(k,1,k)` through the body's feet / the crosshair's floor point (elevation fix, stage 1). Needs `flat_body_pick(_third)`. |
| `[debug]` | `flat_elev` | `0` | Log every 2 s: look-at vs hero path place (grid or render frame?), ray callers (RVAs), adjust counters, projected capsules; plus the game's own hover (read without clicking) against the grid point of the last ray, on every change. Read only. |

## Value changes (key already existed upstream)

| Section | Key | Upstream | Here | Note |
|---------|-----|----------|------|------|
| `[fog]` | `spare_effects` | `0` | `1` | Also on monitor; spare transparent layers so horizon FX survive fog. |
| `[hud]` | `bar_size` | `74` | `100` | Local preference, not a new control. |

## Flat keys already in upstream (listed for completeness)

These were already in stock `d2r_vr.ini`; this build relies on them for monitor play:

| Section | Key | Typical |
|---------|-----|---------|
| `[mode]` | `platform` | `0` monitor |
| `[mode]` | `flat_view` | `3` = F3 start |
| `[input]` | `flat_keyboard_move` | `1` |
| `[input]` | `flat_no_pad` | `1` |
| `[input]` | `flat_click_shoot` | `1` |
| `[input]` | `flat_crosshair` | `1` |
| `[input]` | `crosshair` | path / empty |
| `[input]` | `mouse_look` / `mouse_speed` | F9 / F3 |
| `[flatvr]` | `frame_sync`, `head_level`, … | VR-oriented; usually `0` on monitor |

No new ini key for: sky `VrSkyAct` (effect + DLL), `UiMaskSolid` (effect + DLL),
Y-slack on capsules (removed), portal ring shader cover (abandoned).

> **Status 2026-10-26: rozwiązane dla zaznaczania i rzucania** (CHANGES.md pkt 8). Zmierzone: LookAt jest
> punktem renderu (`L − Place = L.y·k`); kapsuły w `grid + y·k`; promień dla gry idzie wzdłuż `−(k,1,k)`
> (`flat_iso_ray`). Poniżej pierwotna analiza; zdanie, że „absolutny shear w flat_pick był zły”, jest nieaktualne.

## Elevation: horizontal shear (stairs / raised pads)

Likely one cause for both:

1. walking up stairs looks diagonal / “under an angle”, and
2. NPCs / interactables on a raised floor look shifted in XZ, not only in Y.

D2R’s renderer shears elevated ground on **both horizontal axes**:

`renderXZ = gridXZ + height * √(3/2)`   (`kGroundShear` ≈ 1.225 in `vr/melee_bodies.h`).

- Logic / path / `flat_pick` use **grid** XZ (and often the hero’s ground Y).
- Drawn meshes on a pad or stair sit at the **sheared** place.
- Height change while climbing stairs changes that horizontal offset every step → motion and facing can look skewed; aim/pick that ignores shear misses the mesh sideways.

`[input] portal_axis` is unrelated (off).

### Compensation (yes — calculable)

Already in `vr/melee_bodies.h`:

```
kGroundShear = √(3/2) ≈ 1.2247
renderXZ = gridXZ + floorY * kGroundShear   // RenderGround
gridXZ   = renderXZ - floorY * kGroundShear // GridPlane
```

Relative form (same units as hero / `flat_pick`):

```
dY = unitFloorY - heroFloorY
pickXZ = unitGridXZ + dY * kGroundShear
```

| Goal | How | Needs |
|------|-----|--------|
| **Select** elevated NPC / object | Hit-test capsule at `pickXZ` + `unitFloorY`; on hit, `AimFeet` still at **grid** feet (game path test). | Per-unit (or per-tile) floor Y. Path XZ alone is not enough — D2 path has no elevation; height comes from the collision / render height at that tile. |
| **Walk** on stairs looking straight | Optional: counter-steer stick by `−kGroundShear * dY` expected for the step so sheared motion stays on the look line. | Slope / next-step dY (harder). Climbing can look diagonal even when grid walk is “straight W”, because each +Y also slides +X and +Z in the picture. |

Same-level ground: absolute shear still applied (F4 style) so look-at render space and
path grid match. Pads with a **different** floor than the hero still need per-unit Y later.

**v15 tried absolute shear in `flat_pick` — wrong:** camera LookAt and path Place are
already the same frame (unlike F4 skeleton pose). Capsules missed; no hover/labels.
**v16:** reverted. Relative `dY * k` only when we have a real per-unit floor delta.
