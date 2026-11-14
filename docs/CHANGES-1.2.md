# InkBall 1.2 — responsiveness, pointer and board controls

This update starts from the attached `InkBall-VS2026-updated(1).zip` and the
available gameplay footage and screenshots. The source remains C++20 with
`InkBall.sln` targeting Visual Studio 2026. The prior fixes for the manifest,
gray repaint flash, ten-pixel rounded ink and purple ball backgrounds remain.

## Why the browser could feel slow

The paused game and its modal browser run on the same UI thread. Version 1.1
paused simulation but left the 15 ms game timer running. Each timer message
still rebuilt and invalidated the game image, including 289 tile draws,
while the browser was trying to handle dragging, selection and painting.
The browser also resized its child controls with immediate repaint calls and
rebuilt every list row and cell after changing filters or sorting. Selecting
a board synchronously rebuilt its preview inside the selection notification.

These are concrete sources of unnecessary work in the previous code. They
explain a responsiveness problem without assuming that the exact multi-second
delay reported on the user's Windows desktop was reproduced in the test
environment. See `VALIDATION.md` for measured evidence and its limits.

Version 1.2 changes that work as follows:

- Stops the main game's timer while paused, inactive or in a modal window.
  An already queued timer message is ignored after stopping.
- Renders the latest game state during painting, coalescing multiple input
  or timer updates into the frame Windows actually requests.
- Caches board tiles and redraws only cells whose sprite changes, such as
  animated gates or broken/placed walls. The cached board pixels are copied
  into the frame before ink and moving balls are drawn.
- Uses a virtual board list with cached cell text, so filtering and sorting
  update the row mapping rather than resending all 551 rows and their cells.
- Batches browser child-window placement, skips unchanged layouts and column
  widths, and queues the latest preview request when selection changes rapidly.

The preview still uses a private game instance. Browsing does not advance
the active game or alter its random sequence, queue, score or mechanics.

## Pen-shaped drawing pointer

The plus-shaped drawing cursor is replaced with a white, dark-outlined pen
recreated from the supplied original-gameplay footage. Its tip is the
hotspot. The shape is rendered for the current display DPI, while menus,
the HUD and margins use the normal arrow.

This is a reconstruction from the visual reference, not a claim that an
original cursor resource was extracted during this update. No new extraction
of the original installer or game EXE is claimed.

## Current-board mechanics

**Game > Board Mechanics** or **Ctrl+M** opens a native settings window. It
shows current and original values for seven controls:

| Control | Range | Original/default behavior |
| --- | --- | --- |
| Time limit | 0–999 seconds | Original board limit; zero means unlimited |
| Launch interval | 0–60 seconds | Original board interval; first queued launch remains four seconds |
| Minimum initial speed | 1–1000 px/s | Original board lower bound |
| Maximum initial speed | 1–1000 px/s, not below minimum | Original board upper bound; equal bounds fix the speed |
| Ball speed multiplier | 0.1–4× | 1× |
| Chevron boost | 0–20× | 1× preserves the original directional increment of 3 px/s per contact step |
| Chevron speed limit | 1–2000 px/s along the arrow's axis | 400 px/s |

Initial speeds use the original random selection rule, with an exclusive
upper bound when the bounds differ. They affect future launches and
preplaced balls after restarting. Changing the ball multiplier rescales
already moving balls by the ratio of the new and old values, so repeated
Apply does not repeatedly multiply their speeds. The initial-speed product
and rescaled live velocity components are limited to 2000 px/s.

Changing the configured time limit resets remaining time. Applying an
unchanged limit preserves the countdown. Changing the launch interval after
launching has begun resets the pending interval; the original first-launch
delay remains intact. A zero chevron boost removes acceleration while its
configured speed cap still applies on contact.

**Apply** updates the board immediately and leaves the dialog open. **OK**
applies and closes. **Cancel** discards unapplied edits; values already
applied remain. **Restore Original Defaults** fills the form and waits for
Apply or OK. Invalid values are rejected before mutating any game state.

Positions, tiles, score, ink, pause and the current phase are preserved when
applying settings. Settings do not revive a lost board; restart is separate.
Overrides persist through a restart of this board, including after a loss.
New Game, choosing a board to play, automatic progression or application
exit discards them. The original level records remain unchanged.

## Restart and frame rate

**Game > Restart Current Board** or **Ctrl+R** restores this board's original
layout and queue while keeping its current mechanics. Score, blocks, ink and
time reset. High score and manual pause are retained. Queue randomization
uses the board's shuffle flag again.

**Game > Show FPS** toggles a title-bar display of completed game paints per
second, measured over roughly one-second intervals. It shows "paused" while
the game is paused and does not keep a background render loop running just
to maintain the counter. Exposure repaint events count; this is an application
paint measurement rather than a monitor-refresh or physics-step measurement.
The Show FPS preference is saved alongside the existing high score and
difficulty. Mechanics overrides remain session-only.

## Score rendering stays unchanged

The original digit cells, score colors, HUD layout and scaling algorithm are
unchanged. FPS is shown in the title bar. The user's score screenshot was
reviewed to explain the pixelation, and possible future approaches are
described in [SCORE_RENDERING.md](SCORE_RENDERING.md). None is implemented in
this release.

## Validation boundary

The source and exact executable's build, regression and UI results are in
[VALIDATION.md](VALIDATION.md) and `BUILD_VERIFICATION.json`. The current
environment can cross-compile Windows executables and run them under Wine;
a native Windows desktop and Visual Studio 2026/MSVC execution were not
available. Native Windows performance or a native MSVC build is not claimed
as a performed test.
