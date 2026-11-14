#include "Renderer.h"
#include "SpriteRects.h"
#include "resource.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace inkball;

namespace {
constexpr std::uint32_t Sentinel = 0x0003115bu;
constexpr std::uint32_t Margin = 0x00c0c0cbu;
int checks = 0;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
}

// A separate caller-owned surface lets the tests inspect the public presentation
// result, including clipping and client offsets, without reading renderer internals.
class Surface {
public:
    Surface(int width, int height) : width_(width), height_(height) {
        dc_ = CreateCompatibleDC(nullptr);
        if (!dc_) throw std::runtime_error("Cannot allocate a test drawing context.");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap_ || !pixels) {
            DeleteDC(dc_);
            dc_ = nullptr;
            throw std::runtime_error("Cannot allocate a test bitmap.");
        }
        pixels_ = static_cast<std::uint32_t*>(pixels);
        oldBitmap_ = SelectObject(dc_, bitmap_);
        clear();
    }
    ~Surface() {
        if (oldBitmap_) SelectObject(dc_, oldBitmap_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
    }
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    HDC dc() const { return dc_; }
    int width() const { return width_; }
    int height() const { return height_; }
    void clear(std::uint32_t color = Sentinel) {
        GdiFlush();
        std::fill(pixels_, pixels_ + width_ * height_, color);
    }
    std::uint32_t at(int x, int y) const {
        return pixels_[y * width_ + x] & 0x00ffffffu;
    }
    std::vector<std::uint32_t> snapshot() const {
        GdiFlush();
        std::vector<std::uint32_t> result(pixels_, pixels_ + width_ * height_);
        for (auto& pixel : result) pixel &= 0x00ffffffu;
        return result;
    }
private:
    int width_, height_;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    std::uint32_t* pixels_ = nullptr;
};

GameState floorState() {
    GameState state;
    state.levelSerial = 1;
    return state;
}

