#include "MechanicsDialog.h"
#include "resource.h"
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace inkball {
namespace {
constexpr wchar_t DialogClass[] = L"InkBall.MechanicsDialog.Window";
constexpr wchar_t PanelClass[] = L"InkBall.MechanicsDialog.Panel";
constexpr int FirstFieldId = 3101;
constexpr int ApplyId = 3108, DefaultsId = 3109, StatusId = 3110, PanelId = 3111;
constexpr int DocumentHeight = 552, FooterHeight = 54;
struct Field {
    const wchar_t* label;
    const wchar_t* name;
    double minimum, maximum;
    bool integer;
    int y;
};
constexpr std::array<Field, 7> Fields{{
    {L"&Time limit (s; 0 = unlimited)", L"time limit", 0, 999, true, 122},
    {L"&Launch interval (s)", L"launch interval", 0, 60, true, 153},
    {L"Mi&nimum initial speed (px/s)", L"minimum initial speed", 1, 1000, true, 184},
    {L"Ma&ximum initial speed (px/s)", L"maximum initial speed", 1, 1000, true, 215},
    {L"Ball &speed multiplier (x)", L"ball speed multiplier", 0.1, 4, false, 290},
    {L"Chevron &boost (x original)", L"chevron boost", 0, 20, false, 321},
    {L"Chevron speed &cap (px/s/axis)", L"chevron speed cap", 1, 2000, false, 352}
}};

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) return L"The settings could not be applied.";
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
std::wstring Number(double value) {
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(10) << value;
    return out.str();
}
UINT WindowDpi(HWND window) {
    using GetDpi = UINT(WINAPI*)(HWND);
    const auto getDpi = std::bit_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpi && window) return getDpi(window);
    HDC dc = GetDC(window);
    const UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc) ReleaseDC(window, dc);
    return dpi ? dpi : 96;
}
void AdjustFrame(RECT& rect, DWORD style, DWORD extended, UINT dpi) {
    using Adjust = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    const auto adjust = std::bit_cast<Adjust>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (adjust) adjust(&rect, style, FALSE, extended, dpi);
    else AdjustWindowRectEx(&rect, style, FALSE, extended);
}
std::array<double, 7> Values(const BoardMechanics& value) {
    return {{static_cast<double>(value.timeLimitSeconds), static_cast<double>(value.launchIntervalSeconds),
        static_cast<double>(value.minBallSpeed), static_cast<double>(value.maxBallSpeed),
        value.ballSpeedMultiplier, value.chevronAccelerationMultiplier, value.chevronSpeedLimit}};
}

class MechanicsDialog {
public:
    MechanicsDialog(HWND owner, HINSTANCE instance, Game& game)
        : owner_(owner), instance_(instance), game_(game), dpi_(WindowDpi(owner)) {}
    ~MechanicsDialog() {
        if (font_) DeleteObject(font_);
        if (headingFont_) DeleteObject(headingFont_);
    }

