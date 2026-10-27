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
14. Tyrael (akt 4, Forteca Pandemonium) lewituje: jego hover w grze nie obejmuje stóp, tylko punkty o 2–12 jednostek
    wstecz po przekątnej `(−1,−1)`. Naprawione: promień izometryczny dla ciał idzie przez oś ciała na 25–60% jego
    wysokości, nie przez stopy (CHANGES.md, „Floating units”). `[debug] flat_elev` czyta teraz hover gry bez klikania.

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

---

## Plamy i „pływające” tekstury na zmienionym poziomie (śledztwo od 2026-10-27, NIEROZWIĄZANE)

Stan na koniec sesji 2026-10-27. Nic z tego nie jest w `CHANGES.md` (nie ma jeszcze zweryfikowanej
poprawki), kod diagnostyczny leży w źródłach, **niezacommitowany**.

### Objaw (gracz)

Akt 2, Daleka Oaza, schody między półkami skalnymi, poziom poniżej 0, F2 i F3 (nie F1). Duża ciemna
plama na płaskim terenie, o miękkich krawędziach, razem z teksturami typu spękane błoto i trawą.
Przy ruchu myszą plama „pływa”: kursor w górę oddala ją, w dół przybliża, w lewo przesuwa w prawo.
W jej obrębie widać **modele kamieni**, które „materializują się” po wejściu w plamę i znikają
poza nią. **W F1 tych kamieni nie ma wcale.** Poza plamą kamienie są niewidoczne (jasny kamień na
jasnym piasku, bez cienia). Na poziomie 0 gracz widział to krótko albo wcale.

### Pewne ustalenia (zmierzone)

- Plama NIE jest w albedo G-buffera; jest już w pierwszej kopii celu koloru po przebiegu G-buffera
  (linia 101 śladu), czyli powstaje wewnątrz shaderów materiałów (shader terenu liczy oświetlenie
  i cienie sam, w pętli po listach dekali/świateł kafelka).
- W klatkach zatrzymanych (dwa zrzuty, różny pitch) plamy NIE przesuwają się względem świata:
  korelacja map z góry dała 0,00 jednostek przy każdej wysokości płaszczyzny −26…−17
  (`groundshift.ps1`). Zjawisko jest dynamiczne albo zależy od czegoś poza jedną klatką.
- Macierze świata (b0) tych samych obiektów są IDENTYCZNE w dwóch widokach. Macierze kamery w
  przejściu G-buffera są spójne (view, invView, viewProj). Macierz widok→mapa cieni (`cb10[14..17]`)
  jest spójna z naszym widokiem (bohater trafia w UV 0,50/0,50). Obie kaskady cieni pokrywają
  95–97% widocznej ziemi, w tym całą plamę.
- Teren płaski jest rysowany przez PS `a11d8d1c6efd3f33` (VS `1a9cba7642393f45`, bez UV, liczy
  `uv = (POS_WS.xz − POS_WS.y·1,224745 − cb10[3].zw)/180`, próbkuje `t35` kolor, `t36` normalne).
  Kolor terenu składa co klatkę **compute `775c45982728780a`** (2019 linii, tablice `t10–t13`,
  `t30–t34`, UAV `u4–u7`, grupy 8×8, stałe `cb4`, `cb7`): jego pominięcie daje czarną podłogę.
  Modele: PS `ae12b1e437155234` i pokrewne (pominięcie czerni modele). Trawa i rekwizyty:
  PS `df7d377bda243447` z VS `e93f2ed14b51e9e0` (wiatr, bez zależności od kamery).
- Pola kamery gry w świecie (log `[debug] cam_fields_log`): `type 0`, `FovDeg 3,44`, aspect
  2560×1440, near 10, far 10000 (długa ogniskowa imitująca ortografię; w menu bywa `type 1`,
  pudełko 94,28×53,03). `FovDeg` steruje projekcją własnej kamery gry w F1.