bool contains(const RECT& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void render(Renderer& renderer, const GameState& state, Surface& surface) {
    renderer.draw(state);
    renderer.present(surface.dc(), {0, 0, surface.width(), surface.height()});
    GdiFlush();
}

void checkTimerWarning(Renderer& renderer) {
    Surface surface(Renderer::Width,Renderer::Height);
    auto state=floorState();state.timeRemaining=10;state.timerVisible=true;
    render(renderer,state,surface);const auto visible=surface.snapshot();
    state.timerVisible=false;render(renderer,state,surface);const auto hidden=surface.snapshot();
    int changed=0;bool outside=false;
    for(int y=0;y<Renderer::Height;++y)for(int x=0;x<Renderer::Width;++x) {
        const auto i=static_cast<std::size_t>(y*Renderer::Width+x);
        if(visible[i]!=hidden[i]) {
            outside=outside||x<505||x>=532||y<13||y>=32;
            ++changed;
        }
    }
    require(!outside,"Warning must change only the timer digit pixels.");
    require(changed>0,"Warning must visibly blank the countdown digits.");
    state.timerVisible=true;render(renderer,state,surface);
    require(surface.snapshot()==visible,"Warning on-phase must restore all original pixels without trails.");
    state.unlimitedTime=true;render(renderer,state,surface);const auto unlimited=surface.snapshot();
    state.timerVisible=false;render(renderer,state,surface);
    require(surface.snapshot()==unlimited,"Unlimited-time symbols must remain visible regardless of warning phase.");
}

void checkQueueAnimation(Renderer& renderer) {
    Surface actual(Renderer::Width,Renderer::Height),expected(Renderer::Width,Renderer::Height);
    auto state=floorState();state.levelSerial=200;state.queue={1,2,3,4,0,1};
    render(renderer,state,actual);
    for(int tick=1;tick<=30;++tick){state.elapsed=tick*0.015;render(renderer,state,actual);}
    // After 30 original 15-ms updates, queue coordinates are 62.15 and
    // 87.125. The third ball is still outside the visible queue strip.
    Renderer background;background.initialize(GetModuleHandleW(nullptr));
    auto empty=state;empty.queue.clear();render(background,empty,expected);
    HBITMAP atlas=static_cast<HBITMAP>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDB_SPRITES),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION));
    HDC atlasDc=CreateCompatibleDC(nullptr);auto old=SelectObject(atlasDc,atlas);
    const int saved=SaveDC(expected.dc());IntersectClipRect(expected.dc(),5,7,104,35);
    for(const auto& item:std::array<std::pair<int,int>,2>{{{13,62},{14,87}}}){
        const auto& r=kSpriteRects[item.first];
        BitBlt(expected.dc(),item.second,14,r.right-r.left,r.bottom-r.top,atlasDc,r.left,r.top,SRCCOPY);
    }
    RestoreDC(expected.dc(),saved);SelectObject(atlasDc,old);DeleteDC(atlasDc);DeleteObject(atlas);
    require(actual.snapshot()==expected.snapshot(),"Queue entry pixels must match the original 15-ms motion and descending follower acceleration.");
    const auto held=actual.snapshot();render(renderer,state,actual);
    require(actual.snapshot()==held,"Repainting without elapsed game time must not move the queue.");
    Renderer coalesced;coalesced.initialize(GetModuleHandleW(nullptr));
    auto start=state;start.elapsed=0;render(coalesced,start,actual);render(coalesced,state,actual);
    require(actual.snapshot()==held,"Coalescing paints must not change queue animation speed.");
    for(int tick=31;tick<=60;++tick){state.elapsed=tick*0.015;render(renderer,state,actual);}
    // Both leading balls must have settled by 0.9 s, independently of paint rate.
    auto settled=actual.snapshot();state.elapsed=1.2;render(renderer,state,actual);
    // Other queued balls can still move; compare just the leading pair.
    const auto later=actual.snapshot();bool same=true;
    for(int y=14;y<32;++y)for(int x=6;x<42;++x)if(settled[y*Renderer::Width+x]!=later[y*Renderer::Width+x])same=false;
    require(same,"The first two balls must settle at the original speed.");
}

int blackInColumn(const Surface& surface, int x, int y1, int y2) {
    int count = 0;
    for (int y = y1; y < y2; ++y) if (surface.at(x, HudHeight + y) == 0) ++count;
    return count;
}

void checkInk(Renderer& renderer) {
    Surface surface(Renderer::Width, Renderer::Height);
    auto state = floorState();
    state.strokes = {
        {1, {{100, 100}, {200, 100}}},
        {2, {{300, 100}}},
        {3, {{100, 200}, {150, 200}, {150, 250}}}
    };
    render(renderer, state, surface);
    require(blackInColumn(surface, 150, 80, 120) == 10,
            "Reference ink must be exactly ten pixels thick at native scale.");
    require(surface.at(96, HudHeight + 100) == 0 &&
            surface.at(96, HudHeight + 96) != 0 &&
            surface.at(94, HudHeight + 100) != 0,
            "Stroke end caps must be round and extend by five logical pixels.");
    require(blackInColumn(surface, 300, 80, 120) == 10 &&
            surface.at(300, HudHeight + 100) == 0 &&
            surface.at(296, HudHeight + 96) != 0,
            "A single-point stroke must be a round ten-pixel dot.");
    require(surface.at(150, HudHeight + 200) == 0 &&
            surface.at(153, HudHeight + 198) == 0 &&
            surface.at(154, HudHeight + 196) != 0,
            "A stroke bend must have a continuous round join without a square spike.");

    // Window scaling must also scale the full stroke thickness, not leave a
    // cosmetic screen-pixel pen that becomes too thin on high-DPI displays.
    Surface doubled(Renderer::Width * 2, Renderer::Height * 2);
    renderer.present(doubled.dc(), {0, 0, doubled.width(), doubled.height()});
    GdiFlush();
    int thickness = 0;
    for (int y = (HudHeight + 80) * 2; y < (HudHeight + 120) * 2; ++y)
        if (doubled.at(300, y) == 0) ++thickness;
    require(thickness == 20, "Ink must remain ten logical pixels thick at 200% scale.");
}

