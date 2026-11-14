#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "Game.h"
#include "Levels.h"
#include "Renderer.h"
#include "BoardBrowser.h"
#include "MechanicsDialog.h"
#include "PenCursor.h"
#include "resource.h"
#include "TextPage.h"
#include "StatisticsFile.h"
#include <sstream>
#include <iomanip>

namespace {
using namespace inkball;
constexpr UINT_PTR UpdateTimer = 1;
constexpr UINT ShowLossMessage = WM_APP + 1;
constexpr wchar_t WindowClassName[] = L"InkBall.Reconstructed.Window";

std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) return L"An unexpected error occurred.";
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::wstring Text(HINSTANCE instance, UINT id, const wchar_t* fallback) {
    wchar_t buffer[2048]{};
    const int length = LoadStringW(instance, id, buffer, static_cast<int>(std::size(buffer)));
    return length > 0 ? std::wstring(buffer, static_cast<std::size_t>(length)) : fallback;
}

void EnableDpiAwareness() {
    HMODULE user = GetModuleHandleW(L"user32.dll");
    using SetAwareness = BOOL(WINAPI*)(HANDLE);
    auto setAwareness = reinterpret_cast<SetAwareness>(GetProcAddress(user, "SetProcessDpiAwarenessContext"));
    if (setAwareness) setAwareness(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    else SetProcessDPIAware();
}

UINT DpiForWindow(HWND window) {
    using GetDpi = UINT(WINAPI*)(HWND);
    auto getDpi = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpi && window) return getDpi(window);
    HDC dc = GetDC(nullptr);
    const UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

RECT WindowRect(int clientWidth, int clientHeight, UINT dpi) {
    RECT rect{0, 0, clientWidth, clientHeight};
    using AdjustForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjust = reinterpret_cast<AdjustForDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (adjust) adjust(&rect, WS_OVERLAPPEDWINDOW, TRUE, 0, dpi);
    else AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, TRUE, 0);
    return rect;
}

struct Options {
    bool smokeTest = false;
    std::wstring screenshot;
    int difficulty = -1;
    int level = -1;
    std::uint32_t seed = 0;
    double advance = 0;
};

Options ReadOptions() {
    Options options;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) throw std::runtime_error("The command line could not be read.");
    try {
        for (int i = 1; i < argc; ++i) {
            const std::wstring argument = argv[i];
            auto value = [&]() -> std::wstring {
                if (++i >= argc) throw std::runtime_error("A command-line option is missing its value.");
                return argv[i];
            };
            if (argument == L"--smoke-test") options.smokeTest = true;
            else if (argument == L"--screenshot") options.screenshot = value();
            else if (argument == L"--difficulty") options.difficulty = std::stoi(value());
            else if (argument == L"--level") options.level = std::stoi(value());
            else if (argument == L"--seed") options.seed = static_cast<std::uint32_t>(std::stoul(value()));
            else if (argument == L"--advance") options.advance = std::stod(value());
            else throw std::runtime_error("Unknown option. Supported: --smoke-test, --screenshot FILE.bmp, --difficulty 0..4, --level INDEX, --seed NUMBER, --advance SECONDS.");
        }
        if (options.difficulty < -1 || options.difficulty > 4 || options.level < -1 ||
            !std::isfinite(options.advance) || options.advance < 0 || options.advance > 600)
            throw std::runtime_error("Command-line values are outside the supported range.");
    } catch (...) {
        LocalFree(argv);
        throw;
    }
    LocalFree(argv);
    if ((options.smokeTest || !options.screenshot.empty()) && !options.seed) options.seed = 1;
    return options;
}

std::vector<std::uint8_t> LevelResource(HINSTANCE instance) {
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(IDR_LEVELS), RT_RCDATA);
    if (!resource) throw std::runtime_error("The original level resource is missing from this executable.");
    const DWORD size = SizeofResource(instance, resource);
    HGLOBAL loaded = LoadResource(instance, resource);
    const auto* data = loaded ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
    if (!data || !size) throw std::runtime_error("The original level resource could not be read.");
    return {data, data + size};
}

