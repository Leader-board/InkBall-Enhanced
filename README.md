# InkBall — Visual Studio 2026 C++ reconstruction

This package contains a complete, editable C++20 Windows desktop application,
`InkBall.sln`, the recovered original game resources, regression tests, and a
rebuilt x64 `inkball.exe`.

**Version 1.2** improves browser responsiveness and adds a pen-shaped pointer,
current-board mechanics settings, restart and an optional FPS display. The
score/HUD digits and their scaling are **unchanged**. See
`docs/CHANGES-1.2.md` and `docs/SCORE_RENDERING.md`.

The version 1.1 improvements remain: all-board browsing, stable offscreen
presentation, thick rounded ink, clean ball transparency and the corrected
manifest build configuration. Their history is in `docs/CHANGES-1.1.md`.

The source was reconstructed from the native executables and resources in the
supplied `HoldEm-InkBall-Tinker-Setup.exe`. It is a new source implementation
of the recovered game behavior, not Microsoft's unavailable original source
tree or a byte-for-byte reproduction of its machine code. The new executable
runs its own engine; it does not extract, load, wrap, or launch the old EXE.

## Run the included build

Open `bin/Prebuilt-x64/inkball.exe` on 64-bit Windows 10 or Windows 11. All game
assets and localized menus are embedded in that executable. The included
binary was cross-compiled with MinGW-w64 and exercised under Wine; see
`docs/VALIDATION.md` for the exact validation and its limits.

## Build in Visual Studio 2026

1. In **Visual Studio Installer**, install **Desktop development with C++**,
   including **MSVC v145** and a **Windows SDK**. Use an up-to-date Visual
   Studio 2026 installation.
2. Extract this entire package, keeping its directory structure intact.
3. Open **`InkBall.sln`**.
4. Select **Release | x64** in the toolbar. The solution also has Debug and
   Win32 configurations.
5. If needed, right-click **InkBall** and choose **Set as Startup Project**.
6. Choose **Build > Build Solution**, then press **F5** to debug or **Ctrl+F5**
   to run without the debugger.

The application output is **`bin\x64\Release\inkball.exe`**. Debug and Win32
builds use the corresponding platform/configuration directories. All
projects use the static Microsoft C++ runtime, so the game does not depend on
a separately installed .NET runtime, MFC, ATL, the old Tablet PC ink COM
component, or DirectDraw.

From PowerShell, the included build script finds Visual Studio 2026 and also
runs the engine, board-catalog and Windows renderer tests:

```powershell
.\Build.ps1 -Configuration Release -Platform x64
```

From a Visual Studio developer command prompt:

```bat
msbuild InkBall.sln /m /p:Configuration=Release /p:Platform=x64
bin\x64\Release\InkBall.EngineTests.exe assets\levels.ikl
bin\x64\Release\InkBall.CatalogTests.exe assets\levels.ikl
bin\x64\Release\InkBall.RendererTests.exe
```

If Visual Studio reports that v145 is missing, add/update the MSVC component
in Visual Studio Installer. The project intentionally targets the Visual
Studio 2026 toolset; it does not silently retarget to an older compiler.

The manifest is handled by Visual Studio's manifest tool through the project
settings. `resources/InkBall.rc` no longer declares it. The provided CMake
build also lets MSVC handle its manifest; only a MinGW build generates its
own manifest resource, keeping the prebuilt game a single executable.

## What is preserved

- The original **551 boards**, with their actual launch queues, speeds, time
  limits, tile encodings, concealed tiles, and difficulty/bonus classification.
- The original sprite bitmap and icon, with a **544 × 586** logical canvas,
  **17 × 17** board, **32-pixel** tiles, **24-pixel** balls, and original HUD.
- All five original difficulties and the original normal/bonus collections.
- Solid and recoloring walls, matching-color breakable walls, one-way gates,
  color filters, timed walls, color-keyed walls, ramps, launchers and drains.
- Original score values: gray 0, red/orange 200, blue 400, green 800 and gold
  1,600; 10 points for a broken wall; one point per remaining second after a
  completed board; one extra placeable wall per 1,600 points.
