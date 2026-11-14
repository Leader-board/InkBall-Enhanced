# InkBall 1.1 — requested fixes and board browser

This update is based on the supplied `InkBall-VS2026(1).zip`, the issue
recording, the original-gameplay recording, and the original-game screenshot.
The source remains C++20 with a Visual Studio 2026 solution and a standalone
Windows executable.

## Manifest build correction

Removed `1 RT_MANIFEST "../assets/app.manifest"` from
`resources/InkBall.rc`. The Visual Studio application project now enables
manifest generation and embedding, and passes `assets/app.manifest` to the
Manifest Tool through `AdditionalManifestFiles`. That gives MSVC one owner
for the final manifest, while retaining DPI awareness and current common
controls. The former `/MANIFEST:NO` CMake option is also removed.

The CMake MSVC build accepts the manifest as a target source. MinGW, which
does not have the MSVC manifest-tool step, gets a separate generated resource
only in its own build directory. That path is excluded from the Visual Studio
project and does not restore the removed resource declaration.

Microsoft documents these project settings in
[Manifest Tool property pages](https://learn.microsoft.com/en-us/cpp/build/reference/manifest-tool-property-pages)
and [Manifest generation for C/C++](https://learn.microsoft.com/en-us/cpp/build/understanding-manifest-generation-for-c-cpp-programs).
The supplied projects were structurally checked and the Windows code was
cross-compiled; an actual Visual Studio/MSVC execution was unavailable.

## Board browser

**Game > Browse Boards** or **Ctrl+B** opens a resizable native window with
all 551 original records. Difficulty and normal/bonus filters can be combined.
Allowed time sorts numerically in either direction; unlimited time sorts as
infinity and equal times use ascending original board IDs. The list's Time
allowed header toggles direction, and the ID header restores ID order.

The selected board has a static preview and a scrollable parameter report:
time, launch interval, initial speed bounds, queue, shuffle flag, preplaced
balls, tile counts, resource offset/size and preserved trailing bytes. Click
a preview tile to inspect its raw code, coordinates, decoded parameters and
breakable-wall underlay. The preview shows stored queue order and identifies
whether playing shuffles it.

The browser uses a private game instance and does not advance the active
game. Close/Escape preserves the current game and manual pause. Play Selected
Board, Enter or double-click begins the selected board with a fresh score
and earned-wall count, while keeping the high score. Selection uses the
original collection index, not the current sorted row number.

## Gray flash and apparent shaking

The first recording contains one full gray game-client frame at about
0.433 seconds. The title bar and menu remain present. Static-board comparison
does not establish whole-board movement in the other frames. The former
presentation code cleared the visible window gray before stretching the
rendered board onto it, exposing an intermediate operation consistent with
that captured flash.

The updated renderer composes the complete client image, including margins
and scaling, in a persistent offscreen buffer. It presents that finished
image with one 1:1 `BitBlt`. Window background erasure remains suppressed.
The presentation surface is recreated only when its size changes, and
partial repaints copy the same already-scaled pixels as a full repaint.

## Ink thickness and collisions

The original screenshot shows approximately 10-pixel ink at the native
544-pixel board width. The former two-pixel pen is replaced with a shared
10-logical-pixel width, rounded caps and rounded joins. Single-point strokes
are matching round dots. Window/DPI scaling scales the complete image, so
the width remains consistent with the board.

The engine uses the corresponding five-pixel stroke radius. Each candidate
stroke must intersect the ball's radius plus the ink radius before the
existing integer-perimeter reflection samples can select it. This keeps
the established sampling tolerance from hitting another nearby stroke
outside the visible edge. A struck stroke still disappears in its entirety.

## Balls without purple rectangles

The original-gameplay recording contains magenta/purple backgrounds around
balls. These are deliberately removed as requested. The renderer converts
the atlas's exact magenta key to transparent, zero-color alpha pixels once,
before ball shrinking or window scaling, and composites the balls using
premultiplied alpha. Opaque sprite pixels remain unchanged. HUD ball sprites
retain their original black queue background.

The embedded sprite bitmap, all 551 boards and icon remain byte-identical
to the supplied project's recovered originals. The alpha conversion exists
only in the renderer's working copy.

## Validation

The package includes 617 engine checks, 38 catalog checks and 75 Windows
renderer checks. The new cases cover thick-edge and round-endpoint hits and
misses, actual filter/sort behavior, raw tile parameters, all five ball
colors at normal and near-hole sizes, clean scaled transparency, clipped
repaints, repeated resizing and pointer alignment. The running GUI was also
checked with the board browser, drawing and a frame capture.

See [VALIDATION.md](VALIDATION.md) for the current binary identity, exact
completed checks and platform limitations.
