# Reconstruction evidence and fidelity notes

## Input and scope

The input was the attached NSIS installer `HoldEm-InkBall-Tinker-Setup.exe`,
87,045,110 bytes, SHA-256:

`fc132aa5866f7d2949f951ac0e11658a72d925b7aad44e1260d9cc74bfc86f46`

The installer was unpacked as an archive. It was not executed. Work was scoped
to InkBall; the installer also contains other games, which are not included
in this project.

Two native versions of InkBall were recovered:

| Original file | Architecture | Version | Size |
|---|---|---|---:|
| `inkball.exe` | x64 | 6.0.6000.16386 | 1,282,048 bytes |
| `inkball_1.exe` | x86 | 6.0.6001.18000 | 1,254,400 bytes |

Neither contains a CLR/.NET header. Thus there was no managed C# assembly to
round-trip into source. C++ was selected for native Win32 integration and to
express the recovered behavior directly without a managed runtime.

The full source/extracted-resource hashes are in `ORIGINAL_ASSETS.json`.
The x86 and x64 versions contain byte-identical level and bitmap resources;
the reconstructed multi-image icons also match.

## Recovering data and behavior

PE resource directories yielded the original IKL resource (type `IKL`, ID
20001), bitmap resource (ID 501), group icon (ID 101), menu (ID 102), string
tables and accelerator table. The bitmap's original DIB bytes are unchanged;
the `.bmp` file simply adds the standard 14-byte file header. The icon
reassembles the original 12 images with the corresponding ICO directory.

