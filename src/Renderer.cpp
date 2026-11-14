#include "Renderer.h"
#include "SpriteRects.h"
#include "resource.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace inkball {
namespace {
int GoldSprite(int index) {
    if (index >= 17 && index <= 36) return 33 + (index - 17) % 4;
    if (index >= 42 && index <= 46) return 46;
    return 41;
}
}
Renderer::~Renderer() {
    if (atlasDc_ && oldAtlasBitmap_) SelectObject(atlasDc_, oldAtlasBitmap_);
    if (alphaDc_ && oldAlphaBitmap_) SelectObject(alphaDc_, oldAlphaBitmap_);
    if (frameDc_ && oldFrameBitmap_) SelectObject(frameDc_, oldFrameBitmap_);
    if (boardDc_ && oldBoardBitmap_) SelectObject(boardDc_, oldBoardBitmap_);
    if (presentationDc_ && oldPresentationBitmap_) SelectObject(presentationDc_, oldPresentationBitmap_);
    if (atlasBitmap_) DeleteObject(atlasBitmap_);
    if (alphaBitmap_) DeleteObject(alphaBitmap_);
    if (frameBitmap_) DeleteObject(frameBitmap_);
    if (boardBitmap_) DeleteObject(boardBitmap_);
    if (presentationBitmap_) DeleteObject(presentationBitmap_);
    if (atlasDc_) DeleteDC(atlasDc_);
    if (alphaDc_) DeleteDC(alphaDc_);
    if (frameDc_) DeleteDC(frameDc_);
    if (boardDc_) DeleteDC(boardDc_);
    if (presentationDc_) DeleteDC(presentationDc_);
    if (inkPen_) DeleteObject(inkPen_);
}

