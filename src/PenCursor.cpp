#include "PenCursor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace inkball {
namespace {
struct Point { double x, y; };
constexpr std::array<Point, 5> Outline{{{1, 1}, {8, 4}, {22, 18}, {18, 22}, {4, 8}}};

double distance(Point p, Point a, Point b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) /
        (dx * dx + dy * dy), 0.0, 1.0);
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}
bool inside(Point point) {
    for (std::size_t i = 0; i < Outline.size(); ++i) {
        const auto a = Outline[i], b = Outline[(i + 1) % Outline.size()];
        if ((b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x) < 0)
            return false;
    }
    return true;
}
}

PenCursor::~PenCursor() { if (cursor_) DestroyCursor(cursor_); }

void PenCursor::setDpi(UINT dpi) {
    dpi = std::clamp(dpi ? dpi : 96u, 96u, 768u);
    if (cursor_ && dpi_ == dpi) return;
    const int size = MulDiv(32, static_cast<int>(dpi), 96);
    const double scale = static_cast<double>(size) / 32.0;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!color || !pixels) throw std::runtime_error("Windows could not create the pen cursor.");

    // Supersample this small code-defined shape; there is no raster asset to
    // enlarge. Store premultiplied BGRA for native alpha cursor compositing.
    auto* output = static_cast<std::uint32_t*>(pixels);
    constexpr int Samples = 4, SampleCount = Samples * Samples;
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        unsigned covered = 0, white = 0;
        for (int sy = 0; sy < Samples; ++sy) for (int sx = 0; sx < Samples; ++sx) {
            const Point point{(x + (sx + 0.5) / Samples) / scale,
                (y + (sy + 0.5) / Samples) / scale};
            if (!inside(point)) continue;
            ++covered;
            double edge = distance(point, {16, 20}, {20, 16});
            for (std::size_t i = 0; i < Outline.size(); ++i)
                edge = std::min(edge, distance(point, Outline[i], Outline[(i + 1) % Outline.size()]));
            if (edge > 0.65) ++white;
        }
        const std::uint32_t alpha = covered * 255u / SampleCount;
        const std::uint32_t light = white * 255u / SampleCount;
        output[y * size + x] = (alpha << 24) | (light << 16) | (light << 8) | light;
    }
    const int maskStride = ((size + 15) / 16) * 2;
    std::vector<std::uint8_t> maskBits(static_cast<std::size_t>(maskStride * size), 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    if (!mask) {
        DeleteObject(color);
        throw std::runtime_error("Windows could not create the pen cursor mask.");
    }
    ICONINFO cursorInfo{};
    cursorInfo.fIcon = FALSE;
    cursorInfo.xHotspot = static_cast<DWORD>(std::lround(scale));
    cursorInfo.yHotspot = static_cast<DWORD>(std::lround(scale));
    cursorInfo.hbmColor = color;
    cursorInfo.hbmMask = mask;
    HCURSOR replacement = static_cast<HCURSOR>(CreateIconIndirect(&cursorInfo));
    DeleteObject(mask);
    DeleteObject(color);
    if (!replacement) throw std::runtime_error("Windows could not create the pen pointer.");
    if (cursor_) {
        // Do not leave a currently selected cursor referring to a freed handle.
        if (GetCursor() == cursor_) SetCursor(replacement);
        DestroyCursor(cursor_);
    }
    cursor_ = replacement;
    dpi_ = dpi;
}
}