void checkBallTransparency(Renderer& renderer) {
    Surface surface(Renderer::Width, Renderer::Height);
    const auto empty = floorState();
    render(renderer, empty, surface);
    const auto background = surface.snapshot();
    for (int color = 0; color < 5; ++color) {
        for (const bool nearHole : {false, true}) {
            for (const int radius : {12, 9, 6, 1}) {
                auto state = empty;
                Ball ball;
                ball.color = color;
                ball.position = {240, 240};
                ball.radius = radius;
                ball.nearHole = nearHole;
                state.balls.push_back(ball);
                render(renderer, state, surface);
                const int left = 240 - radius, top = HudHeight + 240 - radius;
                const int diameter = radius * 2;
                const std::string description = "Ball color " + std::to_string(color) +
                    (nearHole ? " near hole" : " normal") + " radius " + std::to_string(radius);
                bool okay = true, visible = false;
                for (int y = top - 2; y < top + diameter + 2; ++y) {
                    for (int x = left - 2; x < left + diameter + 2; ++x) {
                        const auto pixel = surface.at(x, y);
                        const auto backdrop = background[static_cast<std::size_t>(y * surface.width() + x)];
                        const unsigned red = (pixel >> 16) & 255;
                        const unsigned green = (pixel >> 8) & 255;
                        const unsigned blue = pixel & 255;
                        if (red > 200 && blue > 200 && green < 64) okay = false;
                        if (x < left || x >= left + diameter || y < top || y >= top + diameter) {
                            if (pixel != backdrop) okay = false;
                        } else if (pixel != backdrop) visible = true;
                    }
                }
                const auto matchesBackground = [&](int x, int y) {
                    return surface.at(x, y) == background[static_cast<std::size_t>(y * surface.width() + x)];
                };
                // A two-pixel ball may legitimately put resampled ball coverage
                // into every destination pixel. Test its visibility, key-color
                // removal, and bounds above; larger balls must retain all four
                // transparent corners of the artwork.
                if (diameter >= 12) {
                    okay = okay && matchesBackground(left, top) &&
                        matchesBackground(left + diameter - 1, top) &&
                        matchesBackground(left, top + diameter - 1) &&
                        matchesBackground(left + diameter - 1, top + diameter - 1);
                }
                if (!okay || !visible) {
                    std::cerr << description << ": visible=" << visible << ", corners=" << std::hex
                              << surface.at(left, top) << ',' << surface.at(left + diameter - 1, top)
                              << ',' << surface.at(left, top + diameter - 1) << ','
                              << surface.at(left + diameter - 1, top + diameter - 1) << std::dec << '\n';
                }
                require(okay && visible, description + " must remain visible with transparent corners and no purple border.");
            }
        }
    }

    auto state = empty;
    state.balls.push_back({1, 2, {240, 240}, {}, 12, 0, true, false});
    renderer.draw(state);
    Surface scaled(680, 733);
    renderer.present(scaled.dc(), {0, 0, 680, 733});
    GdiFlush();
    bool purple = false;
    for (int y = 0; y < scaled.height(); ++y) for (int x = 0; x < scaled.width(); ++x) {
        const auto pixel = scaled.at(x, y);
        if (((pixel >> 16) & 255) > 200 && (pixel & 255) > 200 && ((pixel >> 8) & 255) < 64)
            purple = true;
    }
    require(!purple, "125% window scaling must not introduce magenta into a transparent ball edge.");
}