struct Settings {
    int difficulty = 0;
    std::uint64_t highScore = 5000;
    bool showFps = false;
    std::wstring file, statisticsFile;
    RunStatistics lifetime;

    void load() {
        wchar_t folder[32768]{};
        const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", folder, static_cast<DWORD>(std::size(folder)));
        if (!size || size >= std::size(folder)) return;
        std::wstring directory = std::wstring(folder) + L"\\InkBall-Reconstructed";
        if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
        file = directory + L"\\settings.ini";
        statisticsFile = directory + L"\\statistics-v1.dat";
        LoadLifetimeStatistics(statisticsFile,lifetime);
        difficulty = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Game", L"Difficulty", 0, file.c_str())), 0, 4);
        showFps = GetPrivateProfileIntW(L"Display", L"ShowFPS", 0, file.c_str()) != 0;
        wchar_t value[64]{};
        GetPrivateProfileStringW(L"Game", L"HighScore", L"5000", value, static_cast<DWORD>(std::size(value)), file.c_str());
        wchar_t* end = nullptr;
        const unsigned long long parsed = std::wcstoull(value, &end, 10);
        if (end && !*end && value[0] != L'-') highScore = parsed;
    }

    bool save(const GameState& game, const Statistics& statistics) {
        if (file.empty()) return false;
        const auto difficultyText = std::to_wstring(game.difficulty);
        const auto highScoreText = std::to_wstring(game.highScore);
        const BOOL first = WritePrivateProfileStringW(L"Game", L"Difficulty", difficultyText.c_str(), file.c_str());
        const BOOL second = WritePrivateProfileStringW(L"Game", L"HighScore", highScoreText.c_str(), file.c_str());
        const BOOL third = WritePrivateProfileStringW(L"Display", L"ShowFPS", showFps ? L"1" : L"0", file.c_str());
        const bool totals = SaveLifetimeStatistics(statisticsFile, statistics.lifetime);
        return first && second && third && totals;
    }
};

class Application {
public:
    HINSTANCE instance;
    HWND window = nullptr;
    Renderer renderer;
    Game game;
    Settings settings;
    Options options;
    std::wstring name;
    bool drawing = false;
    bool manualPause = false;
    bool active = true;
    bool inMenu = false;
    bool sizing = false;
    bool minimized = false;
    bool inDialog = false;
    bool lossQueued = false;
    bool fatalError = false;
    bool timerRunning = false;
    bool frameDirty = true;
    PenCursor penCursor;
    TextPage statisticsPage;
    std::wstring lastTitle;
    unsigned presentedFrames = 0;
    double measuredFps = 0;
    LARGE_INTEGER frequency{};
    LARGE_INTEGER lastTick{};
    LARGE_INTEGER fpsStart{};

    Application(HINSTANCE module, Options arguments)
        : instance(module), game(LoadLevels(LevelResource(module))), options(std::move(arguments)) {
        name = Text(instance, 15000, L"InkBall");
        renderer.initialize(instance);
        if (!options.smokeTest && options.screenshot.empty()) settings.load();
        const int difficulty = options.difficulty >= 0 ? options.difficulty : settings.difficulty;
        game.seedLifetimeStatistics(settings.lifetime);
        game.setHighScore(settings.highScore);
        game.newGame(difficulty, options.seed);
        if (options.level >= 0) {
            if (static_cast<std::size_t>(options.level) >= game.levels().size())
                throw std::runtime_error("The requested level index does not exist.");
            game.loadLevel(static_cast<std::size_t>(options.level));
        }
        QueryPerformanceFrequency(&frequency);
        resetClock();
        resetFps();
    }