- **Obserwacje gracza (2026-10-27, druga sesja):** po obrocie i zatrzymaniu plama wraca dokładnie pod to
  samo ciało; patrząc w dół i w górę (po 2 s) zawsze w tym samym miejscu ziemi; przy chodzeniu bez myszy
  się nie rusza. „Płynie” tylko w trakcie obrotu. Limit 30 FPS nie zmienia, jak daleko odjeżdża (to nie
  jest opóźnienie o klatkę).
- Shader terenu `a11d8d1c…` ma pętlę **dekali w kafelkach ekranu**: kafelek z `(v0.xy − cb3[2].xy)·cb3[1].zw·cb10[13].zw`,
  liczba z `t4`, lista z `t5` (stride 1024), dane dekala (3 wiersze macierzy pudełka, flagi, warstwy) z bufora `t8`,
  tekstury dekali `t31` (normalne), `t32` (kolor/maski); test `|p|≤0,5` w pudełku. Macierz pudełka działa na `v2`,
  a `v2` to **pozycja w przestrzeni widoku** (VS `1a9cba76…`: `o2 = świat × cb3[5..8]`). Dekale dostają więc z CPU
  pudełka już przeliczone do widoku kamery. Plama/spękane błoto to najpewniej te dekale.
- `t44` to maska w przestrzeni ekranu (`v0.xy·cb3[0].zw`) mnożąca światło i ambient w terenie i trawie.
- Kolejność w klatce (`[debug] frame_order_log`): na jednym wątku gra dwa razy woła `BuildFrame` (`B`), dopiero
  potem nasza synchronizacja kamery (`S`), co ~18 ms: `B@0 B@3,9 S@5,7 B@18,1 …`. `MouseTick` chodzi na innym
  wątku co 1–2 ms.
- Przejście oświetlenia w ogólnym sensie: 17 shaderów compute, żaden nie próbkuje map cieni.
  `5fafbd06ef7f97d7` (2100 linii, 48× `sample_c`) to włosy/skóra, nie teren.

### Wykluczone (każde sprawdzone w grze lub na danych)

