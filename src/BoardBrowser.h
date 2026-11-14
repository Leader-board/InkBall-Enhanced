#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Levels.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace inkball {
// Caller pauses the active game before entry. Preview state is private. Cancel
// returns nullopt; Play returns the immutable collection index to load.
std::optional<std::size_t> ShowBoardBrowser(HWND owner, HINSTANCE instance,
    const std::vector<Level>& levels, int currentLevelId);
}
