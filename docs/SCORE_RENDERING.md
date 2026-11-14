# Score pixelation — explanation only

**No score/HUD rendering changes are made in version 1.2.** The original
digit artwork, spacing, colors, HUD layout and canvas scaling remain as in
version 1.1. The FPS feature draws its text in the window title.

## Why the digits look pixelated

The score is assembled from the original bitmap atlas. In `SpriteRects.h`,
the white score digits and green high-score digits occupy **9 × 19 pixel
cells**; the red timer uses cells of the same size. These cells include
background/spacing as well as the painted glyph. `Renderer::number` advances
nine logical pixels for each digit, and `drawHud` places the cells on the
original **544 × 586** game canvas.

The complete canvas is then scaled to the window using GDI's `COLORONCOLOR`
stretch mode. Enlarging a small bitmap makes its original pixel steps more
visible. At a fractional scale, such as 125% or 150%, some source pixels
must occupy different numbers of screen pixels, which can make small strokes
and spacing look less even. The supplied screenshot is consistent with this
bitmap enlargement; it alone does not establish which window size or Windows
DPI setting produced the scale.

Microsoft documents the available bitmap stretch modes in
[SetStretchBltMode](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setstretchbltmode).
The low-resolution source cells and this project's composition/scaling path
are directly inspectable in `src/SpriteRects.h` and `src/Renderer.cpp`.

## Could it be improved later?

Yes. The choice depends on how closely the display should preserve the
original pixel artwork. These are future design options, not controls added
to this release.

| Possible approach | Benefit | Tradeoff |
| --- | --- | --- |
| Keep native or whole-number canvas scales | Preserves even pixel blocks and the exact original artwork | Digits remain pixel art; some window sizes require more unused margin |
| Smooth bitmap scaling, applied to the HUD or digit layer | Can soften visible steps without replacing the source artwork | Can blur small digits; cannot recover detail absent from a 9 × 19 cell; applying it to the whole canvas would also soften tiles and ink |
| Draw a separate score layer from vector digits or a carefully matched outline font at the final display DPI | Can produce crisp, antialiased text at different sizes and keep the board artwork unchanged | Changes the old raster appearance; matching the original glyph shapes, weight, spacing and glow requires deliberate visual work |

For a bitmap-based approach, GDI's `HALFTONE` mode is one available
higher-quality resampling option; Microsoft notes that it costs more
processing and requires the brush origin to be set correctly. Whether it
looks better for these very small digits would need visual comparison at
several display scales.

For scalable text, [DirectWrite](https://learn.microsoft.com/en-us/windows/win32/directwrite/direct-write-portal)
supports resolution-independent outline fonts and high-quality text
rendering. A future implementation could draw the score after scaling the
board, using matching custom digit outlines or a selected font. A standard
font would alter the original appearance, so this should be an explicit
visual choice.

The current release retains the existing output, as requested.