Modele `global/grunge/*`; pełnoekranowe przejścia piszące do albedo (`d92d77ce`, `0f833afc`,
`4c551117`, `53183967`, `bc28182c`, `f56bdb8a`, `35348faf`; `f0102f21` i `403f6d66` to światło,
ekran robi się czarny); AO, jakość cieni, blended shadows, tryb wygładzania; okno `cb10[47].zw`
(przypięte, log potwierdza); warstwy `cb10[69..89]` (wyzerowane, log potwierdza); tryb filtrowania
cieni `cb10[31/33].w` (wyzerowany, log potwierdza); zakres liniowej głębi (`depth_fix_mode 2`,
działa po naprawie, bez wpływu); pola `Pos/Quat` kamery (`keep_game_pose`); `[fog]` i `[sky]`
(efekt ReShade); `rings` 2→4; pola `AspectW/H` (`cull_scale`); pole `FovDeg` (`cull_fov`, w F2/F3
bez zmian, w F1 psuło widok, bo nie było ograniczone do naszej kamery: poprawione i wdrożone
2026-10-27 16:43). Druga sesja: `skip_shaders=5d76441e00c5b627` (cały obraz czarny, także menu:
to złożenie obrazu, nie maska), `skip_shaders=a914164017855558` (maska R8 2560×1440, bez zmian);
`[debug] early_view=1` (nasz widok przeliczony jeszcze raz przed każdym `BuildFrame`): plamy bez
zmian, za to cienie obiektów drżą przy obrocie, więc dekale nie biorą kamery w `BuildFrame`;
opóźnienie o klatkę (limit 30 FPS bez różnicy); postproces (zrzut przebiegów w trakcie obrotu:
plama identyczna po przejściu terenu i w obrazie końcowym); listy dekali/świateł w kafelkach
(`[debug] one_tile`, siatka 32×20 → 1×1, plama bez zmian); okna map (`[debug] window_log`, log każdej
zmiany: okno mapy terenu `b10[3].zw` skacze o pełne 180 jednostek, okno nakładki `b10[47].zw` = oko − 90
co 10 jednostek, obie treści przypięte do świata, płynnego ruchu nie tłumaczą). Nasza kamera nie ma
wygładzania obrotu (`VrView` bierze `TargetYaw/TargetPitch` wprost).
Gracz: plama idzie do przodu przy myszy w górę, w prawo przy myszy w lewo, niezależnie od kierunku
patrzenia (względem kamery). Uwaga: w poziomie „wyprzedza” obraz, w pionie „zostaje w tyle” — możliwy
odwrócony znak osi X w czymś, co przelicza ruch kamery.
Maska ekranowa `t44` = `1280×720 R16_FLOAT`, rysuje ją pełnoekranowy PS `3270b93ced3e18bd`;
`skip_shaders=3270b93ced3e18bd`: plama bez zmian (odpada). Nowe narzędzie `[debug] ui_trace_srv`
(z `ui_trace=1`): ślad wypisuje tekstury shadera terenu/trawy po rejestrach (`srv ps …: t35=#… …`)
i zrzuca 8-bitowe mapy ≥2048 (t35 kolor, t36 normalne terenu, 3888×3888) jako `uitex_*_28.raw`.
W śladzie (diag_srv_2026-10-27): t35/t36 3888×3888 RGBA8 UAV, t39 kaskady cieni, t40 512² fmt53,
t42 2560×1440 R8, t44 jw., t45–t48 32×8 RGBA16F.
**PRZEŁOM (2026-10-27 wieczór): plama JEST w mapie koloru terenu t35**, już przed oświetleniem
(zrzuty `diag_map_2026-10-27\{f1,f2,f2_down,f2_up}`, postać w miejscu, różne kamery). Ciemne plamy
błota to warstwa materiału składana przez compute `775c4598…`; mapa jest toroidalna (widoczne szwy
pasów w tym samym miejscu we wszystkich zrzutach). Porównanie masek plam
(`scripts\mapcompare.ps1`): trzy mapy z F2 mają ten sam wzór, przesunięty zależnie od kamery,
przesunięcia się sumują (f2→dół 9,6/5,9; dół→góra 1,9/17,8; f2→góra 11,5/23,7 jednostek x/z);
F1 względem F2 ~30–60 jednostek. Oko (b10[1]): F1 9619/419/10628, F2 9070/−5,8/10072,
dół 9082/4,5/10091, góra 9078/−11,1/10079 — przesunięcie nie jest prostą funkcją oka.
Wniosek: CPU podaje compute'owi położenie warstw (`cb7[5..8]` macierze pudełek, `cb7[0]` okno,
`cb4`) zależne od kamery.
**Potwierdzone na stałych (diag_map_2026-10-27\mv1, mv2; `scripts\tilelayers.ps1`):** compute 775c
działa tylko gdy kamera się rusza; jeden dispatch = jeden kafel 10×10 jednostek (216² tekseli,
`cb7[0].xy` = numer kafla jako int, `cb7[24].y` = 216), w nim do 3 warstw: wiersze z intami
i parametrami, potem macierz świat→warstwa (wiersz x, (0,1,0,0), wiersz z, translacja z w=1).
Te same warstwy (ta sama skala 0,0509 i 0,0150) mają w mv2 środek przesunięty o (−3,3; −6,5)
względem mv1 — CPU przesuwa warstwy materiału razem z kamerą. Oko mv1 9071,98/−3,86/10074,5,
fwd 0,243/−0,477/0,845; mv2 9072,23/−0,63/10076,8, fwd 0,228/−0,667/0,709 — wzoru nie znaleziono.
`[debug] cam_callers_log`: GetProjection wołane z 7 miejsc (0xEDE410, 0xEDE9FA, 0xE3977A, 0xF74D20,
0xE08778, 0xE039E7, 0x6AAEBF) tyle samo w spoczynku i w ruchu; RebuildView wcale — kod warstw
nie idzie przez te funkcje.

