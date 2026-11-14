InkBall x64 prebuilt binaries

inkball.exe is the complete game, compiled from this package with MinGW-w64.
It embeds the original levels, graphics, icon and localized menus.

From the package root, run the included native regression tests with:
  bin\Prebuilt-x64\InkBall.EngineTests.exe assets\levels.ikl
  bin\Prebuilt-x64\InkBall.CatalogTests.exe assets\levels.ikl
  bin\Prebuilt-x64\InkBall.RendererTests.exe

Version 1.1: Ctrl+B opens the new board browser. This release also fixes the
gray repaint flash, uses 10-pixel rounded ink with matching collisions, and
renders balls without the purple background rectangles in the reference video.

For a Microsoft compiler build, open InkBall.sln in Visual Studio 2026.
See README.md and docs/VALIDATION.md for requirements and validation details.