void checkPresentation(Renderer& renderer) {
    auto state = floorState();
    state.strokes.push_back({1, {{40, 80}, {220, 170}, {440, 90}}});
    state.balls.push_back({1, 4, {240, 240}, {}, 12, 0, true, false});
    renderer.draw(state);
    const std::array<RECT, 6> clients{{
        {0, 0, 544, 586}, {13, 19, 557, 605}, {0, 0, 680, 733},
        {17, 23, 289, 316}, {7, 11, 907, 597}, {9, 15, 553, 715}
    }};
    for (std::size_t i = 0; i < clients.size(); ++i) {
        const auto client = clients[i];
        Surface target(client.right + 19, client.bottom + 13);
        renderer.present(target.dc(), client);
        const auto full = target.snapshot();
        bool outsideClean = true, painted = false;
        for (int y = 0; y < target.height(); ++y) for (int x = 0; x < target.width(); ++x) {
            if (!contains(client, x, y) && target.at(x, y) != Sentinel) outsideClean = false;
            if (contains(client, x, y) && target.at(x, y) != Sentinel) painted = true;
        }
        require(outsideClean && painted,
                "Presentation must respect the client rectangle including a nonzero origin.");
        if (i == 4) {
            require(target.at(client.left, client.top + 100) == Margin &&
                    target.at(client.right - 1, client.top + 100) == Margin,
                    "A wide client must paint both letterbox margins.");
        }
        if (i == 5) {
            require(target.at(client.left + 100, client.top) == Margin &&
                    target.at(client.left + 100, client.bottom - 1) == Margin,
                    "A tall client must paint both letterbox margins.");
        }

        // Repaint a deliberately unaligned clip. Its pixels must be identical
        // to the same region in a whole-window repaint; otherwise a partial
        // invalidation can appear to shift the board's scaled texture.
        const RECT clip{client.left + 3, client.top + 5,
            client.left + (client.right - client.left) * 2 / 3,
            client.top + (client.bottom - client.top) * 3 / 4};
        target.clear();
        const HRGN region = CreateRectRgn(clip.left, clip.top, clip.right, clip.bottom);
        if (!region) throw std::runtime_error("Cannot allocate a test clipping region.");
        SelectClipRgn(target.dc(), region);
        renderer.present(target.dc(), client);
        SelectClipRgn(target.dc(), nullptr);
        DeleteObject(region);
        GdiFlush();
        bool stable = true;
        for (int y = 0; y < target.height(); ++y) for (int x = 0; x < target.width(); ++x) {
            const auto expected = contains(clip, x, y) ?
                full[static_cast<std::size_t>(y * target.width() + x)] : Sentinel;
            if (target.at(x, y) != expected) stable = false;
        }
        require(stable, "A partial repaint must preserve exact whole-frame pixels and clipping.");
        target.clear();
        renderer.draw(state);
        renderer.present(target.dc(), client);
        require(target.snapshot() == full, "Redrawing unchanged game state must not move or alter the image.");
    }

    Surface native(Renderer::Width, Renderer::Height);
    renderer.present(native.dc(), {0, 0, native.width(), native.height()});
    const auto original = native.snapshot();
    for (int i = 0; i < 24; ++i) {
        const auto client = clients[static_cast<std::size_t>(i) % clients.size()];
        Surface resized(client.right + 1, client.bottom + 1);
        renderer.present(resized.dc(), client);
        renderer.present(native.dc(), {0, 0, native.width(), native.height()});
        if (native.snapshot() != original)
            throw std::runtime_error("Returning from repeated resizes changed the native frame.");
    }
    require(true, "Repeated resizes preserve the native frame.");
    native.clear();
    renderer.present(native.dc(), {0, 0, 0, 0});
    const auto unpainted = native.snapshot();
    require(std::all_of(unpainted.begin(), unpainted.end(), [](auto pixel) { return pixel == Sentinel; }),
            "An empty client must not overwrite the target surface.");
}