### WZÓR (2026-10-27, `[debug] layer_log`, `scripts\layerfit.ps1`, `diag_map_2026-10-27\layer_log_1.log`)

556 chwil w F2 dopasowanych do położeń z F1 (głosowanie par o tej samej skali):
**przesunięcie warstwy = h · (k − 4 · fwd.y · fwd.xz)** (osobno x i z), gdzie k = 1,224745,
fwd = kierunek patrzenia naszej kamery (b10[2]), **h = wysokość terenu pod postacią** (LookAt.y
= −22,97; L.y·k = −28,13 = wyraz wolny dopasowania −28,02). Oś z: −28,02 + 92,48·fy·fz, rms 1,0;
oś x: −26,9 + 79,9·fy·fx (kilka błędnych par). Dla kamery gry fwd = (−0,612, −0,5, −0,612):
−4·fy·fxz = k → przesunięcie 0. Gra liczy ścięcie warstw materiału z KIERUNKU KAMERY (zakłada
izometrię), a geometria terenu ma k na sztywno w shaderze (1,224745) — z naszą kamerą się
rozjeżdżają; na poziomie 0 (h = 0) problemu nie ma.
Gra bierze kierunek raczej z macierzy (View/InvView), nie z Quat (`keep_game_pose` nie pomógł).
Drogi naprawy: (1) znaleźć kod czytający kierunek (sprzętowy breakpoint na InvView/View w kamerze)
i dać mu kierunek gry; (2) poprawiać translacje warstw w b7 compute'a 775c przed GPU
(push_descriptors, jak depth_fix dla b10) o −h·(k − 4·fy·fxz) — wymaga h warstwy (h postaci
działa tylko dla warstw na poziomie postaci).

### NAPRAWA (2) DZIAŁA — test 2026-10-27 18:29

`[debug] layer_fix=1` (z `ui_trace=1`, `vr/uitrace.cpp` `LayerFix`): przy dispatchu 775c fwd z b10
row 2 tego dispatchu, h = LookAt.y (`VrFrame` → `uitrace::SetLayerFixHeight`), każda warstwa w b7
przesunięta (tu += a·ox + b·oz, tv += c·ox + d·oz), zakres już poprawiony rozpoznany po hashu treści.
Gracz: „sukces, plamy zniknęły” (przestały pływać). Log (`diag_map_2026-10-27\layer_log_fix.log`):
po poprawce warstwy w F2/F3 leżą tam, gdzie w F1, z dokładnością ~0,06 jednostki (wcześniej 30–60).
Krótkie przycięcie po włączeniu — najpewniej zalew `layer_log` (6000 linii w kilka sekund) i `ui_trace`.
**Wersja docelowa (18:38, zweryfikowana w grze, opisana w `CHANGES.md` sekcja 9):**
`[render] ground_layer_fix=1` (domyślnie 1), bez `ui_trace` — w haku camfix
`SetComputeRootConstantBufferView` (`camfix::GroundLayers` → `groundfix::Bound/Fix`), rejestr przez
`uitrace::CbRegister`; testowy `[debug] layer_fix` usunięty. Zostaje do zrobienia: h warstwy zamiast
h postaci (warstwy na innym poziomie niż postać) — np. sprzętowy breakpoint na kierunku kamery.

### Najbliższe tropy (nieprzetestowane)

1. Lista warstw/dekali dla compute `775c4598…` jest przygotowywana przez CPU. Sprawdzić skąd
   `cb7[0]` (okno), `cb7[2..8]` (warstwa, macierz) i `cb4` i czym jest przycinana lista.