- Four-second initial launch delay, per-level subsequent launch intervals,
  timer start on first launch, bonus-board cadence, recent-board avoidance,
  same-board restart after a loss, and high-score retention.
- Original-style maximum four-millisecond ball-motion steps, 32-point integer
  circle sampling, leading-arc reflection, ball-to-ball deflection and drain
  attraction. Ink is removed as a complete stroke after it deflects a ball.
- The **35 original menu/string translations**, embedded as Windows language
  resources. Windows selects the applicable resource language. Newly written
  Help/About text and a few diagnostic strings are in English.

| Difficulty | Normal boards | Bonus boards | Total |
|---|---:|---:|---:|
| Beginner | 138 | 48 | 186 |
| Novice | 78 | 28 | 106 |
| Intermediate | 77 | 26 | 103 |
| Advanced | 73 | 21 | 94 |
| Expert | 45 | 17 | 62 |
| **Total** | **411** | **140** | **551** |

## Controls

| Action | Control |
|---|---|
| Draw an ink barrier | Drag with the left mouse button or a Windows pen |
| Place an earned gray wall | Right-click an empty, unoccupied floor tile |
| New game | F2 or Game > New Game |
| Restart the current board | Ctrl+R or Game > Restart Current Board |
| Pause/resume | Space or Game > Pause |
| Remove ink | Game > Clear Ink |
| Browse, inspect or play any board | Ctrl+B or Game > Browse Boards |
| Configure the current board | Ctrl+M or Game > Board Mechanics |
| Show/hide measured frame rate | Game > Show FPS |
| Change difficulty | Difficulty menu |
| Help | F1 |
| Exit | Esc or Alt+X |

Gray balls may enter any hole and gray holes accept any ball; these neutral
captures score zero. A ball entering a different non-gray color, or an expired
timer, ends the current attempt. Dismissing the game-over message restarts the
same board with the score and earned-wall count reset, as in the original.

The game pauses while inactive, minimized, being resized, or in a menu/dialog.
A manual pause remains in effect after focus returns. Resizing preserves the
original aspect ratio and maps the pointer back to the original board grid.
The desktop window uses the current Windows theme rather than recreating
Vista's title-bar frame.

The drawing pointer is a white, dark-outlined pen recreated from the supplied
original-gameplay footage. Its tip is the drawing hotspot, and it scales for
the display DPI. The arrow remains over menus, the HUD and window margins.

## Board browser

Open **Game > Browse Boards** or press **Ctrl+B**. The current game pauses
while the browser is open. **Close** or **Esc** returns to the same game;
an existing manual pause is preserved.

- **Difficulty:** all boards, or Beginner, Novice, Intermediate, Advanced or
  Expert. **Board type:** normal and bonus, normal only, or bonus only.
- **Sort by:** board ID, shortest allowed time first, or longest first.
  Clicking **Time allowed** in the list also toggles ascending/descending
  numeric sorting. **Unlimited** is treated as infinity: last in ascending
  order and first in descending order. Equal times use ascending board IDs.
- Select any row to see a static original-art preview and its parameters:
  board ID, difficulty/type, time limit, launch interval, speed bounds,
  stored queue and color counts, randomization, preplaced balls, tile counts,
  resource offset and record size.
- Click a tile in the preview to inspect its raw code, coordinates, decoded
  direction/heading/timing/color parameters, and any concealed replacement
  under a breakable wall. Scroll the details pane for the complete report.
- **Play Selected Board**, Enter, or double-click starts that board with a
  new score and block count, retaining the high score. The chosen difficulty
  becomes the current difficulty. The selected board's own queue and shuffle
  flag apply when played. The browser's static preview does not run physics
  or advance the current game.

Board IDs are the original zero-based resource indexes **0–550**, not the
number of boards completed in a play session. The browser is a new feature;
its labels are English. Its window can be resized or maximized.

Version 1.2 stops the paused game's update timer while browsing, avoids
repeated layout work and prepares only the latest requested preview. The
list supplies visible rows on demand. The original board data and sorting
behavior remain the same. See `docs/CHANGES-1.2.md` for the performance diagnosis.