    void resetClock() { QueryPerformanceCounter(&lastTick); }
    void resetFps() {
        QueryPerformanceCounter(&fpsStart);
        presentedFrames = 0;
        measuredFps = 0;
    }
    void updateTitle() {
        if (!window) return;
        std::wstring title = name + (manualPause ? L" (Paused)" : L"");
        if (settings.showFps) {
            title += L"  |  FPS: ";
            if (game.state().paused) title += L"paused";
            else if (measuredFps > 0) title += std::to_wstring(static_cast<int>(std::lround(measuredFps)));
            else title += L"measuring";
        }
        if (title != lastTitle) {
            SetWindowTextW(window, title.c_str());
            lastTitle = std::move(title);
        }
    }
    void framePresented() {
        if (!settings.showFps || game.state().paused) return;
        ++presentedFrames;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double seconds = frequency.QuadPart
            ? static_cast<double>(now.QuadPart - fpsStart.QuadPart) / static_cast<double>(frequency.QuadPart)
            : 0;
        if (seconds >= 1.0) {
            measuredFps = presentedFrames / seconds;
            presentedFrames = 0;
            fpsStart = now;
            updateTitle();
        }
    }
    void syncTimer() {
        if (!window) return;
        const bool shouldRun = !game.state().paused && !fatalError;
        if (timerRunning == shouldRun) return;
        if (shouldRun) {
            if (!SetTimer(window, UpdateTimer, 15, nullptr))
                throw std::runtime_error("Windows could not start the game timer.");
        } else KillTimer(window, UpdateTimer);
        timerRunning = shouldRun;
    }
    void updateMenu() {
        if (!window) return;
        HMENU menu = GetMenu(window);
        CheckMenuRadioItem(menu, IDM_BEGINNER, IDM_EXPERT, IDM_BEGINNER + game.state().difficulty, MF_BYCOMMAND);
        CheckMenuItem(menu, IDM_PAUSE, MF_BYCOMMAND | (manualPause ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, IDM_SHOW_FPS, MF_BYCOMMAND | (settings.showFps ? MF_CHECKED : MF_UNCHECKED));
        updateTitle();
    }

    void stopDrawing() {
        if (!drawing) return;
        game.endStroke();
        drawing = false;
        // Moving/resizing the native window also captures the mouse. Only
        // release capture owned by an ink stroke, or pausing on entry to the
        // system move loop would immediately cancel the title-bar drag.
        if (window && GetCapture() == window) ReleaseCapture();
    }

    void syncPause() {
        const bool paused = manualPause || !active || inMenu || sizing || minimized || inDialog;
        const bool changed = paused != game.state().paused;
        if (paused) stopDrawing();
        game.pause(paused);
        resetClock();
        if (changed) resetFps();
        // A modal browser/settings window shares this thread. Paused games
        // must not keep consuming its message loop with 15 ms redraw work.
        syncTimer();
        updateMenu();
    }

    void refresh() {
        // Rendering belongs to WM_PAINT, so multiple timer/mouse messages can
        // update state without redundantly drawing frames Windows never shows.
        frameDirty = true;
        if (window) InvalidateRect(window, nullptr, FALSE);
    }

    bool boardPoint(LPARAM coordinates, Vec2& point) const {
        RECT client{};
        GetClientRect(window, &client);
        POINT mouse{GET_X_LPARAM(coordinates), GET_Y_LPARAM(coordinates)};
        return renderer.toBoard(mouse, client, point);
    }

    void newGame(int difficulty) {
        stopDrawing();
        settings.save(game.state(),game.statistics());
        game.newGame(difficulty);
        manualPause = false;
        lossQueued = false;
        resetFps();
        syncPause();
        refresh();
    }

    void browseBoards() {
        if (inDialog) return;
        inDialog = true;
        syncPause();
        const auto selected = ShowBoardBrowser(window, instance, game.levels(), game.state().levelId);
        if (selected) {
            settings.save(game.state(),game.statistics());
            game.loadLevel(*selected);
            manualPause = false;
            lossQueued = false;
        }
        inDialog = false;
        syncPause();
        refresh();
    }
    std::wstring statisticsText() const {
        const auto stats = game.statistics();
        std::wostringstream text;
        text << std::fixed << std::setprecision(1);
        const auto section = [&](const wchar_t* label, const RunStatistics& s) {
            text << label << L"\r\n"
                << L"Active play: " << s.activeSeconds << L" seconds\r\n"
                << L"Ink drawn: " << s.inkLength << L" board pixels\r\n"
                << L"Strokes drawn: " << s.strokes << L"\r\n"
                << L"Hits on ink: " << s.inkHits << L"\r\n"
                << L"Hits on wall tiles: " << s.wallHits << L"\r\n"
                << L"Ball-to-ball hits: " << s.ballHits << L"\r\n"
                << L"Balls sunk: " << s.ballsSunk << L"\r\n"
                << L"Boards cleared: " << s.boardsCleared << L"\r\n";
            if (s.boardsCleared) text << L"Completion active time (total): " << s.completionActiveSeconds << L" seconds\r\n";
            if (s.timedBoardsCleared) text << L"Time remaining at completion (total): " << s.completionRemainingSeconds
                << L" seconds across " << s.timedBoardsCleared << L" timed boards\r\n";
            text << L"\r\n";
        };
        text << L"A run is one board attempt. A game spans boards until a new game,\r\n"
             << L"restart, or Play Selected Board. Paused time is excluded.\r\n"
             << L"Ink length uses board pixels, independent of screen scaling.\r\n"
             << L"Hits count successful deflections. Lifetime totals are saved across launches.\r\n\r\n";
        text << L"Play continues while this page is open. Press Refresh for a snapshot.\r\n"
             << L"Completion times are captured before the celebration; unlimited boards have no remaining-time value.\r\n\r\n";
        section(L"CURRENT RUN",stats.run); section(L"CURRENT GAME",stats.game);
        section(L"LIFETIME",stats.lifetime);
        if(stats.hasPreviousRun)section(L"PREVIOUS RUN",stats.previousRun);
        if(stats.hasPreviousGame)section(L"PREVIOUS GAME",stats.previousGame);
        return text.str();
    }
    void showStatistics() {
        if (inDialog) return;
        statisticsPage.showModeless(window,L"Statistics - InkBall",[this]{return statisticsText();});
    }

    void restartBoard() {
        if (inDialog) return;
        stopDrawing();
        settings.save(game.state(),game.statistics());
        game.restartLevel();
        lossQueued = false;
        resetFps();
        syncPause();
        refresh();
    }

    void mechanicsSettings() {
        if (inDialog) return;
        inDialog = true;
        syncPause();
        const bool changed = ShowMechanicsDialog(window, instance, game);
        inDialog = false;
        syncPause();
        if (changed) refresh();
    }

    void toggleFps() {
        settings.showFps = !settings.showFps;
        resetFps();
        updateMenu();
    }

    void help() {
        inDialog = true;
        syncPause();
        MessageBoxW(window,
            L"Guide each colored ball into a hole of the same color.\n"
            L"Gray balls may enter any hole; gray holes accept any ball.\n\n"
            L"Drag with the left mouse button or a pen to draw an ink barrier.\n"
            L"A ball bounces off the ink, and the whole struck stroke disappears.\n"
            L"Right-click an empty square to place a block when one is available.\n\n"
            L"Orange, blue, green and gold balls score 200, 400, 800 and 1600.\n"
            L"Gray balls score no points. Clear a board for a remaining-time bonus.\n"
            L"A wrong-color hole or an expired timer ends the game.\n\n"
            L"Colored walls recolor balls. Cracked blocks break on a matching hit;\n"
            L"gray cracked blocks accept any ball. Arrow gates require the matching\n"
            L"color and arrow direction (gray gates accept any color). Draining a\n"
            L"matching colored ball toggles walls of that color. Clock walls cycle\n"
            L"automatically. Chevrons accelerate balls in their arrow direction.\n\n"
            L"F2: New game     Space: Pause/resume     F1: Help\n"
            L"Ctrl+B: Browse, filter, preview and play any original board.\n"
            L"Ctrl+R: Restart this board, keeping its current mechanics settings.\n"
            L"Ctrl+M: Current-board mechanics, speeds and chevron settings.\n"
            L"Game > Show FPS displays measured game paints/second in the title.\n"
            L"Game > Clear Ink removes all strokes. Difficulty selects a new game.\n"
            L"The game pauses while its window is inactive or a menu is open.",
            (name + L" Help").c_str(), MB_OK | MB_ICONINFORMATION);
        inDialog = false;
        syncPause();
    }

    void about() {
        inDialog = true;
        syncPause();
        MessageBoxW(window,
            L"InkBall — C++ reconstruction\n\n"
            L"A source-buildable Windows desktop game using the original levels,\n"
            L"bitmap artwork and icon extracted from the supplied installer.\n\n"
            L"The game engine and modern Win32/GDI front end are reconstructed.\n"
            L"This is an independent reconstruction, not Microsoft's source code.\n"
            L"Original game assets remain copyright Microsoft Corporation.\n\n"
            L"See README.md, fidelity notes and third-party notices in the source package.",
            (L"About " + name).c_str(), MB_OK | MB_ICONINFORMATION);
        inDialog = false;
        syncPause();
    }

    void showLoss() {
        lossQueued = false;
        if (game.state().phase != Phase::GameOver || inDialog) return;
        inDialog = true;
        syncPause();
        const auto& message = game.state().message;
        const bool timeout = message.find("time") != std::string::npos || message.find("Time") != std::string::npos;
        const std::wstring reason = timeout
            ? Text(instance, 15002, L"You ran out of time.")
            : Text(instance, 15003, L"A ball entered a hole of the wrong color.");
        MessageBoxW(window, reason.c_str(), Text(instance, 15001, L"Game over!").c_str(), MB_OK | MB_ICONINFORMATION);
        settings.save(game.state(),game.statistics());
        game.restartLevel();
        inDialog = false;
        manualPause = false;
        syncPause();
        refresh();
    }

    void tick() {
        // KillTimer does not remove an already queued timer message.
        if (!timerRunning || fatalError || game.state().paused) return;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double elapsed = frequency.QuadPart
            ? static_cast<double>(now.QuadPart - lastTick.QuadPart) / static_cast<double>(frequency.QuadPart)
            : 0.016;
        lastTick = now;
        game.tick(std::clamp(elapsed, 0.0, 0.10));
        refresh();
        if (game.state().phase == Phase::GameOver && !lossQueued && !inDialog && active && !inMenu) {
            lossQueued = true;
            PostMessageW(window, ShowLossMessage, 0, 0);
        }
    }

    int runHeadless() {
        if (options.smokeTest) {
            // Validate every recovered board through the actual loader, engine,
            // sprite selection and renderer. This does not touch saved settings.
            for (std::size_t index = 0; index < game.levels().size(); ++index) {
                game.loadLevel(index);
                for (int frame = 0; frame < 12; ++frame) game.tick(1.0 / 120.0);
                for (const auto& ball : game.state().balls) {
                    if (!std::isfinite(ball.position.x) || !std::isfinite(ball.position.y) ||
                        ball.color < 0 || ball.color > 4)
                        throw std::runtime_error("A recovered board produced an invalid ball state.");
                }
                renderer.draw(game.state());
            }
            game.newGame(options.difficulty >= 0 ? options.difficulty : 0, options.seed);
            if (options.level >= 0) game.loadLevel(static_cast<std::size_t>(options.level));
        }
        for (double remaining = options.advance; remaining > 0; remaining -= 1.0 / 120.0)
            game.tick(std::min(remaining, 1.0 / 120.0));
        renderer.draw(game.state());
        if (!options.screenshot.empty() && !renderer.saveBitmap(options.screenshot))
            throw std::runtime_error("The screenshot file could not be written.");
        return 0;
    }
};

LRESULT HandleMessage(Application& app, HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        app.window = window;
        if (!InsertMenuW(GetSubMenu(GetMenu(window),0),IDM_CLEAR,
            MF_BYCOMMAND|MF_STRING,IDM_STATISTICS,L"&Statistics..."))
            throw std::runtime_error("The statistics menu could not be created.");
        // Add the new feature to whichever original language menu Windows chose.
        if (!InsertMenuW(GetSubMenu(GetMenu(window), 0), IDM_CLEAR,
            MF_BYCOMMAND | MF_STRING, IDM_BROWSE, L"&Browse Boards...\tCtrl+B"))
            throw std::runtime_error("The board browser menu could not be created.");
        if (!InsertMenuW(GetSubMenu(GetMenu(window), 0), IDM_PAUSE,
            MF_BYCOMMAND | MF_STRING, IDM_RESTART, L"&Restart Current Board\tCtrl+R") ||
            !InsertMenuW(GetSubMenu(GetMenu(window), 0), IDM_CLEAR,
            MF_BYCOMMAND | MF_STRING, IDM_MECHANICS, L"Board &Mechanics...\tCtrl+M") ||
            !InsertMenuW(GetSubMenu(GetMenu(window), 0), IDM_CLEAR,
            MF_BYCOMMAND | MF_STRING, IDM_SHOW_FPS, L"Show &FPS"))
            throw std::runtime_error("The game controls could not be created.");
        app.penCursor.setDpi(DpiForWindow(window));
        app.updateMenu();
        app.refresh();
        app.syncTimer();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        if (app.frameDirty) {
            app.renderer.draw(app.game.state());
            app.frameDirty = false;
        }
        app.renderer.present(dc, client);
        EndPaint(window, &paint);
        app.framePresented();
        return 0;
    }
    case WM_TIMER:
        if (wParam == UpdateTimer) app.tick();
        return 0;
    case WM_SIZE:
        app.minimized = wParam == SIZE_MINIMIZED;
        app.syncPause();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        const UINT dpi = DpiForWindow(window);
        const RECT rect = WindowRect(MulDiv(Renderer::Width / 2, dpi, 96),
            MulDiv(Renderer::Height / 2, dpi, 96), dpi);
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {rect.right - rect.left, rect.bottom - rect.top};
        return 0;
    }
    case WM_DPICHANGED: {
        app.penCursor.setDpi(HIWORD(wParam));
        const auto* rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        app.sizing = true; app.syncPause(); return 0;
    case WM_EXITSIZEMOVE:
        app.sizing = false; app.syncPause(); return 0;
    case WM_ACTIVATEAPP:
        app.active = wParam != 0; app.syncPause(); return 0;
    case WM_ENTERMENULOOP:
        app.inMenu = true; app.syncPause(); return 0;
    case WM_EXITMENULOOP:
        app.inMenu = false; app.syncPause(); return 0;
    case WM_INITMENUPOPUP:
        app.updateMenu(); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT point{};
            RECT client{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            GetClientRect(window, &client);
            Vec2 boardPoint;
            SetCursor(app.renderer.toBoard(point, client, boardPoint)
                ? app.penCursor.get() : LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_LBUTTONDOWN: {
        Vec2 point;
        if (!app.game.state().paused && app.boardPoint(lParam, point)) {
            SetFocus(window);
            SetCapture(window);
            app.drawing = true;
            app.game.beginStroke(point);
            app.refresh();
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (app.drawing) {
            if (!(wParam & MK_LBUTTON)) app.stopDrawing();
            else {
                Vec2 point;
                if (app.boardPoint(lParam, point)) app.game.extendStroke(point);
                app.refresh();
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (app.drawing) {
            Vec2 point;
            if (app.boardPoint(lParam, point)) app.game.extendStroke(point);
            app.stopDrawing();
            app.refresh();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (app.drawing) { app.game.endStroke(); app.drawing = false; }
        return 0;
    case WM_RBUTTONDOWN: {
        Vec2 point;
        if (!app.game.state().paused && app.boardPoint(lParam, point)) {
            app.game.placeBlock(point);
            app.refresh();
        }
        return 0;
    }
    case WM_COMMAND: {
        const UINT command = LOWORD(wParam);
        if (command >= IDM_BEGINNER && command <= IDM_EXPERT)
            app.newGame(static_cast<int>(command - IDM_BEGINNER));
        else switch (command) {
        case IDM_NEW: app.newGame(app.game.state().difficulty); break;
        case IDM_PAUSE: app.manualPause = !app.manualPause; app.syncPause(); break;
        case IDM_CLEAR: app.stopDrawing(); app.game.clearInk(); app.refresh(); break;
        case IDM_BROWSE: app.browseBoards(); break;
        case IDM_STATISTICS: app.showStatistics(); break;
        case IDM_RESTART: app.restartBoard(); break;
        case IDM_MECHANICS: app.mechanicsSettings(); break;
        case IDM_SHOW_FPS: app.toggleFps(); break;
        case IDM_HELP: app.help(); break;
        case IDM_ABOUT: app.about(); break;
        case IDM_EXIT: SendMessageW(window, WM_CLOSE, 0, 0); break;
        default: return DefWindowProcW(window, message, wParam, lParam);
        }
        return 0;
    }
    case ShowLossMessage: app.showLoss(); return 0;
    case WM_CLOSE:
        app.stopDrawing();
        if (!app.fatalError && !app.settings.save(app.game.state(),app.game.statistics())) {
            app.inDialog = true;
            app.syncPause();
            MessageBoxW(window, L"The game could not save your high score and difficulty in your local settings folder.",
                app.name.c_str(), MB_OK | MB_ICONWARNING);
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, UpdateTimer);
        app.timerRunning = false;
        PostQuitMessage(app.fatalError ? 1 : 0);
        return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);
    try {
        return HandleMessage(*app, window, message, wParam, lParam);
    } catch (const std::exception& exception) {
        if (!app->fatalError) {
            app->fatalError = true;
            KillTimer(window, UpdateTimer);
            app->timerRunning = false;
            MessageBoxW(window, Widen(exception.what()).c_str(), L"InkBall Error", MB_OK | MB_ICONERROR);
            DestroyWindow(window);
        }
        return 0;
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    bool headless = false;
    try {
        EnableDpiAwareness();
        Options options = ReadOptions();
        headless = options.smokeTest || !options.screenshot.empty();
        Application app(instance, std::move(options));
        if (headless) return app.runHeadless();

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_INKBALL));
        windowClass.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_INKBALL), IMAGE_ICON, 16, 16, LR_SHARED));
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU);
        windowClass.lpszClassName = WindowClassName;
        if (!RegisterClassExW(&windowClass)) throw std::runtime_error("The InkBall window class could not be registered.");

        const UINT dpi = DpiForWindow(nullptr);
        const RECT rect = WindowRect(MulDiv(Renderer::Width, dpi, 96), MulDiv(Renderer::Height, dpi, 96), dpi);
        HWND window = CreateWindowExW(0, WindowClassName, app.name.c_str(), WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, instance, &app);
        if (!window) throw std::runtime_error("The InkBall window could not be created.");
        HACCEL accelerators = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCELERATOR));
        if (!accelerators) throw std::runtime_error("The InkBall keyboard shortcuts could not be loaded.");
        ShowWindow(window, showCommand);
        UpdateWindow(window);
        app.resetClock();

        MSG message{};
        BOOL received;
        while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (app.statisticsPage.translate(message)) continue;
            if (!TranslateAcceleratorW(window, accelerators, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (received < 0) throw std::runtime_error("The Windows message loop failed.");
        return static_cast<int>(message.wParam);
    } catch (const std::exception& exception) {
        if (!headless) MessageBoxW(nullptr, Widen(exception.what()).c_str(), L"InkBall Error", MB_OK | MB_ICONERROR);
        OutputDebugStringW((L"InkBall: " + Widen(exception.what()) + L"\n").c_str());
        return 1;
    }
}