2. Kamienie istnieją tylko przy naszej kamerze: system detali przy ziemi (zasięg od kamery)? Szukać
   funkcji gry, która go włącza i czy czyta wysokość/pozycję kamery.
3. Zapis tego samego celu po każdym przejściu już działa (`ui_trace_passes`); można go użyć dla
   klatki w ruchu (Ctrl+F10 podczas obrotu): rozstrzyga, czy plama jest przesunięta już po G-bufferze
   (dane dekali), czy dopiero w postprocesie (historia czasowa: TAA/skalowanie z wektorami ruchu
   liczonymi z kamery gry). Nie wiadomo, czy wcześniejszy test „trybu wygładzania” objął DLSS/FSR.
4. Gdzie CPU przelicza pudełka dekali do widoku (bufor `t8`) i z jakiej macierzy/kiedy.

### Narzędzia dodane (wszystkie klucze `[debug]`, domyślnie wyłączone)

`ui_trace` (Ctrl+F10, już było), `ui_trace_geometry` (opis rysowań geometrii), `ui_trace_passes`
(kopia kolorów sceny po każdym przejściu), `dump_shaders` (lista hashy), `dump_compute` (wszystkie
compute), `model_log`, `pin_decal_window`, `kill_decal_layers`, `no_sun_shadow`, `keep_game_pose`,
`cull_scale`, `cull_fov`, `cam_fields_log`; `[render] skip_shaders` obsługuje teraz też hash
compute (pomija dispatch). Szczegóły i znaczenie: `INI_ADDED.md`, sekcja „Diagnostic keys”.
Rozbieranie shaderów: `fxc -dumpbin plik.dxbc` (w Git Bash z `-`, nie `/`), Windows SDK
`...\10.0.26100.0\x64\fxc.exe`. Python nie jest zainstalowany; skrypty PowerShell/C# w
`E:\AndroidStudioProjects\d2vrflat_backup\diag_2026-10-27\scripts` (`uitex2png.ps1`,
`groundshift.ps1`, `shadowcheck.ps1`, `cascademap.ps1`).

### Naprawione przy okazji (do opisania w `CHANGES.md` dopiero po weryfikacji)

`depthsrc::Configure` (haczyk konstruktora ramki, `[render] depth_fix_mode 2`) był wołany tylko
raz przy starcie z `LoadSettings`, gdy kod gry jest jeszcze zaszyfrowany; pierwsza nieudana
kontrola sygnatur kończyła się po cichu i na stałe. Teraz `depthsrc::Tick` ponawia instalację
(do ok. 40 s). Efekt widoczny w logu, ale poprawy obrazu nie potwierdzono. Dodatkowo w ini gracza
`depth_fix_mode=1` jest martwy („0 root signatures bind b10”): tryb 1 nie znajduje bufora b10.

### Dane i stan ini

- Diagnostyki (ok. 2 GB) są w `E:\AndroidStudioProjects\d2vrflat_backup\diag_*`
  (zrzuty celów `uitex_*.raw`, kopie po przejściach `uipass_*.raw`, ślady `*.txt`, shadery
  `shader_*.dxbc`, `compute_*.dxbc`, katalogi `geo2_*`).
- Żywy `d2r_vr.ini` jest wrócony do wartości gracza (`rings=2`, `depth_fix_mode=1`, `ui_trace=0`,
  testowe klucze `[debug]` na 0); aktualnie ustawione `cam_fields_log=1`, `cull_scale=1`,
  `cull_fov=0`. Wdrożona DLL to wersja z 16:31 (z błędem `cull_fov` w F1, ustawione na 0 = bezpieczne).
- Źródła zmienione, niezacommitowane: `cleanroom/camera/d2rcam.{cpp,h}`, `vr/uitrace.cpp`,
  `vr/vrcam.cpp`, `vr/roof_sides.{cpp,h}`. `CHANGES.md` i `INI_ADDED.md` miały zmiany jeszcze
  przed tym śledztwem (nie moje).