## Current-board mechanics

Open **Game > Board Mechanics** or press **Ctrl+M**. The game pauses while
the settings window is open. Each field shows the current setting beside
the original value for this board.

| Setting | Allowed values | Effect |
| --- | --- | --- |
| Time limit | 0–999 seconds | Zero is unlimited. Changing the configured limit resets remaining time; leaving it unchanged preserves the countdown. |
| Launch interval | 0–60 seconds | Sets the interval between queued launches. The first queued launch retains its original four-second delay. |
| Minimum initial speed | 1–1000 px/s | Lower bound for future launches and preplaced balls after restart. |
| Maximum initial speed | 1–1000 px/s, at least the minimum | Upper bound for the same initial-speed range. Equal bounds give a fixed speed. |
| Ball speed multiplier | 0.1–4× | Rescales balls already moving and multiplies future initial speeds. Reapplying the same value does not multiply them again. |
| Chevron boost | 0–20× the original | Controls the directional acceleration on a chevron. The original value is 1×; zero removes its acceleration. |
| Chevron speed limit | 1–2000 px/s along the arrow's axis | Caps the affected velocity component while touching a chevron. The original limit is 400 px/s. |

The initial-speed range uses the original random selection rule: its upper
bound is exclusive when the bounds differ. Initial speed multiplied by the
ball multiplier must not exceed 2000 px/s. A multiplier change that would
push an existing ball above 2000 px/s in either axis is also rejected.
Validation happens before any changes are applied.

**Apply** changes the current board and keeps the dialog open. **OK** applies
and closes it. **Cancel** discards edits made since the last successful Apply;
already applied values remain. **Restore Original Defaults** fills the form
with this board's original values; choose Apply or OK to use them.

Applying settings preserves positions, tiles, score, ink and pause state.
The default chevron boost preserves the original discrete three-pixel-per-
second velocity increment on each contact step. A zero boost still leaves
the configured chevron speed cap active.

Overrides belong to the current board in this session. **Restart Current
Board** retains them. New Game, loading a board from the browser, automatic
progression or closing the application discards them. Original level records
are never rewritten.

## Restart and FPS

**Ctrl+R** restarts the current board with its current mechanics settings,
original tile layout and ball queue. It resets the score, placed/earned
blocks, ink and timer while preserving the high score and any manual pause.
The queue follows the board's shuffle flag again when restarted.

**Game > Show FPS** adds a roughly one-second measurement of game paints per
second to the title bar. It reports the application's completed paint calls,
including exposure repaints; it is not a monitor-refresh or physics-step
counter. The title shows **paused** while the game is paused. This display
does not change the score area or other original HUD artwork.

High score, last difficulty and the Show FPS preference are stored per user in
`%LOCALAPPDATA%\InkBall-Reconstructed\settings.ini`. The default high score is
the original 5,000. Mechanics overrides are not saved between sessions. No
old InkBall registry data is modified.

## Score pixelation

The score and timer use the original small bitmap digit cells, enlarged with
the rest of the game canvas. Their pixelation is more visible at larger or
fractional scales. This release makes **no change** to the digits, HUD layout
or scaling algorithm. `docs/SCORE_RENDERING.md` explains possible future
improvements and their visual tradeoffs; those options are not implemented.

## Source layout

