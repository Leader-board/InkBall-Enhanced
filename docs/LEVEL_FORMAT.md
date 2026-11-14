# InkBall resource format and recovered engine behavior

This document describes the data and behavior recovered from the InkBall binaries in the supplied `HoldEm-InkBall-Tinker-Setup.exe`. It accompanies the new C++ implementation; it is not a claim that Microsoft's original source project was recovered.

## Original assets

Both the x86 and x64 executables contain identical level and bitmap resources:

| Resource | Size | SHA-256 |
| --- | ---: | --- |
| `IKL`, ID 20001, language 1033 | 334,980 bytes | `b2f262d56bb12340c3a4375dfc19a0056de39dff45da6ec5d86e676eb828175b` |
| `RT_BITMAP`, ID 501, language 1033, DIB body | 457,640 bytes | `3d4620599b3d0ac4d0e8d6a7163b7b9b401aede823fd4e55dd475eab8e7dd98a` |

`assets/levels.ikl` retains the complete original level resource. The BMP asset has a standard 14-byte BMP file header prepended to the unchanged original DIB. The original sprite atlas is 381 × 400 pixels. Its transparent color is magenta (`#FF00FF`).

## Level records

The IKL resource has 551 consecutive records and a final zero byte. Each record begins with a **big-endian 16-bit payload length**. The length excludes its own two bytes. All multibyte values inside a record are also big-endian.

| Payload offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 2 | Time limit in seconds; zero means unlimited; otherwise 1–999 |
| 2 | 1 | Interval between queued ball launches, in seconds; 0–60 |
| 3 | 2 | Minimum initial ball speed, in pixels/second; 50–300 |
| 5 | 2 | Maximum initial ball speed, in pixels/second; 50–300 |
| 7 | 1 | Nonzero shuffles queued ball colors |
| 8 | 1 | Number of queued balls; maximum 64 |
| 9 | Queue count | Color bytes: gray 0, red 1, blue 2, green 3, gold 4 |
| 9 + queue count | Variable | 17 × 17 row-major tile descriptions |

A tile normally occupies two bytes. A type-4 breakable wall consumes another two bytes describing the tile revealed when it breaks. The original resources use floor, hole quadrant, ramp and color-filter tiles as replacements.

The original parser stops after 289 cells. Fourteen records contain additional bytes that the original loader ignores. The new parser preserves them in `Level::unusedTrailingBytes` and also leaves the original resource untouched. Their zero-based IDs are 83, 169, 173, 199, 208, 213, 267, 277, 282, 285, 288, 381, 399 and 430.

## Difficulty collections

The executable stores collection tables separately from IKL. Each entry is a little-endian 32-bit resource offset followed by a 256-character UTF-16 name. In this supplied version the names are decimal strings of the zero-based record IDs.

| Difficulty | Normal IDs | Normal count | Bonus IDs | Bonus count |
| --- | --- | ---: | --- | ---: |
| Beginner | 0–137 | 138 | 138–185 | 48 |
| Novice | 186–263 | 78 | 264–291 | 28 |
| Intermediate | 292–368 | 77 | 369–394 | 26 |
| Advanced | 395–467 | 73 | 468–488 | 21 |
| Expert | 489–533 | 45 | 534–550 | 17 |

The first board is chosen from the normal collection. Subsequent boards use the bonus collection whenever the completed-board counter is divisible by five: the sixth, eleventh, sixteenth displayed boards, and so on. Separate histories exclude each collection's ten most recent choices. Starting a new game with F2 keeps those histories; changing difficulty clears them.

## Tile encoding

For a 16-bit code `t`, let `kind = t >> 12`, `p2 = (t >> 8) & 15`, `p1 = (t >> 4) & 15`, and `p0 = t & 15`.

| Kind | Meaning | Parameters | Original sprite index |
| ---: | --- | --- | --- |
| 0 | Floor | Remaining bits ignored | 0 |
| 1 | Ball launcher | Remaining bits ignored | 1 |
| 2 | One quadrant of a hole | `p0` color, `p1` quadrant 0–3 | `17 + 4*p0 + p1` |
| 3 | Solid or color-changing wall | `p0` color; gray does not repaint | `37 + p0` |
| 4 | Breakable wall | `p0` color; next word is its replacement | `42 + p0` |
| 5 | One-way wall or one-way color filter | `p0` color, `p1` direction | `47 + 5*p1 + p0` |
| 6 | Timed gray wall | `p0` initial raised frame 0–4; `p1` open time; `p2` closed time | `77 + 5*frame` |
| 7 | Colored keyhole wall | `p0` color, `p1` initial raised frame 0–4 | `77 + 5*frame + p0` |
| 8 | Ramp | `p1` direction | `102 + p1` |
| 9 | Preplaced moving ball on floor | `p0` color; `(t >> 4) & 255` heading | Floor 0; ball `2 + p0` |
| 10 | Two-way color filter | `p0` color; `p1` 0 horizontal, 1 vertical | `67 + 5*p1 + p0` |

Directions for kinds 5 and 8 are **0 up, 1 down, 2 left, 3 right**. Preplaced ball headings instead use 32 steps around the compass: heading 0 is up, 8 right, 16 down and 24 left; each increment is 11.25 degrees. Heading 255 requests a random direction.

The hole quadrants are 0 upper-left, 1 upper-right, 2 lower-left and 3 lower-right. Four 32-pixel cells form a 64-pixel hole with a shared center. Treating each quadrant as a separate centered hole gives incorrect gameplay.

## Rules retained in the C++ engine

### Balls and collision handling

The board is 544 × 544 pixels, made from 17 × 17 tiles of 32 pixels. The original HUD adds 42 pixels above it. Normal moving balls are 24 pixels across; the HUD uses separate 18-pixel previews.