    bool show() {
        registerClasses();
        RECT desired{0, 0, px(720), px(612)};
        AdjustFrame(desired, Style, ExtendedStyle, dpi_);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &monitor);
        const auto& work = monitor.rcWork;
        const int width = std::min(desired.right - desired.left, work.right - work.left - px(20));
        const int height = std::min(desired.bottom - desired.top, work.bottom - work.top - px(20));
        window_ = CreateWindowExW(ExtendedStyle, DialogClass, L"Board Mechanics - InkBall", Style,
            work.left + (work.right - work.left - width) / 2,
            work.top + (work.bottom - work.top - height) / 2,
            width, height, owner_, nullptr, instance_, this);
        if (!window_) throw std::runtime_error(error_.empty() ? "Windows could not create the mechanics settings window." : error_);
        const bool enableOwner = IsWindowEnabled(owner_) != FALSE;
        if (enableOwner) EnableWindow(owner_, FALSE);
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        SetFocus(edits_[0]);
        SendMessageW(edits_[0], EM_SETSEL, 0, -1);
        MSG message{};
        while (!closed_) {
            const BOOL received = GetMessageW(&message, nullptr, 0, 0);
            if (received <= 0) {
                if (received == 0) PostQuitMessage(static_cast<int>(message.wParam));
                else error_ = "The mechanics settings message loop failed.";
                break;
            }
            if (handleShortcut(message)) continue;
            if (!IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (IsWindow(window_)) DestroyWindow(window_);
        if (enableOwner && IsWindow(owner_)) {
            EnableWindow(owner_, TRUE);
            SetActiveWindow(owner_);
        }
        if (!error_.empty()) throw std::runtime_error(error_);
        return applied_;
    }

private:
    static constexpr DWORD Style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN;
    static constexpr DWORD ExtendedStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    HWND owner_ = nullptr, window_ = nullptr, panel_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Game& game_;
    UINT dpi_ = 96;
    HFONT font_ = nullptr, headingFont_ = nullptr;
    HWND heading_ = nullptr, scope_ = nullptr, currentHeader_ = nullptr, originalHeader_ = nullptr;
    HWND timingGroup_ = nullptr, motionGroup_ = nullptr, notes_ = nullptr, status_ = nullptr;
    HWND okay_ = nullptr, cancel_ = nullptr, apply_ = nullptr, defaults_ = nullptr;
    std::array<HWND, 7> labels_{}, edits_{}, originals_{};
    bool closed_ = false, dirty_ = false, applied_ = false, filling_ = false, layingOut_ = false;
    int scroll_ = 0, wheelRemainder_ = 0;
    std::string error_;

    int px(int logical) const { return MulDiv(logical, static_cast<int>(dpi_), 96); }

    bool handleShortcut(const MSG& message) {
        if (message.message == WM_KEYDOWN && message.wParam == 'A' &&
            (GetKeyState(VK_CONTROL) & 0x8000)) {
            const HWND focus = GetFocus();
            if (std::find(edits_.begin(), edits_.end(), focus) != edits_.end()) {
                SendMessageW(focus, EM_SETSEL, 0, -1);
                return true;
            }
        }
        // Keep form mnemonics reliable across the nested, scrollable container
        // and older standard EDIT implementations as well as current Windows.
        if (message.message == WM_SYSKEYDOWN && (GetKeyState(VK_MENU) & 0x8000)) {
            constexpr std::array<WPARAM, 7> keys{{'T', 'L', 'N', 'X', 'S', 'B', 'C'}};
            const auto found = std::find(keys.begin(), keys.end(), message.wParam);
            if (found != keys.end()) {
                const auto index = static_cast<std::size_t>(found - keys.begin());
                reveal(index);
                SetFocus(edits_[index]);
                SendMessageW(edits_[index], EM_SETSEL, 0, -1);
                return true;
            }
        }
        return false;
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<MechanicsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<MechanicsDialog*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            self->window_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
        try { return self->dispatch(message, wParam, lParam); }
        catch (const std::exception& exception) {
            self->error_ = exception.what();
            if (message == WM_CREATE) return -1;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
    }
    static LRESULT CALLBACK PanelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<MechanicsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<MechanicsDialog*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
        if (message == WM_COMMAND) return SendMessageW(self->window_, message, wParam, lParam);
        if (message == WM_SIZE) { self->layoutPanel(); return 0; }
        if (message == WM_VSCROLL) { self->scrollPanel(LOWORD(wParam)); return 0; }
        if (message == WM_MOUSEWHEEL) {
            self->wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wParam);
            const int lines = self->wheelRemainder_ / WHEEL_DELTA;
            self->wheelRemainder_ -= lines * WHEEL_DELTA;
            self->setScroll(self->scroll_ - lines * self->px(72));
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    static BOOL CALLBACK SetChildFont(HWND hwnd, LPARAM font) {
        SendMessageW(hwnd, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
        return TRUE;
    }

    void registerClasses() {
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type);
        type.hInstance = instance_;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_INKBALL));
        type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        type.lpfnWndProc = WindowProc;
        type.lpszClassName = DialogClass;
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Windows could not register the mechanics settings window.");
        type.lpfnWndProc = PanelProc;
        type.lpszClassName = PanelClass;
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Windows could not register the settings form.");
    }

    HWND control(HWND parent, DWORD extended, const wchar_t* type, const wchar_t* text, DWORD style, int id = 0) {
        const HWND hwnd = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (!hwnd) throw std::runtime_error("Windows could not create a mechanics settings control.");
        return hwnd;
    }

