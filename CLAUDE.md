# d2r3dflat — zasady projektu

1. Projekt jest moją wersją (forkiem) oryginalnego repozytorium
   [natpoh/diablo2-vr-port](https://github.com/natpoh/diablo2-vr-port), skupioną na poprawkach
   dla trybu „Flat” (3D Flat mode: gra na monitorze, `[mode] platform=0`, widoki F2/F3).
2. Plik `CHANGES.md` służy do dokumentowania zmian: dodajemy tam wyłącznie zweryfikowane
   poprawki, które faktycznie ulepszają kod w stosunku do oryginału. Plik jest po angielsku
   (idzie do autorów moda).
3. Commity: tylko na polecenie „commit”. Wtedy nowy commit opisujący w tytule/treści
   wprowadzone poprawki. Bez polecenia — żadnych commitów.
4. Zmiany wprowadzamy tylko dla wersji flat (F1–F3, `[mode] platform=0`). Nie ruszamy
   ścieżek VR/F4 — nowy kod musi być bramkowany na flat i nie zmieniać zachowania w VR.

Dodatkowo (z `PRZEKAZANIE.md`, które jest pełnym przekazaniem kontekstu — czytaj je):

- Odpowiadamy po polsku.
- Testowanie (zasady od 2026-10-26, zastępują wcześniejszy zakaz podmiany DLL): po każdej zmianie
  i udanym buildzie od razu nadpisujemy `E:\battlenet\d2r\d2rloader\plugins\d2rl-vrcam.dll`
  oraz `...\plugins\d2r_vr.ini` (z włączonymi opcjami, które testujemy). Jeśli gra działa i
  DLL jest zablokowana — powiedz to i nie obchodź blokady. W `plugins` nie zostawiamy żadnych kopii.
  - „kopia” = skopiuj żywe `d2rl-vrcam.dll` + `d2r_vr.ini` z `plugins` do nowego folderu
    `E:\AndroidStudioProjects\d2vrflat_backup\RRRR-MM-DD_GG-MM-SS\`.
  - „przywróć z kopii” = przywróć DLL i ini z najnowszego folderu w `d2vrflat_backup`.
  - „przywróć z git” = cofnij źródła do ostatniego commita (stan przed zmianami); potem rebuild i
    wdrożenie jak wyżej.
- Nie ruszamy plików gry (CASC, `mods\`, siatki, `.particles`, `default_mod`).
- Build: `cmake --build build --config Release --target d2rl-vrcam` (CMake z VS 18,
  pełna ścieżka w `PRZEKAZANIE.md`). Nie konfiguruj `build` od zera.
- W `vr/flat_pick.cpp` nie nazywaj zmiennej `far` (makro z `windows.h`).
- Remote: `origin` = `sotodeshi/d2r3dflat`, `upstream` = `natpoh/diablo2-vr-port`.
  Lokalna historia jest płytka (jeden commit importu); stara historia w branchu `backup-history`.

---

# Architektura

Jedna DLL `d2rl-vrcam.dll` (wtyczka D2RLoadera, id `d2r-vr-vrcam`) hookuje `D2R.exe`.
Adresy (RVA) są dla D2R 3.3.93787; gracz ma 3.3.0 (93847), sygnatury nadal pasują.

| Katalog | Co |
| --- | --- |
| `cleanroom/camera/` | Warstwa kamery: przejęcie kamery gry, macierze, `ScreenToRay`. Czysta, mała, najważniejsza dla geometrii. |
| `cleanroom/sigscan/` | Sygnatury i RVA funkcji gry (`game_sigs.h`), skanowanie i hooki. |
| `cleanroom/drawdist/` | Zasięg rysowania (pierścienie pokoi D2Prism). |
| `vr/vrcam.cpp` | ~17,7 tys. linii: cała logika moda — widoki F1–F4, mysz, ini, HUD, hooki gry. |
| `vr/*.h` | Głównie czysta matematyka do testów (`cleanroom/tests`, `tools/*_test.cpp`). Pliki `*_wire.cpp` to okablowanie z grą. |
| `shared/` | Struktury wspólne z BodyWalk/FlatVR/ustawieniami. |
| `reshade/D2R_DepthFog.fx` | Efekt nieba/mgły (kopia tego, co gra uruchamia). |
| `tools/settings/` | Program ustawień `D2R_VR_Settings.exe`. |

Widoki (flat, `ApplyView` `vr/vrcam.cpp:11394`): F1 — kamera gry (`g_enabled=false`),
F2 — trzecia osoba (`ThirdPerson()` `vr/vrcam.cpp:2655`), F3 — pierwsza osoba
(`InsideFree()` `vr/vrcam.cpp:2676`). F4 (`FullBody()`) jest tylko dla VR i ma osobne ścieżki
(`vr/view_gate.h`: dodatki portowane działają tylko w F4).

## Układy współrzędnych (kluczowe dla wszystkiego poniżej)

- **Świat renderu**: Y w górę, prawoskrętny, macierze row-major, wektory wierszowe
  (`p' = p * M`), kamera patrzy w −Z, projekcje infinite-far reverse-Z
  (`cleanroom/camera/mat4.h`).
- **Siatka (grid) logiki gry**: ścieżka jednostki `unit+0x38` → `path`, współrzędne 16.16;
  `x = 2*(p[1] + p[0]/65536)`, `z = 2*(p[3] + p[2]/65536)` (jednostki świata = 2× komórka).
  `unitscan::Place` (`vr/unit_scan.h:56`), `plateworld::FootOf` (`vr/plate_world.h:85`).
  Obiekty statyczne: `path+0x10/+0x14` jako kafel → `2*tile+1` (`ObjectPlace`,
  `vr/flat_pick.cpp:136`). **Siatka nie ma wysokości.**
- **Shear izometryczny (elewacja)**: renderer przesuwa podniesiony grunt po obu osiach
  poziomych o `k = cot(30°)/√2 = √(3/2) ≈ 1.2247` na jednostkę wysokości:
  `render = (gx + h·k, h, gz + h·k)`. Kierunek `(k, 1, k)` to dokładnie kierunek patrzenia
  starej kamery izometrycznej (30° w dół po przekątnej), więc w F1 wysokość jest „niewidoczna”
  dla logiki 2D. Ściany/postacie stoją pionowo nad przesuniętym punktem podłogi (bearstonem
  d2r-pcvr: „only the floor's shear is removed”).
  - `vr/melee_bodies.h:53-59` — `kGroundShear`, `GridPlane(render)`, `RenderGround(grid, h)`.
  - `vr/melee_bodies.h:67` `BodyFor` — kapsuła potwora w świecie renderu (shear od podłogi bohatera).
  - `vr/melee_touch.cpp:888-889` — pozycja bohatera ze szkieletu (render) → `GridPlane`.
  - `vr/plate_world.h:99` `HeadOnAxis`, `:461` `MakeAnchor`, `:525` `LiftAnchor` — oś „pionowa”
    potwora liczona jako `(k,1,k)` (uwaga: inne założenie niż pionowa kapsuła w `BodyFor`).
  - Podłoga bohatera dla tabliczek: `plateworld::SetWorld` (`vr/vrcam.cpp:15218`, z `g_heroPosNow[1]`).
  - Notatki: `INI_ADDED.md` sekcja „Elevation”, `PRZEKAZANIE.md` pkt 12–13.
- **LookAt** kamery gry (`camf::LookAt = 0x11C`, `game_layout.h:20`): „punkt bohatera na
  ziemi”, od niego budowana jest nasza kamera. **Zmierzone 2026-10-24 (`[debug] flat_elev`): LookAt
  jest punktem RENDERU**, `L − Place bohatera = L.y·k` na obu osiach (41,33 vs 41,31 przy L.y = 33,73).
  Dawny komentarz w kodzie „LookAt i Place w jednym układzie” był błędny (widać tylko przy L.y ≠ 0).
  Siatka bohatera = `L − L.y·k`.

## Kamera i rzutowanie

- `cleanroom/camera/game_layout.h` — offsety obiektu `Camera` (`camf::View 0x10`,
  `InvView 0x50`, `Proj 0x90`, `Pos 0x110`, `Quat 0x12C`, `LookAt 0x11C`, `ViewportW/H 0x150/0x154`)
  i RVA: `CamRenderTick`, `RebuildView 0xED70A0`, `GetProjection`, `ScreenToRay 0xED62B0`,
  zakres `ShadowFitBegin/End`.
- `cleanroom/camera/d2rcam.cpp` — co klatkę po follow gry (`HookCamRenderTick` `:333` →
  `SyncFrame` `:186` → `Apply` `:244`): odczyt pozy gry, wywołanie callbacków widoku/projekcji,
  wpisanie naszego oka/kwaternionu i macierzy do kamery. `HookRebuildView` `:346` i
  `HookGetProjection` `:357` pilnują, by leniwe przebudowy nie nadpisały naszych macierzy.
- `cleanroom/camera/mat4.h` — `LookTo` `:74`, `PerspectiveRevZ` `:133`, `RayThrough` `:164`
  (piksel → promień przez `invView`/`proj`).
- `vr/vrcam.cpp`:
  - `VrView` / `VrViewInner` `:3928` / `:3940` — budowa naszego widoku. Gałąź flat F2/F3:
    `:4026-4069`. Oko: `pivot = (L.x, L.y + ViewHeight() + JumpLift(), L.z)` + przesunięcia
    `camSide/camForward`, model szyi, `eye = eyes − fwd·ViewDistance()`. `L = in.lookAt`.
    Zapisuje `g_lookAt`, `g_labelView` (dla etykiet), `g_eyeWorld`, `g_ptrMap.groundY = L[1]`.
  - `VrProj` / `VrProjInner` `:4201` / `:4210` — projekcja (`CameraFov()` `:3049`).
  - `TargetYaw` `:2570`, `TargetPitch` `:2608`, `ViewHeight` `:2787`, `ViewDistance` `:2779`,
    `JumpLift` `:2796`, `LookAtNow` `:3290`, `Facing` `:3301`.
  - `TopCamera` `:3448`, `TableCamera` `:3495` — widoki VR z góry/stół (nie flat).

## Mysz → świat gry (ScreenToWorld / Raycast)

Ścieżka kliknięcia w flat F2/F3 z mouse look:

1. `MouseTick` (`vr/vrcam.cpp:7452`) obraca kamerę (`g_mouseYaw/g_mousePitch`) i centruje kursor.
   `GameWndProc` (`:7222`) podmienia `lParam` każdej wiadomości myszy na środek okna, gdy
   `ShooterActive() && KeyMoveMode()` (`:6068`, `:6163`). Celownik = kursor (`HookSetCursor` `:7185`).
2. Gra woła `Camera::ScreenToRay` → `HookScreenToRay` (`cleanroom/camera/d2rcam.cpp:367`):
   - wywołania z zakresu shadow-fit dostają pozę gry,
   - `g_rayFn` (`SetRayOverride`, `VrRay` `vr/vrcam.cpp:4298`) — tylko pad / `[bow] mode=2`,
   - inaczej `m4::RayThrough(invView, proj, viewport, px)` z naszych macierzy,
   - na końcu `g_rayAdjust` = `flatpick::Adjust` (rejestrowane w `vr/vrcam.cpp:17546`).
3. `vr/flat_pick.cpp` — celowanie w ciało: `Refresh` (`:223`, co klatkę z `VrFrame`
   `vr/vrcam.cpp:3306-3316`) buduje kapsuły (48 ciał z hasha jednostek `kUnits 0x2A23910`,
   32 obiekty z `flat_pick_obj.inc`), wysokość z MonStats (`SizeOf` `:98`). **`c.y = heroY`**
   (wszystko na podłodze bohatera, brak shearu). `Adjust` — `RayCapsule`, przy trafieniu
   `AimFeet` kieruje promień w stopy `(gridX, heroY, gridZ)`, żeby test ziemi gry zaznaczył tę
   jednostkę. Kapsuły są testowane w miejscu rysowania (`grid + y·k`), zasięg liczony od pozycji
   bohatera na siatce (`L − L.y·k`). Z `[input] flat_iso_ray=1` zamiast tego `IsoRay`: promień wzdłuż `−(k,1,k)` przez
   stopy w układzie renderu (lub przez punkt podłogi pod celownikiem). `[debug] flat_elev=1` —
   `ElevLog`: LookAt kontra `Place` bohatera, RVA wywołujących `ScreenToRay`, liczniki.
   `RayAdjustFn` dostaje piksel, viewport i RVA wywołującego.
4. Gra przecina promień ze swoją ziemią (płaszczyzna/teren — **nie zdeasemblowane**) i ustawia
   hover. `HoverUnit = 0xF1900(0x9A820(unit))`, klik `HookMapClick` (`vr/vrcam.cpp:6356`,
   `RVA_MAP_CLICK 0xFE3B0`); przy pustym hoverze i `flat_click_shoot` — strzał w celownik.
- Inne konsumenty promienia/rzutu: `d2rcam::PickRay` (`d2rcam.cpp:516`), `vr/pointer3d.h:80`
  `GroundUnderPixel` (płaszczyzna `y = L.y`), etykiety przedmiotów `ItemLabelOnScreen`
  (`vr/vrcam.cpp:11126`), `HookLabelLayout` (`:11150`), `InstallLabelHooks` (`:11206`),
  tabliczki potworów `vr/mob_bars.cpp:146-154` + `vr/plate_world.h`.
- Dialog NPC: `flatpick::NearestMonster` / `MonsterPose` (`vr/flat_pick.cpp:344`, `:324`),
  użycie w `VrFrame` `vr/vrcam.cpp:3320-3340`.

## Elewacja — stan wiedzy (2026-10-24)

- Upstream (`natpoh/diablo2-vr-port`, main do `f746dec`, 2026-10-21) **nie rozwiązał** problemu
  wysokości dla flat. Dodał tylko diagnostykę `[debug] flat_pick` (branch
  `claude/flat-targeting`, `docs/notes/flat_targeting.md`), gdzie hipoteza nr 1 to niezgodność
  płaszczyzny ziemi gry z podłogą na obrazie, wzmacniana płaskim kątem patrzenia
  (`slip = dh / tan(down)`). Uwaga: upstreamowy `vr/flat_pick.h` ma inną treść niż nasz.
- bearstonem/d2r-pcvr (`steamvr/world_coordinates.h`) potwierdził shear `√(3/2)` pomiarem w
  jaskiniach; tam też usuwa się tylko shear podłogi.
- Nie robić globalnego „odshearowania” macierzą widoku: przesunięte są tylko pozycje na
  podłodze, geometria ścian stoi pionowo, więc ściany by się przechyliły.
