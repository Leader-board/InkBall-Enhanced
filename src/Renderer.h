#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <string>
#include "Game.h"

namespace inkball {

// The original art remains at its native 544 x 586 resolution. Only the final
// presentation is scaled; input uses the inverse of the same transform.
class Renderer {
public:
    static constexpr int Width = BoardPixels;
    static constexpr int Height = BoardPixels + HudHeight;
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void initialize(HINSTANCE instance);
    void draw(const GameState& game);
    void present(HDC target, const RECT& client);
    bool toBoard(POINT clientPoint, const RECT& client, Vec2& boardPoint) const;
    bool saveBitmap(const std::wstring& path) const;

private:
    HDC atlasDc_ = nullptr;
    HDC alphaDc_ = nullptr;
    HDC frameDc_ = nullptr;
    HDC boardDc_ = nullptr;
    HDC presentationDc_ = nullptr;
    HBITMAP atlasBitmap_ = nullptr;
    HBITMAP alphaBitmap_ = nullptr;
    HBITMAP frameBitmap_ = nullptr;
    HBITMAP boardBitmap_ = nullptr;
    HBITMAP presentationBitmap_ = nullptr;
    HGDIOBJ oldAtlasBitmap_ = nullptr;
    HGDIOBJ oldAlphaBitmap_ = nullptr;
    HGDIOBJ oldFrameBitmap_ = nullptr;
    HGDIOBJ oldBoardBitmap_ = nullptr;
    HGDIOBJ oldPresentationBitmap_ = nullptr;
    HPEN inkPen_ = nullptr;
    void* framePixels_ = nullptr;
    int presentationWidth_ = 0;
    int presentationHeight_ = 0;
    bool presentationDirty_ = true;
    bool boardValid_ = false;
    std::array<int, BoardCellCount> cachedTileSprites_{};
    std::uint64_t lastLevelSerial_ = 0;
    double lastElapsed_ = 0;
    double queueAccumulator_ = 0;
    std::array<double, 6> queuePositions_{};
    std::array<double, 6> queueSpeeds_{};
    std::size_t previousQueueSize_ = 0;
    Phase previousPhase_ = Phase::Playing;
    int completionBonusSeconds_ = 0;
    RECT destination(const RECT& client) const;
    void resizePresentation(int width, int height);
    void updateAnimations(const GameState& game);
    void drawBoard(const GameState& game);
    void spriteTo(HDC target, int index, int x, int y, bool transparent = false,
                  int width = 0, int height = 0) const;
    void sprite(int index, int x, int y, bool transparent = false,
                int width = 0, int height = 0) const;
    int number(std::uint64_t value, int right, int y, int digitBase) const;
    void drawHud(const GameState& game) const;
};
}