Initial speed is the minimum plus `rand() % (maximum-minimum)`, except that equal limits produce the fixed speed without drawing a random number. Random headings choose the absolute X component as `rand()%100 / 100.0 * speed`, derive the Y component with a square root, and choose the component signs independently. The reconstruction implements the same 15-bit MSVCRT random-number sequence and the original queue-shuffling procedure.

The original subdivides moving-object updates into steps of at most four milliseconds. Its collision outline consists of 32 integer points spaced by 11.25 degrees. Deflection examines the 17 points on the forward half of that outline. The first and last colliding sample determine the normal used to reflect velocity. The new engine retains this sampling model for walls and ink. Equal-mass ball collisions exchange the velocity components along the line joining the centers.

### Holes and ramps

Hole interaction selects the nearest overlapping quadrant. At a center distance of 42 pixels or less the ball uses the darker hole sprite. Within 32 pixels, each physics step applies the original attraction and damping:

```text
vx = 0.993 * vx - 25 * dx / distance²
vy = 0.993 * vy - 25 * dy / distance²
tallness = truncate(-24 / distance)
radius = 12 + tallness
```

The ball sinks when its distance to the hole center plus its current radius is at most 12 pixels. Matching colored holes award gray 0, red 200, blue 400, green 800 or gold 1600 points. A gray ball may enter any hole, and a gray hole may accept any ball, for zero points. A non-gray mismatch ends the game.

A shrunken ball that escapes onto a floor or launcher regains its normal 12-pixel radius. This matters for partly exposed holes underneath breakable walls; their four quadrants can be uncovered independently.

Ramps add or subtract three units from one velocity component per physics substep. The affected component is limited to ±400 pixels/second. A ramp does not replace the other component or abruptly set a new heading.

### Walls

Colored solid walls repaint balls on contact; gray solid walls retain the ball's color. Breakable walls reflect balls and reveal their stored replacement when their color matches, or whenever the wall is gray. Each destroyed wall awards ten points.

One-way walls block travel against their arrows. Colored variants also block other ball colors. Two-way color filters pass matching balls and deflect other colors. Their collision region is the central strip, rather than the full square.

Timed walls open for `(p1+1)*0.5` seconds and close for `(p2+1)*0.5` seconds. Raising and lowering use frames 0–4, spaced by just over 50 milliseconds. A rising wall waits if a ball occupies its tile. Colored keyhole walls toggle their persistent raised/retracted state when a non-gray matching ball enters a matching hole. Merely touching a keyhole wall does not trigger it.

### Queue, timer, scoring and failure

The first queued launch waits four seconds. Subsequent launches use the level's interval. The timer starts when the first queued ball launches, or immediately when a board has only preplaced balls. A launcher waits when its tile is occupied.

Each 1600 points grants a solid gray wall that can be placed on an empty, unoccupied floor tile. The score carries across boards. Clearing a board converts each remaining second, including a fractional final second, to one point before the next board starts. The original counter animation awards one point per approximately 20 milliseconds and then waits 1.5 seconds.

After the loss dialog, the original reloads the same board and resets the score and earned walls. `Game::restartLevel()` implements that behavior. The high score remains available. F2 instead chooses another normal board.

## Fidelity limits

The game logic is new compilable C++ reconstructed from the supplied binary's data and observable algorithms. It is not a byte-identical recompilation. Modern mouse/pen input produces polylines instead of using the obsolete Tablet PC Ink COM implementation, so stroke interpolation, pressure handling and exact hit boundaries can differ. The reconstruction processes ordinary elapsed-time updates and bounds physics steps; it does not reproduce the original program's unusual `%66` elapsed-time calculation or timing stalls. Floating-point evaluation also differs from the x86 original's x87 execution. Native window chrome and DPI behavior belong to the current Windows environment.

## Evidence and reference points

These are virtual addresses in the supplied **x86** `inkball_1.exe`, whose image base is `0x01000000`:

| Address | Evidence |
| --- | --- |
| `0x01005CCB` | Difficulty selection, collection counts and pointers |
| `0x0100AECB` | Level payload decoding and two-word breakable tiles |
| `0x01008F80` | Board dimensions, HUD height, ball size, atlas dimensions and transparency |
| `0x01017008` | Drain score table: 0, 200, 400, 800, 1600 |
| `0x0101701C` | Break score table: 10 for every ball color |
| `0x01005278` | Initial ball speed and direction |
| `0x0100630D` | Hole quadrants, attraction, shrinking, matching and keyhole toggle |
| `0x0100601F`, `0x01005FDD` | Floor/launcher collision participation and recovery of a shrunken ball's size |
| `0x0100626D` | Ramp acceleration |
| `0x01006740` | Colored keyhole wall initialization |
| `0x01006A9E` | Persistent keyhole state toggle |
| `0x0100B312` | End-of-level time bonus and progression |
| `0x01010389` | Failure message, score reset and same-board reload |

The [archived Microsoft InkBall help](https://web.archive.org/web/20070214130041/http://windowshelp.microsoft.com/Windows/en-US/help/c209fb0d-530b-43ce-954b-7c91aeca00131033.mspx) independently confirms the scoring colors, neutral gray behavior, single-hit ink strokes, mouse/pen controls and pause behavior.

The incomplete [TC999/inkballd decompilation](https://github.com/TC999/inkballd), commit `cbcee4fe44f00504e38bc2b76ddd93656e591fd6`, provided useful symbol names and an atlas-rectangle reference. Its resources were checked against the supplied executables and matched exactly. Its existing stubs were not used as a runnable game. Relevant recovered behavior was checked against the supplied executable and independent disassembly/decompilation. Its MIT notice is retained in `third_party/inkballd-LICENSE.txt`.