void Renderer::initialize(HINSTANCE instance) {
    atlasBitmap_ = static_cast<HBITMAP>(LoadImageW(instance,
        MAKEINTRESOURCEW(IDB_SPRITES), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (!atlasBitmap_) throw std::runtime_error("The embedded original sprite atlas could not be loaded.");
    BITMAP atlas{};
    if (!GetObjectW(atlasBitmap_, sizeof(atlas), &atlas) ||
        atlas.bmWidth != 381 || atlas.bmHeight != 400)
        throw std::runtime_error("The embedded sprite atlas has unexpected dimensions.");
    atlasDc_ = CreateCompatibleDC(nullptr);
    alphaDc_ = CreateCompatibleDC(nullptr);
    frameDc_ = CreateCompatibleDC(nullptr);
    boardDc_ = CreateCompatibleDC(nullptr);
    presentationDc_ = CreateCompatibleDC(nullptr);
    if (!atlasDc_ || !alphaDc_ || !frameDc_ || !boardDc_ || !presentationDc_)
        throw std::runtime_error("Windows could not allocate the drawing surfaces.");

    // Convert the atlas's magenta key to premultiplied alpha once, BEFORE any
    // shrinking or window scaling. Transparent pixels have zero RGB as well
    // as zero alpha, so a ball can never carry a purple rectangular backdrop.
    // Opaque artwork is copied exactly; the embedded original BMP is unchanged.
    BITMAPINFO alphaInfo{};
    alphaInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    alphaInfo.bmiHeader.biWidth = atlas.bmWidth;
    alphaInfo.bmiHeader.biHeight = -atlas.bmHeight;
    alphaInfo.bmiHeader.biPlanes = 1;
    alphaInfo.bmiHeader.biBitCount = 32;
    alphaInfo.bmiHeader.biCompression = BI_RGB;
    void* alphaPixels = nullptr;
    alphaBitmap_ = CreateDIBSection(alphaDc_, &alphaInfo, DIB_RGB_COLORS,
        &alphaPixels, nullptr, 0);
    if (!alphaBitmap_ || !alphaPixels ||
        GetDIBits(atlasDc_, atlasBitmap_, 0, static_cast<UINT>(atlas.bmHeight),
            alphaPixels, &alphaInfo, DIB_RGB_COLORS) != atlas.bmHeight)
        throw std::runtime_error("Windows could not prepare transparent ball artwork.");
    auto* pixels = static_cast<std::uint32_t*>(alphaPixels);
    for (int i = 0; i < atlas.bmWidth * atlas.bmHeight; ++i) {
        const std::uint32_t color = pixels[i] & 0x00ffffffu;
        pixels[i] = color == 0x00ff00ffu ? 0u : color | 0xff000000u;
    }
    oldAlphaBitmap_ = SelectObject(alphaDc_, alphaBitmap_);
    oldAtlasBitmap_ = SelectObject(atlasDc_, atlasBitmap_);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = Width;
    info.bmiHeader.biHeight = -Height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    frameBitmap_ = CreateDIBSection(frameDc_, &info, DIB_RGB_COLORS,
        &framePixels_, nullptr, 0);
    if (!frameBitmap_ || !framePixels_)
        throw std::runtime_error("Windows could not allocate the game image.");
    oldFrameBitmap_ = SelectObject(frameDc_, frameBitmap_);
    info.bmiHeader.biHeight = -BoardPixels;
    void* boardPixels = nullptr;
    boardBitmap_ = CreateDIBSection(boardDc_, &info, DIB_RGB_COLORS,
        &boardPixels, nullptr, 0);
    if (!boardBitmap_ || !boardPixels)
        throw std::runtime_error("Windows could not allocate the board background.");
    oldBoardBitmap_ = SelectObject(boardDc_, boardBitmap_);
    const LOGBRUSH inkBrush{BS_SOLID, RGB(0, 0, 0), 0};
    inkPen_ = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        static_cast<DWORD>(InkWidth), &inkBrush, 0, nullptr);
    if (!inkPen_) throw std::runtime_error("Windows could not create the ink pen.");
}

void Renderer::sprite(int index, int x, int y, bool transparent, int width, int height) const {
    spriteTo(frameDc_, index, x, y, transparent, width, height);
}

void Renderer::spriteTo(HDC target, int index, int x, int y, bool transparent, int width, int height) const {
    if (index < 0 || index >= static_cast<int>(std::size(kSpriteRects))) return;
    const auto& r = kSpriteRects[index];
    const int sourceWidth = r.right - r.left, sourceHeight = r.bottom - r.top;
    if (sourceWidth <= 0 || sourceHeight <= 0) return;
    if (!width) width = sourceWidth;
    if (!height) height = sourceHeight;
    if (transparent) {
        constexpr BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(target, x, y, width, height, alphaDc_, r.left, r.top,
            sourceWidth, sourceHeight, blend);
    } else if (width == sourceWidth && height == sourceHeight) {
        BitBlt(target, x, y, width, height, atlasDc_, r.left, r.top, SRCCOPY);
    } else {
        SetStretchBltMode(target, COLORONCOLOR);
        StretchBlt(target, x, y, width, height, atlasDc_, r.left, r.top,
            sourceWidth, sourceHeight, SRCCOPY);
    }
}

int Renderer::number(std::uint64_t value, int right, int y, int digitBase) const {
    do {
        right -= 9;
        sprite(digitBase + static_cast<int>(value % 10), right, y);
        value /= 10;
    } while (value);
    return right;
}

void Renderer::updateAnimations(const GameState& game) {
    if (game.levelSerial != lastLevelSerial_) {
        lastLevelSerial_ = game.levelSerial;
        lastElapsed_ = queueAccumulator_ = 0;
        for (int i = 0; i < 6; ++i) {
            queuePositions_[i] = 104.0 + i * 18;
            queueSpeeds_[i] = 0;
        }
        previousQueueSize_ = game.queue.size();
        previousPhase_ = Phase::Playing;
        completionBonusSeconds_ = 0;
    }
    if (game.queue.size() < previousQueueSize_) {
        const auto removed = std::min<std::size_t>(6, previousQueueSize_ - game.queue.size());
        for (std::size_t count = 0; count < removed; ++count) {
            for (int i = 0; i < 5; ++i) {
                queuePositions_[i] = queuePositions_[i + 1];
                queueSpeeds_[i] = queueSpeeds_[i + 1];
            }
            queuePositions_[5] = std::max(104.0, queuePositions_[4] + 18);
            queueSpeeds_[5] = 0;
        }
    }
    previousQueueSize_ = game.queue.size();
    const double elapsed = std::max(0.0, game.elapsed - lastElapsed_);
    lastElapsed_ = game.elapsed;
    queueAccumulator_ += elapsed;
    // Original nominal update interval: 15 ms. The head accelerates by six
    // and the followers by five down to one, not their increasing slot index.
    // Accumulate simulation time so coalesced paints do not slow the queue.
    constexpr double queueStep = 0.015;
    while (queueAccumulator_ + 1e-12 >= queueStep) {
        queueAccumulator_ -= queueStep;
        if (game.queue.empty()) continue;
        if (queuePositions_[0] > 6) {
            queueSpeeds_[0] -= 6;
            queuePositions_[0] += queueSpeeds_[0] * queueStep;
        } else { queuePositions_[0] = 6; queueSpeeds_[0] = 0; }
        for (int i = 1; i < 6; ++i) {
            const double target = queuePositions_[i - 1] + 18;
            if (queuePositions_[i] > target) {
                queueSpeeds_[i] -= 6 - i;
                queuePositions_[i] += queueSpeeds_[i] * queueStep;
            } else { queuePositions_[i] = target; queueSpeeds_[i] = 0; }
        }
    }
    if (game.phase == Phase::LevelComplete && previousPhase_ != Phase::LevelComplete)
        completionBonusSeconds_ = game.unlimitedTime ? 0 :
            static_cast<int>(std::ceil(game.timeRemaining) + std::floor(game.celebrationElapsed / 0.020));
    previousPhase_ = game.phase;
}

void Renderer::drawHud(const GameState& game) const {
    sprite(106, 0, 0);
    sprite(107, 104, 0);
    sprite(108, 445, 0);
    sprite(109, 486, 0);
    const int clip = SaveDC(frameDc_);
    IntersectClipRect(frameDc_, 5, 7, 104, 35);
    for (std::size_t i = 0; i < std::min<std::size_t>(game.queue.size(), 6); ++i) {
        if (i == 0 && game.launcherBlink) continue;
        sprite(12 + std::clamp(game.queue[i], 0, 4), static_cast<int>(queuePositions_[i]), 14);
    }
    RestoreDC(frameDc_, clip);

    // The original green high score is rightmost. Black edge sprites preserve
    // the exact rounded counter ends from the extracted atlas.
    int x = 104 + 341 - 12;
    sprite(133, x, 13);
    x = number(game.highScore, x, 13, 123) - 9;
    sprite(122, x, 13);
    x -= 20;
    sprite(121, x, 13);
    x = number(game.score, x, 13, 111) - 9;
    sprite(110, x, 13);

    if (game.spareBlocks > 99) sprite(144, 445 + 14, 18);
    else number(static_cast<std::uint64_t>(std::max(0, game.spareBlocks)), 445 + 23, 18, 134);

    const int seconds = std::max(0, static_cast<int>(std::floor(game.timeRemaining)));
    sprite(158, 532, 13);
    x = 532;
    int value = seconds;
    for (int i = 0; i < 3; ++i) {
        x -= 9;
        sprite(game.unlimitedTime || seconds > 999 ? 156 :
            game.timerVisible ? 146 + value % 10 : 157, x, 13);
        value /= 10;
    }
    sprite(145, x - 9, 13);
}

void Renderer::drawBoard(const GameState& game) {
    // Most board cells do not change between frames. Keep the original pixels
    // in a separate surface and update only cells whose selected sprite changes
    // (broken/recolored walls, gates and placed blocks). This avoids 289 GDI
    // calls per frame, including while the browser shares the UI thread.
    for (int row = 0; row < BoardRows; ++row) {
        for (int column = 0; column < BoardColumns; ++column) {
            const auto index = static_cast<std::size_t>(row * BoardColumns + column);
            const int spriteIndex = TileSprite(game.tiles[index]);
            if (!boardValid_ || cachedTileSprites_[index] != spriteIndex) {
                spriteTo(boardDc_, spriteIndex, column * TileSize, row * TileSize);
                cachedTileSprites_[index] = spriteIndex;
            }
        }
    }
    boardValid_ = true;
    BitBlt(frameDc_, 0, HudHeight, Width, BoardPixels, boardDc_, 0, 0, SRCCOPY);
}

void Renderer::draw(const GameState& game) {
    updateAnimations(game);
    drawHud(game);
    const int clip = SaveDC(frameDc_);
    IntersectClipRect(frameDc_, 0, HudHeight, Width, Height);
    drawBoard(game);

    // Two gold tiles chase clockwise around opposite sides of the perimeter
    // while remaining seconds are converted to points (one step per 20 ms).
    if (game.phase == Phase::LevelComplete && completionBonusSeconds_ > 0) {
        const int converted = std::min(completionBonusSeconds_,
            static_cast<int>(std::floor(game.celebrationElapsed / 0.020)));
        if (converted > 0) {
            const int step = (converted - 1) % (BoardColumns + BoardRows);
            const int column1 = step < BoardColumns ? step : BoardColumns - 1;
            const int row1 = step < BoardColumns ? 0 : step - BoardColumns;
            const int column2 = step < BoardColumns ? BoardColumns - step - 1 : 0;
            const int row2 = step < BoardColumns ? BoardRows - 1 : BoardRows - (step - BoardColumns) - 1;
            sprite(GoldSprite(TileSprite(game.tiles[row1 * BoardColumns + column1])),
                column1 * TileSize, HudHeight + row1 * TileSize);
            sprite(GoldSprite(TileSprite(game.tiles[row2 * BoardColumns + column2])),
                column2 * TileSize, HudHeight + row2 * TileSize);
        }
    }

    HGDIOBJ oldPen = SelectObject(frameDc_, inkPen_);
    HGDIOBJ oldBrush = SelectObject(frameDc_, GetStockObject(BLACK_BRUSH));
    for (const auto& stroke : game.strokes) {
        if (stroke.points.empty()) continue;
        if (stroke.points.size() == 1) {
            const int x = static_cast<int>(std::lround(stroke.points[0].x));
            const int y = HudHeight + static_cast<int>(std::lround(stroke.points[0].y));
            const int radius = static_cast<int>(InkRadius);
            SelectObject(frameDc_, GetStockObject(NULL_PEN));
            Ellipse(frameDc_, x - radius, y - radius, x + radius, y + radius);
            SelectObject(frameDc_, inkPen_);
            continue;
        }
        std::vector<POINT> points;
        points.reserve(stroke.points.size());
        for (const auto& point : stroke.points)
            points.push_back({static_cast<LONG>(std::lround(point.x)),
                HudHeight + static_cast<LONG>(std::lround(point.y))});
        Polyline(frameDc_, points.data(), static_cast<int>(points.size()));
    }
    SelectObject(frameDc_, oldBrush);
    SelectObject(frameDc_, oldPen);

    for (const auto& ball : game.balls) {
        if (!ball.active) continue;
        const int diameter = std::max(1, static_cast<int>(std::lround(ball.radius * 2)));
        sprite(BallSprite(ball), static_cast<int>(std::lround(ball.position.x - ball.radius)),
            HudHeight + static_cast<int>(std::lround(ball.position.y - ball.radius)),
            true, diameter, diameter);
    }
    RestoreDC(frameDc_, clip);
    presentationDirty_ = true;
}

RECT Renderer::destination(const RECT& client) const {
    const int availableWidth = std::max(0L, client.right - client.left);
    const int availableHeight = std::max(0L, client.bottom - client.top);
    const double scale = std::min(static_cast<double>(availableWidth) / Width,
                                  static_cast<double>(availableHeight) / Height);
    const int width = static_cast<int>(std::floor(Width * scale));
    const int height = static_cast<int>(std::floor(Height * scale));
    const LONG left = client.left + (availableWidth - width) / 2;
    const LONG top = client.top + (availableHeight - height) / 2;
    return {left, top, left + width, top + height};
}

void Renderer::resizePresentation(int width, int height) {
    if (presentationBitmap_ && width == presentationWidth_ && height == presentationHeight_) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(presentationDc_, &info, DIB_RGB_COLORS,
        &pixels, nullptr, 0);
    if (!bitmap) throw std::runtime_error("Windows could not resize the presentation surface.");
    HGDIOBJ previous = SelectObject(presentationDc_, bitmap);
    if (!oldPresentationBitmap_) oldPresentationBitmap_ = previous;
    if (presentationBitmap_) DeleteObject(presentationBitmap_);
    presentationBitmap_ = bitmap;
    presentationWidth_ = width;
    presentationHeight_ = height;
    presentationDirty_ = true;
}

void Renderer::present(HDC target, const RECT& client) {
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;
    resizePresentation(width, height);
    if (presentationDirty_) {
        const RECT local{0, 0, width, height};
        SetDCBrushColor(presentationDc_, RGB(192, 192, 203));
        FillRect(presentationDc_, &local, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const auto rect = destination(local);
        if (rect.right > rect.left && rect.bottom > rect.top) {
            SetStretchBltMode(presentationDc_, COLORONCOLOR);
            StretchBlt(presentationDc_, rect.left, rect.top,
                rect.right - rect.left, rect.bottom - rect.top,
                frameDc_, 0, 0, Width, Height, SRCCOPY);
        }
        presentationDirty_ = false;
    }
    // The only drawing operation on the visible window: one complete, 1:1 copy.
    // In particular, never expose the gray clear or a partly stretched frame.
    BitBlt(target, client.left, client.top, width, height, presentationDc_, 0, 0, SRCCOPY);
}

bool Renderer::toBoard(POINT clientPoint, const RECT& client, Vec2& point) const {
    const auto rect = destination(client);
    if (rect.right <= rect.left || rect.bottom <= rect.top ||
        clientPoint.x < rect.left || clientPoint.x >= rect.right ||
        clientPoint.y < rect.top || clientPoint.y >= rect.bottom) return false;
    point.x = static_cast<double>(clientPoint.x - rect.left) * Width / (rect.right - rect.left);
    point.y = static_cast<double>(clientPoint.y - rect.top) * Height / (rect.bottom - rect.top) - HudHeight;
    return point.y >= 0 && point.y < BoardPixels;
}

bool Renderer::saveBitmap(const std::wstring& path) const {
    if (!framePixels_) return false;
    GdiFlush();
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + Width * Height * 4;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = Width;
    info.biHeight = -Height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = Width * Height * 4;
    HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool okay = WriteFile(output, &file, sizeof(file), &written, nullptr) && written == sizeof(file)
        && WriteFile(output, &info, sizeof(info), &written, nullptr) && written == sizeof(info)
        && WriteFile(output, framePixels_, info.biSizeImage, &written, nullptr) && written == info.biSizeImage;
    CloseHandle(output);
    return okay;
}
}