    void createControls() {
        panel_ = CreateWindowExW(WS_EX_CONTROLPARENT, PanelClass, L"Mechanics settings form",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 1, 1, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(PanelId)), instance_, this);
        if (!panel_) throw std::runtime_error("Windows could not create the settings form.");
        const auto& state = game_.state();
        const auto title = L"Board " + std::to_wstring(state.levelId) + L"  |  " +
            Wide(DifficultyName(state.difficulty)) + (state.isBonus ? L"  |  Bonus" : L"  |  Normal");
        heading_ = control(panel_, 0, L"STATIC", title.c_str(), SS_LEFT);
        scope_ = control(panel_, 0, L"STATIC",
            L"Settings apply to this board. Restart keeps them; starting another board restores its original settings.", SS_LEFT);
        currentHeader_ = control(panel_, 0, L"STATIC", L"Value", SS_LEFT);
        originalHeader_ = control(panel_, 0, L"STATIC", L"Original value", SS_LEFT);
        timingGroup_ = control(panel_, 0, L"BUTTON", L"Board timing and initial speeds", BS_GROUPBOX);
        motionGroup_ = control(panel_, 0, L"BUTTON", L"Ball motion and chevrons", BS_GROUPBOX);
        const auto defaults = Values(game_.defaultMechanics());
        for (std::size_t i = 0; i < Fields.size(); ++i) {
            labels_[i] = control(panel_, 0, L"STATIC", Fields[i].label, SS_LEFT | SS_CENTERIMAGE);
            edits_[i] = control(panel_, WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                FirstFieldId + static_cast<int>(i));
            SendMessageW(edits_[i], EM_SETLIMITTEXT, 48, 0);
            originals_[i] = control(panel_, 0, L"STATIC", Number(defaults[i]).c_str(), SS_LEFT | SS_CENTERIMAGE);
        }
        notes_ = control(panel_, 0, L"STATIC",
            L"Ball speed changes moving balls and future launches. Initial speed bounds also apply to preplaced balls after a restart.\r\n"
            L"Changing the time limit resets remaining time. The first queued launch still waits 4 seconds.\r\n"
            L"Chevron boost 1x adds the original 3 px/s each contact step. A boost of 0 disables acceleration; the cap limits speed along the arrow's axis.", SS_LEFT);
        status_ = control(panel_, 0, L"STATIC", L"No changes have been applied.", SS_LEFT, StatusId);
        defaults_ = control(window_, 0, L"BUTTON", L"Restore &Original Defaults", BS_PUSHBUTTON | WS_TABSTOP, DefaultsId);
        okay_ = control(window_, 0, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK);
        cancel_ = control(window_, 0, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);
        apply_ = control(window_, 0, L"BUTTON", L"&Apply", BS_PUSHBUTTON | WS_TABSTOP, ApplyId);
        updateFonts();
        fill(game_.mechanics());
        EnableWindow(apply_, FALSE);
        layout();
    }

    void updateFonts() {
        const auto makeFont = [&](int weight) {
            return CreateFontW(-MulDiv(10, static_cast<int>(dpi_), 72), 0, 0, 0, weight,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        };
        HFONT replacement = makeFont(FW_NORMAL), heading = makeFont(FW_SEMIBOLD);
        if (!replacement || !heading) {
            if (replacement) DeleteObject(replacement);
            if (heading) DeleteObject(heading);
            throw std::runtime_error("Windows could not create the mechanics settings font.");
        }
        EnumChildWindows(window_, SetChildFont, reinterpret_cast<LPARAM>(replacement));
        SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(heading), TRUE);
        if (font_) DeleteObject(font_);
        if (headingFont_) DeleteObject(headingFont_);
        font_ = replacement;
        headingFont_ = heading;
    }

    void layout() {
        if (!apply_) return;
        RECT client{};
        GetClientRect(window_, &client);
        const int footer = px(FooterHeight), gap = px(14), button = px(78), height = px(28);
        MoveWindow(panel_, 0, 0, client.right, std::max(1L, client.bottom - footer), TRUE);
        const int y = client.bottom - px(40);
        MoveWindow(defaults_, gap, y, px(208), height, TRUE);
        MoveWindow(apply_, client.right - gap - button, y, button, height, TRUE);
        MoveWindow(cancel_, client.right - gap - button * 2 - px(10), y, button, height, TRUE);
        MoveWindow(okay_, client.right - gap - button * 3 - px(20), y, button, height, TRUE);
        layoutPanel();
    }

    void layoutPanel() {
        if (!status_ || layingOut_) return;
        layingOut_ = true;
        RECT client{};
        GetClientRect(panel_, &client);
        const int maxScroll = std::max(0, px(DocumentHeight) - static_cast<int>(client.bottom));
        scroll_ = std::clamp(scroll_, 0, maxScroll);
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = px(DocumentHeight) - 1;
        info.nPage = static_cast<UINT>(std::max(1L, client.bottom));
        info.nPos = scroll_;
        SetScrollInfo(panel_, SB_VERT, &info, TRUE);
        GetClientRect(panel_, &client);
        const int width = client.right, pad = px(14), inset = px(28);
        const int valueWidth = px(100), originalWidth = px(118), gap = px(20);
        const int originalX = width - inset - originalWidth;
        const int valueX = originalX - gap - valueWidth;
        const int labelWidth = valueX - gap - inset;
        HDWP positions = BeginDeferWindowPos(31);
        const auto place = [&](HWND hwnd, int x, int y, int w, int h) {
            if (positions) positions = DeferWindowPos(positions, hwnd, nullptr, x, y - scroll_,
                std::max(1, w), std::max(1, h), SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
        };
        place(heading_, pad, px(14), width - pad * 2, px(24));
        place(scope_, pad, px(42), width - pad * 2, px(32));
        place(currentHeader_, valueX, px(77), valueWidth, px(19));
        place(originalHeader_, originalX, px(77), originalWidth, px(19));
        place(timingGroup_, pad, px(96), width - pad * 2, px(155));
        place(motionGroup_, pad, px(264), width - pad * 2, px(124));
        for (std::size_t i = 0; i < Fields.size(); ++i) {
            place(labels_[i], inset, px(Fields[i].y), labelWidth, px(25));
            place(edits_[i], valueX, px(Fields[i].y), valueWidth, px(25));
            place(originals_[i], originalX, px(Fields[i].y), originalWidth, px(25));
        }
        place(notes_, pad, px(402), width - pad * 2, px(106));
        place(status_, pad, px(514), width - pad * 2, px(32));
        if (positions) EndDeferWindowPos(positions);
        // Group boxes have transparent interiors. Repaint the full child tree
        // after a scroll/resize instead of retaining overlapping child pixels.
        RedrawWindow(panel_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        layingOut_ = false;
    }

    void setScroll(int value) { scroll_ = value; layoutPanel(); }
    void scrollPanel(int operation) {
        RECT client{};
        GetClientRect(panel_, &client);
        switch (operation) {
        case SB_LINEUP: setScroll(scroll_ - px(24)); break;
        case SB_LINEDOWN: setScroll(scroll_ + px(24)); break;
        case SB_PAGEUP: setScroll(scroll_ - client.bottom + px(24)); break;
        case SB_PAGEDOWN: setScroll(scroll_ + client.bottom - px(24)); break;
        case SB_TOP: setScroll(0); break;
        case SB_BOTTOM: setScroll(px(DocumentHeight)); break;
        case SB_THUMBPOSITION: case SB_THUMBTRACK: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(panel_, SB_VERT, &info);
            setScroll(info.nTrackPos);
            break;
        }
        default: break;
        }
    }
    void reveal(std::size_t field) {
        RECT client{};
        GetClientRect(panel_, &client);
        const int top = px(Fields[field].y), bottom = top + px(29);
        if (top < scroll_) setScroll(top - px(8));
        else if (bottom > scroll_ + client.bottom) setScroll(bottom - client.bottom + px(8));
    }

    void fill(const BoardMechanics& mechanics) {
        filling_ = true;
        const auto values = Values(mechanics);
        for (std::size_t i = 0; i < values.size(); ++i)
            SetWindowTextW(edits_[i], Number(values[i]).c_str());
        filling_ = false;
    }
    void markDirty() {
        if (filling_) return;
        dirty_ = true;
        EnableWindow(apply_, TRUE);
        SetWindowTextW(status_, applied_ ? L"Pending edits. Cancel keeps the last applied settings."
            : L"Pending edits. Apply updates this board; Cancel discards these edits.");
    }
    bool invalid(std::size_t field, const std::wstring& message) {
        MessageBoxW(window_, message.c_str(), L"Check Mechanics Settings", MB_OK | MB_ICONWARNING);
        reveal(field);
        SetFocus(edits_[field]);
        SendMessageW(edits_[field], EM_SETSEL, 0, -1);
        return false;
    }

    bool apply() {
        if (!dirty_) return true;
        std::array<double, 7> numbers{};
        for (std::size_t i = 0; i < Fields.size(); ++i) {
            wchar_t text[64]{};
            GetWindowTextW(edits_[i], text, static_cast<int>(std::size(text)));
            wchar_t* end = nullptr;
            const double value = std::wcstod(text, &end);
            const bool hasNumber = end && end != text;
            while (end && *end && std::iswspace(*end)) ++end;
            const auto& field = Fields[i];
            if (!hasNumber || *end || !std::isfinite(value) ||
                value < field.minimum || value > field.maximum || (field.integer && std::floor(value) != value)) {
                return invalid(i, std::wstring(L"Enter ") + (field.integer ? L"a whole number" : L"a number") +
                    L" from " + Number(field.minimum) + L" to " + Number(field.maximum) + L" for " + field.name +
                    L".\n\nUse a decimal point for fractional values.");
            }
            numbers[i] = value;
        }
        if (numbers[2] > numbers[3])
            return invalid(3, L"Maximum initial speed must be at least the minimum initial speed.");
        if (numbers[3] * numbers[4] > 2000)
            return invalid(4, L"Maximum initial speed multiplied by the ball speed multiplier must not exceed 2000 px/s.");
        BoardMechanics values;
        values.timeLimitSeconds = static_cast<int>(numbers[0]);
        values.launchIntervalSeconds = static_cast<int>(numbers[1]);
        values.minBallSpeed = static_cast<int>(numbers[2]);
        values.maxBallSpeed = static_cast<int>(numbers[3]);
        values.ballSpeedMultiplier = numbers[4];
        values.chevronAccelerationMultiplier = numbers[5];
        values.chevronSpeedLimit = numbers[6];
        std::string error;
        if (!game_.applyMechanics(values, error)) return invalid(4, Wide(error));
        applied_ = true;
        dirty_ = false;
        fill(game_.mechanics());
        EnableWindow(apply_, FALSE);
        SetWindowTextW(status_, L"Applied to this board. Restart retains these settings; Cancel keeps the applied values.");
        return true;
    }

    LRESULT dispatch(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE: createControls(); return 0;
        case WM_SIZE: if (wParam != SIZE_MINIMIZED) layout(); return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam);
            updateFonts();
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            RECT minimum{0, 0, px(540), px(360)};
            AdjustFrame(minimum, Style, ExtendedStyle, dpi_);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = std::min(minimum.right - minimum.left, monitor.rcWork.right - monitor.rcWork.left);
            limits->ptMinTrackSize.y = std::min(minimum.bottom - minimum.top, monitor.rcWork.bottom - monitor.rcWork.top);
            return 0;
        }
        case DM_GETDEFID: return MAKELRESULT(IDOK, DC_HASDEFID);
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id >= FirstFieldId && id < FirstFieldId + static_cast<int>(Fields.size())) {
                if (HIWORD(wParam) == EN_CHANGE) markDirty();
                if (HIWORD(wParam) == EN_SETFOCUS) reveal(static_cast<std::size_t>(id - FirstFieldId));
                return 0;
            }
            if (id == ApplyId) { apply(); return 0; }
            if (id == IDOK) { if (apply()) DestroyWindow(window_); return 0; }
            if (id == IDCANCEL) { DestroyWindow(window_); return 0; }
            if (id == DefaultsId) {
                fill(game_.defaultMechanics());
                markDirty();
                SetWindowTextW(status_, L"Original defaults loaded into the form. Choose Apply or OK to use them.");
                return 0;
            }
            break;
        }
        case WM_CLOSE: DestroyWindow(window_); return 0;
        case WM_DESTROY: closed_ = true; return 0;
        default: break;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }
};
}

bool ShowMechanicsDialog(HWND owner, HINSTANCE instance, Game& game) {
    MechanicsDialog dialog(owner, instance, game);
    return dialog.show();
}
}
