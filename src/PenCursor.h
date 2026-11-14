#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace inkball {

// A DPI-scaled version of the white, diagonally outlined pen visible in the
// supplied original-gameplay recording. The hotspot is the pen's upper tip.
class PenCursor {
public:
    PenCursor() = default;
    ~PenCursor();
    PenCursor(const PenCursor&) = delete;
    PenCursor& operator=(const PenCursor&) = delete;
    void setDpi(UINT dpi);
    HCURSOR get() const { return cursor_; }

private:
    HCURSOR cursor_ = nullptr;
    UINT dpi_ = 0;
};
}
