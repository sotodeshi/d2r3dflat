# Przekazanie projektu d2vrflat

Lokalna kopia [natpoh/diablo2-vr-port](https://github.com/natpoh/diablo2-vr-port), commit `51a014c`
(„D2R VR (from 36d0392)”, 2026-10-19). Wersja w `vr/vrcam.cpp`: `g_info_version` = `0.171.0`.
Gramy w Diablo II Resurrected na monitorze, bez gogli. Poprawki są opisane po angielsku w
`CHANGES.md` — ten plik idzie do autorów moda.

Użytkownik pisze po polsku i oczekuje odpowiedzi po polsku. Nie rób commita, dopóki o niego nie poprosi.
Nie podmieniaj biblioteki w folderze gry, dopóki nie powie, że gra jest zamknięta. Po zbudowaniu
podaj ścieżkę do DLL i poczekaj, aż sam ją wstawi.

## Gdzie co leży

| Co | Ścieżka |
| --- | --- |
| Źródła | `E:\AndroidStudioProjects\d2vrflat` |
| Biblioteka po buildzie | `E:\AndroidStudioProjects\d2vrflat\build\Release\d2rl-vrcam.dll` |
| Biblioteka w grze | `E:\battlenet\d2r\d2rloader\plugins\d2rl-vrcam.dll` |
| Żywy ini gracza | `E:\battlenet\d2r\d2rloader\plugins\d2r_vr.ini` |
| Ini w repo | `d2r_vr.ini` (osobny plik, gra go nie czyta) |
| Efekt nieba i mgły w grze | `E:\battlenet\d2r\reshade-shaders\Shaders\D2R_DepthFog.fx` |
| Ta sama poprawka w repo | `reshade\D2R_DepthFog.fx` |
| Log loadera | `E:\battlenet\d2r\d2rloader\logs\d2rloader.log` |
| Log moda | `E:\battlenet\d2r\d2rloader\logs\d2r-vr-vrcam.log` |
| Start | `E:\battlenet\d2r\d2rloader\logs\d2r_vr_start.txt` |
| Stara oficjalna DLL 0.169 | `E:\AndroidStudioProjects\d2vrflat\old-plugin\d2rl-vrcam-original.dll` |

D2RLoader ładuje każdą DLL z `d2rloader\plugins`. Dwie kopie z id `d2r-vr-vrcam` dają okno
„SOME EXTENSIONS FAILED TO LOAD”. Zapasowa kopia nie może zostać w tym folderze. Nazwa z
` - Copy` też jest ładowana.

Od 0.170 oficjalny instalator przenosi wtyczkę do
`E:\battlenet\d2r\mods\D2R-VR\d2rloader\plugins\d2rl-vrcam.dll` i kasuje kopię globalną.
Aktualizacja z programu ustawień (`E:\battlenet\d2r\D2R_VR_Settings.exe`, nadal 0.169.0)
podmieniłaby naszą bibliotekę na oficjalną, bez tych poprawek. Po takiej aktualizacji
wstawiać DLL w nowym folderze moda, nie z powrotem do `d2rloader\plugins`.

Gra na tym komputerze to D2R 3.3.0 (93847). Mod jest opisany jako zrobiony pod 3.3.93787.
Sygnatury i tak się złapały. Adresy w kodzie (np. `RVA` w `vr/unit_scan.h` i `vr/flat_pick.cpp`)
są z tego układu.

## Jak zbudować

CMake nie jest na PATH. Użyte polecenie:

```
& "E:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "E:\AndroidStudioProjects\d2vrflat\build" --config Release --target d2rl-vrcam
```

Generator: Visual Studio 18 2026, Windows SDK 10.0.26100.0. `D2RVR_FIRST_PERSON` jest włączone.
Katalog `build` powstał, gdy projekt leżał na pulpicie (`C:\Users\Sajmon\Desktop\d2vrflat`).
Ścieżki w cache zostały przepisane na `E:\AndroidStudioProjects\d2vrflat`. Gdyby CMake znowu
szukał pulpitu, nie konfiguruj od zera, jeśli da się poprawić ścieżkę — `build\_deps` ma już
OpenXR, D2RL PluginSDK i casclib.

Publiczne `CMakeLists.txt` woła testy, których nie ma w opublikowanym drzewie. Jest na to
opakowanie `d2rvr_add_executable`: brakujący plik źródłowy dostaje pusty cel poza domyślnym
buildem. Prawdziwe cele, w tym `d2rl-vrcam`, dalej idą przez `add_library` / `add_executable`.

`external\bodywalk\game_link.h` w publicznym drzewie nie miał struktur, których używa wtyczka
(`BW_LinkWalk`, `BW_LinkVrRune`, `BW_LINK_TRACK_CONTROLS`). Są dopisane jako układ tylko po to,
żeby DLL się skompilowała. Celowanie na płasko ich nie używa. Zły numer wersji bloku oznacza,
że rysowanie run w VR i blok Walk nie są czytane.

W `vr/flat_pick.cpp` nie nazywaj zmiennej `far`. `windows.h` robi z tego makro.

## Widoki na monitorze

`[mode] platform=0`. Klawisze F1..F3 (`ApplyView` w `vr/vrcam.cpp`):

- F1 — własna kamera gry, wtyczka wyłączona (`g_enabled` = false). To „tryb oryginalny”.
- F2 — trzecia osoba. `g_view` = 2, `ThirdPerson()` jest prawdziwe.
- F3 — pierwsza osoba na monitorze. `InsideFree()` jest prawdziwe, `FullBody()` nie:
  `FullBody()` jest tylko dla VR F4.

F9 przełącza sterowanie myszą. W F3 jest włączone samo. `[input] flat_body_pick` domyślnie 1.
Żywy ini przeładowuje część kluczy przy zapisie (timer co około pół sekundy), ale nie każdy.

## Co już jest zrobione

Szczegóły i powód są w `CHANGES.md`. Krótko:

1. Celowanie w ciało NPC i potworów oraz w drzwi, kapliczki, skrzynie, studnie, schowek,
   waypointy, portale i schody. `vr/flat_pick.cpp`. Promień kamery, który trafi w kapsułę,
   jest kierowany w stopy, a gra zaznacza jednostkę swoim testem ziemi. F3 włącza
   `[input] flat_body_pick`, F2 włącza `[input] flat_body_pick_third`. Oba domyślnie 1.
2. Niebo, mgła i sufit jaskini zostają włączone przy menu na monitorze. W VR płaskie menu
   nadal je gasi (`EffectsPaused`).
3. Ruch myszy nad menu nie obraca kamery po jego zamknięciu (`MouseTick`, flaga `dropTravel`).
4. Panele nie są już rozmyte przez niebo. Warstwa interfejsu jest kopiowana także przy menu
   (`vr/hud.cpp`), a `UiMaskSolid` w efekcie zostawia kolor gry tam, gdzie interfejs coś kryje.
5. Nazwy przedmiotów na ziemi w F2 i F3 zostają tylko wtedy, gdy punkt przedmiotu wpada w obraz.
   Sam test „za kamerą” nie wystarczył: haczyk na układanie nazwy w trybie płaskim w ogóle się
   nie zakładał (zakłada go dopiero zmiana rozmiaru nazw na podłodze VR). Przy Alcie gra
   dociska nazwę przedmiotu obok obiektywu do krawędzi ekranu, więc etykieta wjeżdża nad głowę.
   `ItemLabelOnScreen` w `vr/vrcam.cpp` zwraca „nie pokazuj”, gdy rzut jest poza kadrem albo za
   obiektywem. W F1 test jest wyłączony (`g_enabled` = false).
6. Znacznik kapliczki i czary na horyzoncie. Gracz potwierdził 2026-10-19: jest dobrze,
   efekty są dużo wyraźniejsze, szczególnie czary. Działa zwykły zwrot mgły w
   `D2R_DepthFog.fx`: jasny piksel wraca na wierzch mgły. Dwie wcześniejsze próby nie
   weszły, opis niżej.
7. Usunięcie czarnego owalu ze środka portalu (`[input] portal_face=1`, potwierdzone
   2026-10-19 14:03) — tylko flat F2/F3 (`g_enabled`). Liczniki `D2RRenderMesh` są
   zapamiętywane i przywracane w F1 (wcześniej zerowanie Granny zostawiało dziurę też w
   izometrii). Pierścień (`long_hole`) nietknięty. Autoteleport usunięty.
8. Dialog NPC w F3: `[input] npc_look_horizon=1` ustawia pitch myszy z wysokości
   MonStats najbliższego NPC (+6° w dół dla wszystkich). Potwierdzone dla większości
   NPC w miastach (2026-10-19).
9. Niebo utknięte na act1: w FX `SkyTex` → `VrSkyAct` + osobne `#define` per act.
   Nie wołać 24× `set_preprocessor` przy F3 — to wieszało ReShade (log 17:58).
   DLL tylko ustawia float; alias ACT1 tylko gdy brak obu uniformów.
10. Hitboxy postaci w `flat_pick`: bez luzu Y (−2/+14) na podesty — wycofane.
11. `[input] portal_axis` (kwaternion 45° pod efekty) jest **wyłączony** (domyślnie 0).
    Test 2026-10-19: pierścień i czary i tak nie czytają tego quata. `AimPortal` usunięty.
12. Lista nowych kluczy ini (flat): `INI_ADDED.md`. Hipoteza schodów / NPC na podestach:
    shear izometryczny `grid + height*√(3/2)` w XZ (`melee_bodies.h`) — nie `portal_axis`.
13. v15 był błędny w wniosku, nie w pomyśle: LookAt jest punktem RENDERU (`L − Place = L.y·k`, zmierzone
    2026-10-26 przez `[debug] flat_elev`). Naprawione w v17: kapsuły w `grid + y·k`, zasięg od siatki bohatera,
    promień izometryczny `[input] flat_iso_ray=1` (domyślnie 1). Potwierdzone w grze: obiekty zaznaczają się
    na każdej wysokości, czary kierunkowe i obszarowe idą pod celownik (CHANGES.md pkt 8).
14. Otwarte: kapsuła Tyraela w Fortecy Pandemonium (akt 4) jest przesunięta — trzeba celować nieco na prawo od
    niego i w stopy.

## Próby, które nie weszły

Gracz potwierdził, że nic z tego nie zmieniło obrazu. Nie powtarzaj tych samych kroków.
Nie ruszaj plików gry (CASC, `mods\`, siatki, efekty cząsteczek, `default_mod`).

### Portal, czarny środek

Czarny środek to długa siatka `fx_mesh_portalSphere_convex_long_hole` (cztery warianty)
w `vfx_town_portal_NewStuff.particles` i w `..._NewRed`. Pierścień to inna siatka,
`fx_mesh_townportal_lip_crawl_v4`. Obiekt ma `rotationOverride` 45° w starą kamerę
izometryczną. Z przodu otwór jest kołem w pierścieniu. Z boku widać długość siatki obok.

1. Obrót całego szkieletu do oka. `[input] portal_face=1`, `AimPortal` w `vr/skeletons.cpp`,
   przed pozą. Miał łapać nazwy `town_portal`, `portal_permanent`, `guild_portal`,
   `cain_portal`, `permanent_portal`, `last_portal` i raz wpisać do logu
   `a portal's black hole turns toward the camera`. Linii nie ma. Nazwa szkieletu jej nie
   zawiera, więc macierz nie była zapisana. DLL z tą próbą jest już u gracza i nic nie dała.
2. Wywalenie siatki dziury. Pozycje są spakowane BitKnit (Granny, kompresja 4, sekcja 0).
   Skan surowego pliku nie znajduje wierzchołków. Podmiana pliku i tak wymaga moda, a
   `default_mod` jest puste. Gracz zamknął tę drogę: w pliki gry nie wchodzimy.

Gracz poprawił opis (2026-10-19), po obrocie kamery, nie po chodzeniu dookoła.
Czarny środek stoi w miejscu. Rusza się pierścień. Pojawia się jego kopia w nowym
miejscu, a stara wersja, ta która jeszcze leżała na środku, znika po chwili, jakby
pierścień dostał nową pozycję i poprzednia kopia dogasała. Środek nie wyjeżdża z
pierścienia. Pierścień odchodzi od środka.

Pozycja pierścienia jest liczona od kierunku kamery, nie od osi portalu. W efekcie
jest `GeometryBillboard` z `BillboardingMode` i `scene.orientation`: karta dostaje
orientację widoku w chwili narodzin i trzyma ją, aż zgaśnie. Siatka wargi
`lip_crawl` nie siedzi na środku swojego pivota, więc nowy kierunek kamery kładzie
ją obok dziury. Stara karta, urodzona gdy kierunek jeszcze schodził się ze środkiem,
zostaje na środku i po chwili gaśnie. `view.direction` w tym pliku jest przy świetle
portalu, nie przy pozycji wargi.

Test kwaternionu (2026-10-19 04:52) nic nie zmienił w obrazie. Log: `portal axis test on`.
Kwaternion kamery był na sztywno 45°, macierze widoku zostały prawdziwe. Pierścień
i czary wyglądały tak samo, więc nie czytają tego kwaternionu. Czytają widok.
`[input] portal_axis` nie jest w żywym ini. Poprzednia biblioteka, ta która jest
w `plugins`, to `build\Release\d2rl-vrcam-before-axis.dll` (03:47). Plików gry nie ruszamy.

Obrót jest wyłączony. Zapis macierzy przesuwał pierścień w złą stronę, bo pierścień jest
kartą od widoku, a dziura inną siatką. Nie wracać do yaw.

Wycięcie trójkątów nie weszło. Log 2026-10-19 05:36: budowa materiałów widzi siedem modeli
dziury (`_lod0`), każdy z linią `could not be emptied`. Linii `its triangles are dropped` nie ma.
Wskaźnik siatki nie jest układem Granny, który był zakładany. Tej próby nie powtarzać i nie
zerować liczników na tym samym układzie.

Dziura jest nieprzezroczysta (`Transparency.Type Opaque` na `fx_mesh_portalSphere_01`, długa
dziura to czarna siatka na `Diablo_Uber_Mesh`). Zanikanie całego efektu albo `VfxSetVisible`
schowałoby też pierścień i wir (`innerflow`). Osobnej alfy samej dziury nie ma.

Zasłona w shaderze (`D2R_DepthFog.fx`, piksel dziury zamalowany kolorem z otoczenia) też nie
weszła. Cztery wersje grane 2026-10-19 05:52 do 06:30. Liczona od miejsca portalu na ekranie
siadała na pierścieniu, bo dziura po obrocie widoku stoi obok pierścienia. Liczona po
głębokości łapała ciemną część, a niebieską, podświetloną pierścieniem, zostawiała. Poszerzona
na niebieski zjadała pierścień. Pętla w shaderze nie kompiluje się (X3511), trzeba rozpisywać.
Graczowi efekt się podobał, ale dziury nie zasłonił. Kopie w `build\Release`:
`D2R_DepthFog_portal_cover.fx` (tylko ciemna), `D2R_DepthFog_portal_cover_wide.fx` (z
niebieskim), `D2R_DepthFog_original.fx` (bez zasłony). Shader w repo i przy grze jest bez
zasłony. Pliki modeli (`.model`) są spakowane, bez czytelnych nazw: nie ma czego podmieniać
w pliku.

Nowa próba, jeszcze nie potwierdzona w grze: `[input] portal_face` (płaski widok) działa w
haczyku budowy materiału modelu (`vr/roof_sides.cpp` `HookBuild`, `vr/portal_hole.cpp`
`OnModel`), gdy przychodzi jeden z modeli dziury (`long_hole`, `portalsphere` bez `innerflow`
i `lip_crawl`):

- `1` (null material): budowa nie była wołana, haczyk zwracał null. CRASH (2026-10-19 12:46:40):
  trzy modele dziury przeszły `its material build is skipped` i w tej samej chwili gra padła
  w swoim kodzie pod `D2R.exe+0x1006C79`, odczyt z adresu `0x1B00` (pole `+0x1B00` pustego
  materiału). Gra nie sprawdza nulla. Nie wracać do zwracania nulla.
- `2` (empty model name): budowa wołana z pustą nazwą modelu `""`. Log 2026-10-19 12:49:10:
  wszystkie siedem modeli dziury przeszło przez `its material is built under an empty name`,
  bez crasha, ale czarna dziura została w obrazie. Pierwszy argument `RoofMaterialBuild` służy
  tylko do logowania błędów, a sam materiał i jego właściwości są czytane z czwartego argumentu
  (`const Material* material`) przez `GrannyFindMember`.
- `3` (podmiana właściwości materiału w `HookFind`): Log 2026-10-19 12:54:33 pokazał dlaczego
  `changes=0` dla wszystkich 7 modeli dziury: każdy model dziury ma pusty zastępczy materiał
  `mat='lambert1' mapCount=0`, a `RoofMaterialBuild` pyta tylko o `'technique'` (`found=0`)
  i `'needsSingleJointSkinning'` (`found=0`). Renderer cząsteczek VFX bierze własny shader z
  `.particles`, a z pliku `.model` używa wyłącznie geometrii siatki.
- Jednocześnie zapytanie o `'needsSingleJointSkinning'` (pole `ExtendedData` struktury
  `GrannyMesh` pod `+0x3C`/`+0x44`) udowodniło, że trzeci argument `RoofMaterialBuild` (`mesh`)
  naprawdę jest `GrannyMesh*`, oraz wyjaśniło, dlaczego próba z 05:36 (`could not be emptied`)
  nie weszła: pod `+0x00` w `GrannyMesh` leży wskaźnik `const char* Name` (8 bajtów), a kod z
  05:36 czytał sam adres wskaźnika jako tekst ASCII bez dereferencji `*(const char**)mesh` (oraz
  szukał `PrimaryTopology` pod `+0x34` zamiast `+0x1C`).
- Zerowanie buforów CPU w `GrannyMesh` wewnątrz `HookBuild` (`0xF99C40`): Log 2026-10-19 13:07:02
  potwierdził wyzerowanie wszystkich 7 modeli dziury (`collapsed via mesh(r8)`, `verts=230/211/207/210`,
  `idx16=1140/1083/1056/1080`, `idxZeroed=1`), ale czarna dziura została na ekranie. Zrzut
  zdeszyfrowanego kodu funkcji wołającej wokół `0x140FA17BC` (funkcja `0xF9E820..0xFA19E8` w `.pdata`)
  wyjaśnił dlaczego: pętla budująca materiały pod `0xFA173C..0xFA17DC` wykonuje się na samym końcu
  funkcji tworzącej siatkę, kiedy bufory GPU wierzchołków/indeksów oraz obiekt uruchomieniowy
  `D2RRenderMesh` (w rejestrze `r12`, `lea rbx, [r12+278h]`) są już gotowe.
- Zerowanie liczników w gotowym obiekcie `D2RRenderMesh` (`rm+0x158`, `rm+0x1CC`, `rm+0x21C`) przez
  odnalezienie na stosie `HookBuild` zapisanych rejestrów `r12` (`D2RRenderMesh*`) i `rbx` (`r12 + 0x278`):
  **ZADZIAŁAŁO W GRZE (2026-10-19 13:35:24)**! Czarny owal zniknął (`rmZ=3/0 [rm+158(tri) rm+1CC(verts) rm+21C(tri)]`),
  jednak razem z nim zniknął też animowany niebieski pierścień wokół owalu (została poświata na ziemi,
  clickbox i nazwa).
- Dekodowanie binarnych bloków `CLayerCompileCacheRendererProperty` (`0x1120`) we wszystkich 12 rendererach
  siatek w `vfx_town_portal_newstuff.particles` (oraz `vfx_town_portal.particles`) wyjaśniło dlaczego:
  - Czarny, nieprzezroczysty owal tła (`portalBack`, `Diablo_Smoke_Mesh.pkma`, `Transparency.Type = 4` Opaque,
    `White.texture` barwione na czarno) używa **pełnej siatki bez otworu**: `fx_mesh_portalSphere_01.model`
    (oraz w starym pliku `fx_mesh_portalSphere.model` i `fx_mesh_portalSphere_convex.model` — wszystkie trzy mają
    `380` trójkątów i `230` wierzchołków).
  - Natomiast wszystkie 4 siatki z `long_hole` w nazwie (`fx_mesh_portalSphere_convex_long_hole_vertexalpha*`,
    mające `352..361` trójkątów i `207..211` wierzchołków — czyli siatki z wyciętym podłużnym otworem w środku!)
    są używane przez **animowany niebieski pierścień fal wokół otworu** (`New Wavelets`, `Overlapping Wavelets`,
    `Waves OUTRO`, `Portal Energy Glow Card`, na materiale `Diablo_Uber_Mesh.pkma` z gradientem
    `T_FX_portal_gradient_03.texture` / `T_FX_Portal_Gradient02.texture` / `T_FX_towngradient.texture`).
    Natomiast `fx_mesh_townportal_lip_crawl_v4` (`portalBack END`) i `fx_mesh_portalsphere_innerflow_3`
    (`Russels Waves ENd`) odpalają się tylko przy zamykaniu portalu.
- Rozwiązanie docelowe (`[input] portal_face=1`, **POTWIERDZONE W GRZE 2026-10-19 14:03**): `IsHolePath` w `vr/portal_hole.cpp`
  pomija siatki pierścienia `long_hole` (`has("long_hole") -> false`) i zeruje `D2RRenderMesh` (`rm+0x158`, `rm+0x1CC`, `rm+0x21C`)
  wyłącznie dla 3 pełnych siatek czarnego owalu (`fx_mesh_portalSphere_01_lod0.model`, `fx_mesh_portalSphere_lod0.model`,
  `fx_mesh_portalSphere_convex_lod0.model`). Czarny owal zniknął, a animowany niebieski pierścień wokół niego, poświata na ziemi,
  clickbox i nazwa działają prawidłowo.
- `0`: budowa gry.

Dziura już zbudowana trzyma materiał do następnego wczytania modelu (nowa gra). Log przy
starcie: `the black centre's material is not built when its model loads`. Haczyk budowy
materiałów (`roofsides::Install`) jest zakładany także bez `roofs_two_sided`, gdy
`portal_face` jest 1 albo 2 w trybie płaskim. DLL nie jest kopiowane do gry stąd.
Plików gry (CASC, siatki) nie ruszamy.

Zasłona shaderem na czarnym owalu nie weszła, ale dała ciekawe efekty na pierścieniu.
Skoro owal jest już wycięty, dwie nowe kopie tego samego inpaintu celują w pierścień
(jeszcze nie potwierdzone w grze):

- `build\Release\D2R_DepthFog_portal_ring.fx` — wąska: niebieskie ciało pierścienia, bez
  białego rdzenia. Promień 16, poświata na ziemi zostaje (`world.y < 1`).
- `build\Release\D2R_DepthFog_portal_ring_wide.fx` — szeroka: luźniejszy błękit plus jasny
  biało-cyjanowy rdzeń. Promień 22 (pierścień odjeżdża od portalu przy obrocie kamery).

Zasłona shaderem na pierścieniu **porzucona** (2026-10-19 14:54). Żywy
`D2R_DepthFog.fx` jest z powrotem wersją bez `PortalCover` (mgła na horyzoncie zostaje).
Kopie eksperymentalne zostają w `build\Release\` (`portal_cover`, `_wide`, `_ring`,
`_ring_wide`). DLL nie podaje już `PortalN`. Czarny owal nadal jest wycięty siatką
(`portal_face=1`).

### Mgła na horyzoncie, ucięte efekty

Znacznik kapliczki, czary, świecąca ręka. W niebie są całe. Na samym pasku horyzontu
znikają. Im dalej jest tło za wyciętym efektem, tym cięcie mocniejsze. Na wąskim pasku,
gdzie mgła dochodzi do końca, znika wszystko.

1. `[fog] spare_effects=1` na monitorze. Kopia przezroczystych warstw gry miała zostawić
   efekt nietknięty i zamglić tylko nieprzezroczystą część piksela. Log 2026-10-19 03:23:17:
   shader resolve się zgodził, trzy cele przezroczyste nie zostały zobaczone
   (`vr/fog_spare.cpp`, `AtResolve`, powód 2, `g_trio` puste). `SpareOn` został fałszywy.
   Obraz się nie zmienił.
2. W gałęzi pustki `reshade/D2R_DepthFog.fx` jasny piksel (szczyt kanału 0.45..0.85)
   zostawia swoje światło. Efekt jest w żywym `D2R_DepthFog.fx`. Ta gałąź działa tylko tam,
   gdzie w buforze głębokości nic nie ma. Pasek horyzontu ma głębokość dalekiego gruntu,
   więc ta poprawka go nie dotyczy. Ręka dalej jest ucięta dokładnie na tej linii.

Trzecia próba weszła. Gracz: efekty są dużo bardziej widoczne, szczególnie czary.
W zwykłym zwrocie mgły (nie w gałęzi pustki, nie w jaskini) jasny piksel wraca na wierzch:
`lerp(colour, fogColour, t) + colour * (t * smoothstep(0.45, 0.85, szczyt))`.
Opis dla autorów jest w `CHANGES.md`, punkt 5. Zostało ryzyko, niepotwierdzone: bardzo
jasna piana na dalekiej wodzie może wrócić na pasku horyzontu.

## Dlaczego bliski efekt nie wychodzi przed mgłę

Efekt bez głębokości jest domalowany w kolor, a w buforze głębokości zostaje to, co było
za nim. Przejście mgły nie wie, że znak jest przy graczu. Bierze głębokość, która już
tam leży, i z niej liczy `FogAmount` (`NearPlane / depth`, od `FogStart` do `FogEnd`).
Na horyzoncie grunt jest na końcu mgły albo za nim, więc ilość mgły jest 1 i
`lerp` zamienia piksel w sam kolor mgły. Bliżej, gdzie tło jest bliższe, ilość jest
mniejsza i z efektu zostaje więcej. Dlatego cięcie jest tym mocniejsze, im dalej jest
tło, a najwęższy pasek na horyzoncie wycina najmocniej.

Gałąź pustki (niebo, brak głębokości) robi co innego: bierze mgłę pierwszego gruntu
w dół ekranu i gasi ją między 64 a 512 pikselami w górę. Dwa, cztery, osiem i szesnaście
pikseli pod spodem wygrywają z tym rozmyciem (`max(tf, FogAmount)`). Na linii horyzontu
ten grunt jest już w pełnej mgle, więc pasek i tak dostaje pełne cięcie. Kilka pikseli
wyżej w niebie tych próbek już nie ma i efekt zostaje.

Bliski efekt nie jest rysowany przed tą mgłą, bo mgła nie ma jego odległości. Ma tylko
odległość tła.

## Później: pasek na dole ekranu

Pasek z kulami życia i many, miksturami, umiejętnościami i przyciskiem ustawień jest dobrze
złożony w F3. W F2 (trzecia osoba) i w F1 (oryginalny widok gry) robi się mniejszy, a miejsca
kliknięć zostają przy starym, większym układzie, więc klik mija przycisk.

To wygląda na ustawienie, nie na nowy błąd rysowania. Sprawdź to, zanim ruszysz kod.

`BarSizeNow()` w `vr/vrcam.cpp` zwraca rozmiar paska względem rozmiaru gry (1 = 100%):

- F3, kamera włączona i nie trzecia osoba: na sztywno `1`. Dlatego FPP jest dobre.
- F2: `[hud_third] bar_size`. W żywym ini gracza jest `71` (71%).
- F1, bo `g_enabled` jest wtedy false: `[hud_top] bar_size`. W żywym ini jest `74`.

Oba klucze są też w `d2r_vr.ini` w repo, w tych samych wartościach. Suwak w ustawieniach
(`tools/settings/settings.cpp`, „Toolbar size, %”) działa tylko przy `[hud_top] classic=0`
i `[hud_third] classic=0` („In the picture”). Żywy ini nie ma wpisu `classic`, więc zostaje
domyślne 0 i suwak działa. Rozmiar idzie do `hud::SetPictureZoom` i efekt rysuje pasek od nowa,
mniejszy, wokół środka dolnej krawędzi. Kliknięcia zostają przy widgetach gry w pełnym rozmiarze.

Pierwszy sprawdzian: w `E:\battlenet\d2r\d2rloader\plugins\d2r_vr.ini` ustaw
`[hud_third] bar_size=100` i `[hud_top] bar_size=100`, wejdź w F2 i w F1. Klucze ładują się
przy przeładowaniu ini (`ReloadIfChanged`, okolice `g_set.barSizeThird`). Jeśli pasek wróci
do rozmiaru z F3 i kliknięcia znowu trafiają, wystarczy opis w `CHANGES.md` i nie trzeba
zmieniać kodu. Jeśli gracz ma chcieć mniejszy pasek, a kliknięcia mają iść za obrazkiem,
to jest osobna zmiana: hitboxy nie jadą za `bar_size` / `bar_x` / `bar_y`.

Nie ruszaj F3. Tam `BarSizeNow` celowo zwraca 1.