The native x86 executable was statically analyzed with Ghidra. The annotated
decompilation in [TC999/inkballd](https://github.com/TC999/inkballd), commit
`cbcee4fe44f00504e38bc2b76ddd93656e591fd6`, was also used to identify functions
and recover the sprite-coordinate table. Its MIT notice is retained. It
contained incomplete declarations/stubs, so it was not treated as an already
working game and is not linked into this project. No code from the separate
GPL reconstruction was used.

The resulting source separates the original level format, the reconstructed
engine, and a native Win32/GDI interface. The executable embeds the resources
at build time and runs the new engine directly.

## Original level format

`assets/levels.ikl` is 334,980 bytes with SHA-256:

`b2f262d56bb12340c3a4375dfc19a0056de39dff45da6ec5d86e676eb828175b`

It contains 551 sequential records and one final zero byte. Integers in the
record format are big-endian, unlike the PE container's little-endian fields.

| Field | Encoding |
|---|---|
| Record payload length | 16-bit, does not include its own two bytes |
| Time limit | 16-bit seconds; zero means unlimited |
| Launch interval | 8-bit seconds |
| Minimum ball speed | 16-bit pixels/second |
| Maximum ball speed | 16-bit pixels/second |
| Shuffle flag | 8-bit |
| Queued-ball count | 8-bit |
| Queued ball colors | One byte per ball |
| Board | 289 tile words, row-major, 17 × 17 |
| Breakable-wall underlay | Extra 16-bit tile immediately after that wall |
| Unused record tail | Present in 14 original records; retained and ignored |

The original loader's behavior and table boundaries were recovered rather
than inferring difficulty from the appearance of the boards.

| Difficulty | Normal IDs | Bonus IDs |
|---|---|---|
| Beginner | 0–137 | 138–185 |
| Novice | 186–263 | 264–291 |
| Intermediate | 292–368 | 369–394 |
| Advanced | 395–467 | 468–488 |
| Expert | 489–533 | 534–550 |

The source validates all 551 records and preserves all unused trailing bytes.
The maximum observed total of queued and preplaced balls is 19. The engine
retains the original 64-ball capacity.

## Tile encoding

The high nibble of a tile word identifies its category. Color occupies the
low nibble where applicable: 0 gray, 1 red/orange, 2 blue, 3 green, 4 gold.
`p1` denotes bits 4–7 and `p2` bits 8–11.

| Type | Meaning | Parameters |
|---:|---|---|
| 0 | Floor | No collision |
| 1 | Launcher | Releases queued balls |
| 2 | Drain quadrant | Color; p1 selects one of four 32-pixel quadrants |
| 3 | Solid/recoloring wall | Gray reflects; colored wall also changes ball color |
| 4 | Breakable wall | Breaks on gray-wall or matching-color hit; extra word gives the revealed tile |
| 5 | One-way/color gate | p1: up, down, left, right; gray accepts any color |
| 6 | Clock wall | Low nibble initial frame; closed interval 500 × (p2+1) ms, open interval 500 × (p1+1) ms |
| 7 | Color-keyed wall | Color; p1 initial frame; toggles when matching colored ball is drained |
| 8 | Ramp/chevrons | p1: up, down, left, right |
| 9 | Preplaced moving ball | Color; bits 4–11 angle in 11.25° steps, or 255 for random |
| 10 | Color filter | Color and horizontal/vertical orientation |

Breakable underlays in the supplied archive contain floors, drain quadrants,
ramps and filters. The actual encoded underlay is restored rather than always
turning a broken wall into empty floor.

## Gameplay evidence

The following behavior is carried into the implementation:

- Original board/HUD geometry: 544 × 586 canvas, 42-pixel HUD, 32-pixel tiles,
  24-pixel balls, 18-pixel queued-ball graphics. Magenta is the sprite key color.
- At most 4 ms per ball-motion step. The engine uses the original 32 integer
  perimeter samples and 17 leading samples to compute reflection normals.
- Reflection uses `v' = v - 2(v·n)/(n·n)n`. Ball-to-ball collisions exchange
  the normal component for equal masses; balls do not recolor each other.
- Ramps add ±3 to the corresponding velocity component per ball substep,
  with each component clamped to ±400 pixels/second.
- Drains shade a nearby ball within 42 pixels and attract it within 32.
  The recovered update is `v = 0.993v - 25*offset/distance²`. Ball size shrinks
  as it approaches the center; a mismatched non-gray capture ends the attempt.
- Ball scores are the original table at x86 address `0x01017008`:
  `[0, 200, 400, 800, 1600]`. The neighboring break-score table at `0x0101701c`
  contains 10 for every color. Gray holes and gray balls score zero.
- First queued launch waits 4,000 ms. Subsequent launches use the board's
  interval and wait if the selected launcher is occupied. The timer begins
  with the first launch, or immediately when there is no launch queue.
- Queue randomization performs the original whole-queue swap pass. The
  portable RNG reproduces the classic MSVCRT 15-bit `rand()` sequence for a
  given seed. Normal and bonus selection avoid their ten most recent boards.
- The board-completion phase converts remaining seconds into points, then
  waits 1.5 seconds before selecting the next board. Bonus selection occurs
  after every five completed boards (the sixth displayed board, then the
  eleventh, and so on).
- A loss resets score and spare walls and reloads the same board. New Game or
  changing difficulty chooses a fresh board. The high score survives.
- An extra placeable gray wall is awarded at each 1,600-point threshold. A
  right-click requires an empty floor tile, a spare wall and no overlapping
  ball.

Relevant original x86 functions include `CBall::UpdateObject` at
`0x01004c37`, `CBall::Deflect` at `0x01003c58`, ball collision at
`0x01004707`, ink reflection at `0x010048ad`, tile reflection at
`0x010049ab`, `BuildTileObject`, `CGameManager::LoadBoard` at
`0x0100aecb` and `CGameManager::PerformGameUpdate` at `0x0100b312`.
Addresses identify evidence in the supplied binary; they are not required
at runtime and no original code is loaded by address.

## Deliberate modernization and remaining differences

1. **Ink capture and hit testing.** Native mouse/pen events and polylines
   replace Tablet PC COM ink. Whole-stroke disappearance and reflection rules
   are reproduced. Version 1.1 uses a shared 10-pixel logical width, round caps
   and joins, and a five-pixel collision radius, matching the supplied original
   gameplay screenshot. Legacy stroke fitting, rasterization, hit tolerance,
   pressure, and sample timing cannot be claimed identical. Pen input relies
   on Windows mouse-event promotion; no physical pen device was available to
   test.
2. **Timing and arithmetic.** The original uses Windows multimedia timers
   and x87-era arithmetic. This implementation uses a performance-counter
   clock, bounded frame updates, double precision and portable C++. It avoids
   a large simulation jump after a long suspended frame. Exact trajectories
   can diverge near a boundary even when the same recovered rules are used.
3. **Rendering and the desktop.** GDI replaces DirectDraw. The original
   graphics, atlas coordinates, HUD and native-size geometry are retained.
   Resizing and per-monitor DPI support were added. Current Windows draws
   the window frame, menus and dialogs. The rolling queue and gold completion
   chase are recreated from the original behavior, with modern frame pacing.
   Version 1.1 composes the complete scaled client image offscreen and presents
   it with one copy, removing the exposed gray clear identified in the issue
   video. Ball transparency is prepared from the magenta key before scaling;
   the purple rectangles visible in the original-video reference are expressly
   excluded at the user's request. Original atlas bytes remain unchanged.
4. **Settings and integration.** High score and difficulty use a small
   per-user INI file. Old registry paths, Games Explorer registration,
   recovery hooks, licensing/OS integrations and remote-session restrictions
   are not reproduced. The game can run without the original EXE or installer.
5. **Help and localization.** Original menus and basic game-over strings are
   present in 35 languages. A local English Help dialog replaces the obsolete
   Windows Help target. About/diagnostics identify this reconstruction.
   The added board browser and its menu entry are English and use the original
   551 records. Its previews are separate from the active game; filtering and
   inspection do not alter the original level data.
6. **What was not established.** A side-by-side, frame-by-frame run against
   the original on native Windows was not possible. The original x64 game
   failed in Wine when a required Tablet PC COM class was unavailable. This
   prevents an evidence-based claim of perfect binary or timing equivalence.

These limits are specific to reconstruction and validation; the game is a
complete playable implementation with no placeholder engine functions.

## Toolchain references

- [Microsoft: Visual Studio 2026 release notes](https://learn.microsoft.com/en-us/visualstudio/releases/2026/release-notes)
- [Microsoft: MSVC Build Tools and v145](https://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-msvc)
- [Microsoft: Install C++ support in Visual Studio](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation)
- [Ghidra official project](https://github.com/NationalSecurityAgency/ghidra)
- [Annotated InkBall decompilation reference](https://github.com/TC999/inkballd)

The provenance and behavior claims above are based on the supplied file and
the included source/tests. Web references identify the tools and reference
project, not a substitute source for the recovered resource contents.