void checkInputMapping(const Renderer& renderer) {
    struct Mapping { RECT client; POINT sample; double x, y, tolerance; };
    const std::array<Mapping, 6> mappings{{
        {{0, 0, 544, 586}, {160, 142}, 160, 100, 0.001},
        {{13, 19, 557, 605}, {173, 161}, 160, 100, 0.001},
        {{0, 0, 680, 733}, {200, 178}, 160, 100, 0.81},
        {{17, 23, 289, 316}, {97, 94}, 160, 100, 0.001},
        {{0, 0, 900, 586}, {338, 142}, 160, 100, 0.001},
        {{0, 0, 544, 700}, {160, 199}, 160, 100, 0.001}
    }};
    for (const auto& m : mappings) {
        Vec2 point;
        require(renderer.toBoard(m.sample, m.client, point) &&
                std::abs(point.x - m.x) <= m.tolerance && std::abs(point.y - m.y) <= m.tolerance,
                "Pointer coordinates must stay aligned with the scaled and centered board.");
    }
    Vec2 point;
    require(!renderer.toBoard({20, 20}, {0, 0, 544, 586}, point) &&
            !renderer.toBoard({10, 100}, {0, 0, 900, 586}, point) &&
            !renderer.toBoard({544, 585}, {0, 0, 544, 586}, point) &&
            !renderer.toBoard({0, 0}, {0, 0, 0, 0}, point),
            "The HUD, letterbox margins, outside edge, and empty client must reject drawing input.");
}

void checkBoardCache(Renderer& renderer) {
    Surface reused(Renderer::Width, Renderer::Height);
    auto state = floorState();
    const auto compareFresh = [&](const std::string& description) {
        render(renderer, state, reused);
        Renderer freshRenderer;
        freshRenderer.initialize(GetModuleHandleW(nullptr));
        Surface fresh(Renderer::Width, Renderer::Height);
        render(freshRenderer, state, fresh);
        require(reused.snapshot() == fresh.snapshot(), description);
    };
    state.tiles[7 * BoardColumns + 7].code = 0x3002;
    state.tiles[8 * BoardColumns + 8].code = 0x6000;
    state.tiles[8 * BoardColumns + 8].animationFrame = 4;
    state.balls.push_back({1, 3, {240, 180}, {}, 12, 0, true, false});
    state.strokes.push_back({1, {{40, 100}, {200, 100}}});
    compareFresh("The cached board must match a fresh render with walls, a gate, ink and a ball.");

    state.tiles[7 * BoardColumns + 7].code = 0;
    state.tiles[8 * BoardColumns + 8].animationFrame = 0;
    state.balls.clear();
    state.strokes.clear();
    compareFresh("Wall removal, gate animation and cleared ink/balls must leave no stale cached pixels.");

    state.tiles[2 * BoardColumns + 2].code = 0x3004;
    state.tiles[10 * BoardColumns + 11].code = 0x4001;
    compareFresh("Changed and newly placed tiles must update without requiring a new level serial.");

    ++state.levelSerial;
    state.tiles.fill({});
    state.tiles[3 * BoardColumns + 3].code = 0x2002;
    state.tiles[3 * BoardColumns + 4].code = 0x2012;
    state.tiles[4 * BoardColumns + 3].code = 0x2022;
    state.tiles[4 * BoardColumns + 4].code = 0x2032;
    compareFresh("Switching boards must replace the previous board background completely.");
}
}

int main(int argc,char** argv) {
    try {
        Renderer renderer;
        renderer.initialize(GetModuleHandleW(nullptr));
        if(argc==2&&std::string(argv[1])=="--queue-only") {
            checkQueueAnimation(renderer);std::cout<<"PASS: "<<checks<<" queue animation checks.\n";return 0;
        }
        checkTimerWarning(renderer);
        if(argc==2&&std::string(argv[1])=="--timer-only") {
            std::cout<<"PASS: "<<checks<<" timer rendering checks.\n";return 0;
        }
        checkQueueAnimation(renderer);
        checkInk(renderer);
        checkBallTransparency(renderer);
        checkPresentation(renderer);
        checkInputMapping(renderer);
        checkBoardCache(renderer);
        std::cout << "PASS: " << checks << " renderer checks for ink, ball transparency, repaint stability, resize, and input mapping.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " renderer checks: " << e.what() << '\n';
        return 1;
    }
}
