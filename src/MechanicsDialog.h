#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Game.h"

namespace inkball {
// The caller pauses the game for the entire modal interaction. A successful
// Apply is retained even if later edits are cancelled. Returns true if any
// settings were applied; the caller can then refresh the main game image.
bool ShowMechanicsDialog(HWND owner, HINSTANCE instance, Game& game);
}