| Path | Purpose |
|---|---|
| `InkBall.sln` | Visual Studio solution, with game and three test projects |
| `InkBall.vcxproj` | C++20 Win32 desktop application, v145 |
| `InkBall.EngineTests.vcxproj` | Console regression-test application |
| `InkBall.CatalogTests.vcxproj` | Portable board filter/sort/inspection tests |
| `InkBall.RendererTests.vcxproj` | Windows renderer regression tests |
| `src/Game.h`, `src/Game.cpp` | Portable reconstructed engine and game state |
| `src/Levels.h`, `src/Levels.cpp` | Original IKL parser and difficulty tables |
| `src/WinMain.cpp` | Native window, input, menus, pause, settings and dialogs |
| `src/Renderer.h`, `src/Renderer.cpp` | GDI rendering, original HUD and scaling |
| `src/BoardBrowser.h`, `src/BoardBrowser.cpp` | Native modal board browser and preview |
| `src/BoardCatalog.h`, `src/BoardCatalog.cpp` | Portable filtering, sorting and parameter descriptions |
| `src/MechanicsDialog.h`, `src/MechanicsDialog.cpp` | Native current-board mechanics form |
| `src/PenCursor.h`, `src/PenCursor.cpp` | DPI-scaled pen pointer recreated from the footage |
| `src/SpriteRects.h` | Recovered original atlas coordinates |
| `resources/InkBall.rc` | Embedded assets, icon, version and shortcuts |
| `assets/app.manifest` | Manifest input for the build system |
| `resources/LocalizedMenus.rc` | All original localized menus and strings |
| `assets/levels.ikl` | Unchanged original level-resource bytes |
| `assets/sprites.bmp`, `assets/inkball.ico` | Extracted original artwork |
| `tests/engine_tests.cpp` | Gameplay and all-board regression tests |
| `tests/catalog_tests.cpp`, `tests/renderer_tests.cpp` | Browser data and visual regression tests |
| `tools/extract_assets.py` | Repeat asset extraction without running the EXE |
| `docs/RECONSTRUCTION.md` | Binary evidence, format details and fidelity limits |
| `docs/VALIDATION.md` | Build/test results and remaining verification limits |
| `docs/CHANGES-1.1.md` | Requested fixes, design decisions and reference-media findings |
| `docs/CHANGES-1.2.md` | Responsiveness, pointer, mechanics, restart and FPS changes |
| `docs/SCORE_RENDERING.md` | Explanation of unchanged bitmap digits and future rendering choices |
| `THIRD_PARTY_NOTICES.txt` | Original-resource provenance and reference notices |

The source has no third-party package-manager dependencies. On Linux or macOS,
CMake builds the portable engine and tests; the native game window is Windows
only:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Repeatable inspection commands

The Windows application provides optional inspection switches. They are not
needed to play. `--smoke-test` loads and draws every recovered board and exits.
`--screenshot` writes a BMP of the actual renderer and exits. These modes do
not read or write your saved game settings.

```bat
inkball.exe --smoke-test
inkball.exe --screenshot board.bmp --level 0 --seed 1 --advance 5
```

`--level` is the zero-based resource index, 0–550. The board browser provides
the equivalent in-game selection. `--difficulty` accepts 0–4, `--seed` allows a repeatable random
sequence, and `--advance` advances up to 600 simulated seconds.

The supplied extraction script uses Python's standard library. To reproduce
the three core assets from an extracted original executable:

```sh
python tools/extract_assets.py path/to/original/inkball.exe --output recovered-assets
```

To unpack the original NSIS installer first, also install 7-Zip and use:

```sh
python tools/extract_assets.py HoldEm-InkBall-Tinker-Setup.exe --installer --7zip path/to/7z.exe --output recovered-assets
```

Neither command runs the supplied installer or original game. Extracted
asset hashes can be compared with `docs/ORIGINAL_ASSETS.json`.

## Fidelity and validation boundary

The level data and bitmap bytes are recovered, not redrawn or approximated.
The engine is reconstructed C++, using constants and control flow recovered
from the supplied native x86 game and corroborated with a decompilation
reference. The supplied x64 game has identical core assets.

Exact frame-by-frame equivalence is not claimed. Native Win32 polylines replace
the unavailable Tablet PC ink implementation, and modern timing/rendering,
pen event delivery, and floating-point compilation can differ from the old
binary. These are detailed in `docs/RECONSTRUCTION.md`. The source and included
binary were built and tested as described in `docs/VALIDATION.md`; a native
Visual Studio 2026 build and a hardware-pen test were not available here.

Original assets retain their original Microsoft ownership. The new code and
reference notices are identified separately in `LICENSE.txt` and
`THIRD_PARTY_NOTICES.txt`.
