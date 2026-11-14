#include "BoardBrowser.h"
#include "BoardCatalog.h"
#include "Renderer.h"
#include "resource.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <string>

namespace inkball {
namespace {
constexpr wchar_t BrowserClass[] = L"InkBall.BoardBrowser.Window";
constexpr wchar_t PreviewClass[] = L"InkBall.BoardBrowser.Preview";
constexpr int DifficultyId = 2101, KindId = 2102, SortId = 2103;
constexpr int ListId = 2104, PreviewId = 2105, DetailsId = 2106;
constexpr int PlayId = IDOK, CloseId = IDCANCEL;
constexpr UINT UpdatePreview = WM_APP + 40;

std::wstring Wide(const std::string& text) {
    // Catalog labels and the extracted parameter values are deliberately ASCII.
    return std::wstring(text.begin(), text.end());
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

class Browser {
public:
    Browser(HWND owner, HINSTANCE instance, const std::vector<Level>& levels, int currentLevelId)
        : owner_(owner), instance_(instance), levels_(levels), previewGame_(levels), dpi_(WindowDpi(owner)) {
        for (std::size_t i = 0; i < levels.size(); ++i)
            if (levels[i].id == currentLevelId) { selected_ = i; break; }
        rows_.reserve(levels.size());
        for (const auto& level : levels) {
            const auto stats = InspectBoard(level);
            rows_.push_back({std::to_wstring(level.id), Wide(DifficultyName(level.difficulty)),
                level.bonus ? L"Bonus" : L"Normal", Wide(BoardTimeText(level)),
                std::to_wstring(level.queue.size()), std::to_wstring(stats.tileKinds[9])});
        }
        renderer_.initialize(instance);
    }
    ~Browser() { if (font_) DeleteObject(font_); }

    std::optional<std::size_t> show() {
        registerClasses();
        const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        const DWORD extended = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
        RECT desired{0, 0, px(1120), px(780)};
        AdjustFrame(desired, style, extended, dpi_);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT work = monitor.rcWork;
        const int width = std::min(static_cast<int>(desired.right - desired.left),
            static_cast<int>(work.right - work.left) - px(24));
        const int height = std::min(static_cast<int>(desired.bottom - desired.top),
            static_cast<int>(work.bottom - work.top) - px(24));
        const int x = work.left + (work.right - work.left - width) / 2;
        const int y = work.top + (work.bottom - work.top - height) / 2;
        window_ = CreateWindowExW(extended, BrowserClass, L"Browse Boards - InkBall",
            style, x, y, width, height, owner_, nullptr, instance_, this);
        if (!window_) throw std::runtime_error("Windows could not create the board browser.");
        const bool enableOwner = IsWindowEnabled(owner_) != FALSE;
        if (enableOwner) EnableWindow(owner_, FALSE);
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        SetFocus(list_);
        MSG message{};
        while (!closed_) {
            const BOOL received = GetMessageW(&message, nullptr, 0, 0);
            if (received <= 0) {
                if (received == 0) PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
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
        return result_;
    }

private:
    HWND owner_ = nullptr, window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    const std::vector<Level>& levels_;
    Game previewGame_;
    Renderer renderer_;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HWND difficultyLabel_ = nullptr, kindLabel_ = nullptr, sortLabel_ = nullptr;
    HWND difficulty_ = nullptr, kind_ = nullptr, sort_ = nullptr, count_ = nullptr;
    HWND list_ = nullptr, previewLabel_ = nullptr, preview_ = nullptr, details_ = nullptr;
    HWND hint_ = nullptr, play_ = nullptr, close_ = nullptr;
    bool populating_ = false, hasPreview_ = false, closed_ = false;
    bool previewQueued_ = false;
    int layoutWidth_ = -1, layoutHeight_ = -1;
    UINT layoutDpi_ = 0;
    std::string error_;
    std::vector<std::size_t> visible_;
    std::vector<std::array<std::wstring, 6>> rows_;
    std::optional<std::size_t> selected_, result_;
    int tileColumn_ = -1, tileRow_ = -1;

    int px(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    void autoSizeColumn(int column) {
        if(column<0||column>=6)return;
        HWND header=ListView_GetHeader(list_);
        wchar_t title[256]{};HDITEMW item{};
        item.mask=HDI_TEXT|HDI_FORMAT;item.pszText=title;item.cchTextMax=256;
        if(!Header_GetItem(header,column,&item))return;
        HDC dc=GetDC(header);if(!dc)return;
        auto font=reinterpret_cast<HFONT>(SendMessageW(header,WM_GETFONT,0,0));
        const auto old=font?SelectObject(dc,font):nullptr;
        SIZE size{};const BOOL measured=GetTextExtentPoint32W(dc,title,lstrlenW(title),&size);
        const int padding=px(20)+((item.fmt&(HDF_SORTUP|HDF_SORTDOWN))?px(12):0);
        int width=static_cast<int>(size.cx)+padding;
        const auto cellFont=reinterpret_cast<HFONT>(SendMessageW(list_,WM_GETFONT,0,0));
        if(cellFont)SelectObject(dc,cellFont);
        // This is an owner-data list: use the cached filtered rows rather
        // than native auto-size, which may only measure visible/cached cells.
        for(const auto index:visible_){
            const auto& text=rows_[index][column];SIZE cell{};
            if(GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&cell))
                width=std::max(width,static_cast<int>(cell.cx)+px(16));
        }
        if(old)SelectObject(dc,old);ReleaseDC(header,dc);if(!measured)return;
        ListView_SetColumnWidth(list_,column,width);
    }

    static LRESULT CALLBACK ListProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR id,DWORD_PTR reference) {
        auto* self=reinterpret_cast<Browser*>(reference);
        if(message==WM_NOTIFY){
            const auto* header=reinterpret_cast<NMHDR*>(lParam);
            if(header&&header->hwndFrom==ListView_GetHeader(hwnd)&&
                (header->code==HDN_DIVIDERDBLCLICKW||header->code==HDN_DIVIDERDBLCLICKA)){
                self->autoSizeColumn(reinterpret_cast<NMHEADERW*>(lParam)->iItem);return 0;
            }
        }
        if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,ListProc,id);
        return DefSubclassProc(hwnd,message,wParam,lParam);
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Browser*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Browser*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
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

    static LRESULT CALLBACK PreviewProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Browser*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Browser*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            try {
                if (self->hasPreview_) {
                    self->renderer_.present(dc, client);
                    self->highlightTile(dc, client);
                } else FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
            } catch (const std::exception& exception) {
                self->error_ = exception.what();
                PostMessageW(self->window_, WM_CLOSE, 0, 0);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        if (message == WM_LBUTTONDOWN) {
            try {
            // A click may follow a selection before its posted preview update.
            // Inspect the board that is selected, never a stale preview image.
            if (self->previewQueued_) self->renderSelectedBoard();
            RECT client{};
            GetClientRect(hwnd, &client);
            Vec2 point;
            if (self->hasPreview_ && self->renderer_.toBoard(
                {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, client, point)) {
                self->tileColumn_ = std::clamp(static_cast<int>(point.x) / TileSize, 0, BoardColumns - 1);
                self->tileRow_ = std::clamp(static_cast<int>(point.y) / TileSize, 0, BoardRows - 1);
                self->updateDetails();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            } catch (const std::exception& exception) {
                self->error_ = exception.what();
                PostMessageW(self->window_, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void registerClasses() {
        INITCOMMONCONTROLSEX common{sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES};
        if (!InitCommonControlsEx(&common)) throw std::runtime_error("Windows could not initialize the board list.");
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type);
        type.hInstance = instance_;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_INKBALL));
        type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        type.lpfnWndProc = WindowProc;
        type.lpszClassName = BrowserClass;
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Windows could not register the board browser.");
        type.lpfnWndProc = PreviewProc;
        type.lpszClassName = PreviewClass;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = nullptr;
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Windows could not register the board preview.");
    }

    HWND control(DWORD extended, const wchar_t* type, const wchar_t* text,
        DWORD style, int id = 0, void* data = nullptr) {
        HWND result = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, data);
        if (!result) throw std::runtime_error("Windows could not create a board-browser control.");
        return result;
    }

    void createControls() {
        difficultyLabel_ = control(0, L"STATIC", L"&Difficulty", 0);
        difficulty_ = control(0, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, DifficultyId);
        SendMessageW(difficulty_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All difficulties"));
        for (int i = 0; i < 5; ++i) {
            const auto text = Wide(DifficultyName(i));
            SendMessageW(difficulty_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        SendMessageW(difficulty_, CB_SETCURSEL, 0, 0);
        kindLabel_ = control(0, L"STATIC", L"Board &type", 0);
        kind_ = control(0, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, KindId);
        for (const auto* text : {L"Normal and bonus", L"Normal only", L"Bonus only"})
            SendMessageW(kind_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(kind_, CB_SETCURSEL, 0, 0);
        sortLabel_ = control(0, L"STATIC", L"&Sort by", 0);
        sort_ = control(0, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, SortId);
        for (const auto* text : {L"Board ID", L"Time: shortest first", L"Time: longest first"})
            SendMessageW(sort_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(sort_, CB_SETCURSEL, 0, 0);
        count_ = control(0, L"STATIC", L"", SS_LEFT);
        list_ = control(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"Boards",
            LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, ListId);
        if(!SetWindowSubclass(list_,ListProc,1,reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("Windows could not initialize board-column sizing.");
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        const wchar_t* names[] = {L"ID", L"Difficulty", L"Type", L"Time allowed", L"Queued", L"On board"};
        for (int i = 0; i < 6; ++i) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
            column.pszText = const_cast<LPWSTR>(names[i]);
            column.cx = px(72);
            column.iSubItem = i;
            column.fmt = i == 0 || i >= 4 ? LVCFMT_RIGHT : LVCFMT_LEFT;
            ListView_InsertColumn(list_, i, &column);
        }
        previewLabel_ = control(0, L"STATIC", L"Preview - click a tile to inspect its parameters", 0);
        preview_ = control(WS_EX_CLIENTEDGE, PreviewClass, L"Board preview", 0, PreviewId, this);
        details_ = control(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
            WS_VSCROLL | WS_TABSTOP, DetailsId);
        SendMessageW(details_, EM_SETLIMITTEXT, 65535, 0);
        hint_ = control(0, L"STATIC", L"Browsing pauses your game. Play starts the selected board with a new score.", SS_LEFT);
        play_ = control(0, L"BUTTON", L"&Play Selected Board", BS_DEFPUSHBUTTON | WS_TABSTOP, PlayId);
        close_ = control(0, L"BUTTON", L"&Close", BS_PUSHBUTTON | WS_TABSTOP, CloseId);
        updateFont();
        rebuildList();
        layout();
    }

    void updateFont() {
        HFONT replacement = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!replacement) throw std::runtime_error("Windows could not create the board-browser font.");
        const HWND controls[] = {difficultyLabel_, difficulty_, kindLabel_, kind_, sortLabel_, sort_, count_,
            list_, previewLabel_, details_, hint_, play_, close_};
        for (HWND hwnd : controls)
            if (hwnd) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
        if(list_)SendMessageW(ListView_GetHeader(list_),WM_SETFONT,reinterpret_cast<WPARAM>(replacement),TRUE);
        if (font_) DeleteObject(font_);
        font_ = replacement;
    }

    void layout() {
        if (!list_) return;
        RECT client{};
        GetClientRect(window_, &client);
        const int width = client.right, height = client.bottom;
        if (width == layoutWidth_ && height == layoutHeight_ && dpi_ == layoutDpi_) return;
        layoutWidth_ = width;
        layoutHeight_ = height;
        layoutDpi_ = dpi_;
        const int gap = px(12), top = px(70), bottom = px(48);
        const int listWidth = std::max(px(390), (width - gap * 3) * 44 / 100);
        const int rightX = listWidth + gap * 2, rightWidth = width - rightX - gap;
        const int bodyHeight = height - top - bottom;
        const int previewHeight = std::min(static_cast<int>(rightWidth * (586.0 / 544.0)),
            std::max(px(230), bodyHeight * 60 / 100));
        const int detailsY = top + px(23) + previewHeight + gap;
        struct Placement { HWND window; int x, y, width, height; };
        const std::array<Placement, 14> positions{{
            {difficultyLabel_, gap, px(8), px(170), px(18)},
            {difficulty_, gap, px(27), px(170), px(200)},
            {kindLabel_, gap + px(182), px(8), px(170), px(18)},
            {kind_, gap + px(182), px(27), px(170), px(150)},
            {sortLabel_, gap + px(364), px(8), px(210), px(18)},
            {sort_, gap + px(364), px(27), px(210), px(160)},
            {count_, gap + px(590), px(23), width - gap * 2 - px(590), px(35)},
            {list_, gap, top, listWidth, bodyHeight},
            {previewLabel_, rightX, top, rightWidth, px(21)},
            {preview_, rightX, top + px(23), rightWidth, previewHeight},
            {details_, rightX, detailsY, rightWidth, height - bottom - detailsY},
            {hint_, gap, height - px(37), width - px(320) - gap * 2, px(33)},
            {play_, width - gap - px(286), height - px(36), px(190), px(26)},
            {close_, width - gap - px(84), height - px(36), px(84), px(26)}
        }};
        // MoveWindow(..., TRUE) forces an immediate paint after every child.
        // Batch placement so Windows paints the completed layout once instead.
        HDWP batch = BeginDeferWindowPos(static_cast<int>(positions.size()));
        for (const auto& position : positions) {
            if (!batch) break;
            batch = DeferWindowPos(batch, position.window, nullptr, position.x, position.y,
                std::max(1, position.width), std::max(1, position.height), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (!batch || !EndDeferWindowPos(batch)) {
            for (const auto& position : positions)
                SetWindowPos(position.window, nullptr, position.x, position.y,
                    std::max(1, position.width), std::max(1, position.height), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        const int innerWidth = listWidth - GetSystemMetrics(SM_CXVSCROLL) - px(5);
        const int fixedWidths[] = {44, 102, 60, 100, 61, 70};
        for (int i = 0; i < 6; ++i) {
            const int columnWidth = i == 1 ? std::max(px(102), innerWidth - px(335)) : px(fixedWidths[i]);
            if (ListView_GetColumnWidth(list_, i) != columnWidth)
                ListView_SetColumnWidth(list_, i, columnWidth);
        }
        InvalidateRect(preview_, nullptr, FALSE);
    }

    BoardSort currentSort() const {
        const auto selection = SendMessageW(sort_, CB_GETCURSEL, 0, 0);
        return selection == 1 ? BoardSort::TimeAscending : selection == 2 ? BoardSort::TimeDescending : BoardSort::Id;
    }

    void updateSortIndicator() {
        const auto sort = currentSort();
        HWND header = ListView_GetHeader(list_);
        for (int i = 0; i < 6; ++i) {
            HDITEMW item{};
            item.mask = HDI_FORMAT;
            Header_GetItem(header, i, &item);
            item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
            if (i == 3 && sort != BoardSort::Id)
                item.fmt |= sort == BoardSort::TimeAscending ? HDF_SORTUP : HDF_SORTDOWN;
            if (i == 0 && sort == BoardSort::Id) item.fmt |= HDF_SORTUP;
            Header_SetItem(header, i, &item);
        }
    }

    void rebuildList() {
        const int difficulty = static_cast<int>(SendMessageW(difficulty_, CB_GETCURSEL, 0, 0)) - 1;
        const auto kindValue = SendMessageW(kind_, CB_GETCURSEL, 0, 0);
        const auto kind = kindValue == 1 ? BoardKindFilter::Normal :
            kindValue == 2 ? BoardKindFilter::Bonus : BoardKindFilter::All;
        visible_ = FilterBoards(levels_, difficulty, kind, currentSort());
        populating_ = true;
        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        // The virtual list asks for visible text through LVN_GETDISPINFO. A
        // filter/sort no longer inserts 551 rows and sends 3,306 cell strings.
        ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemCountEx(list_, static_cast<int>(visible_.size()),
            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        int selectedRow = 0;
        for (std::size_t row = 0; row < visible_.size(); ++row) {
            const auto index = visible_[row];
            if (selected_ && *selected_ == index) selectedRow = static_cast<int>(row);
        }
        if (!visible_.empty()) {
            ListView_SetItemState(list_, selectedRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list_, selectedRow, FALSE);
        }
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list_, nullptr, TRUE);
        populating_ = false;
        updateSortIndicator();
        const auto count = std::to_wstring(visible_.size()) + L" of " + std::to_wstring(levels_.size()) +
            L" boards\r\nUnlimited time sorts as infinity";
        SetWindowTextW(count_, count.c_str());
        EnableWindow(play_, !visible_.empty());
        if (!visible_.empty()) selectBoard(visible_[static_cast<std::size_t>(selectedRow)]);
        else {
            selected_.reset();
            hasPreview_ = false;
            SetWindowTextW(details_, L"No boards match the selected filters.");
            InvalidateRect(preview_, nullptr, FALSE);
        }
    }

    void selectBoard(std::size_t index) {
        if (hasPreview_ && selected_ && *selected_ == index) return;
        selected_ = index;
        tileColumn_ = tileRow_ = -1;
        if (!hasPreview_) { renderSelectedBoard(); return; }
        if (!previewQueued_) {
            previewQueued_ = true;
            if (!PostMessageW(window_, UpdatePreview, 0, 0)) renderSelectedBoard();
        }
    }

    void renderSelectedBoard() {
        previewQueued_ = false;
        if (!selected_) return;
        const auto index = *selected_;
        previewGame_.loadLevel(index);
        GameState image = previewGame_.state();
        // Settle the queue's visual-only entrance without running any physics.
        // Keep the resource's stored order visible even when Play will shuffle it.
        image.queue = levels_[index].queue;
        image.elapsed = 4;
        renderer_.draw(image);
        hasPreview_ = true;
        updateDetails();
        InvalidateRect(preview_, nullptr, FALSE);
    }

    void updateDetails() {
        if (!selected_) return;
        const auto& level = levels_[*selected_];
        const auto text = Wide(DescribeBoardTile(level, tileColumn_, tileRow_) + DescribeBoard(level));
        SetWindowTextW(details_, text.c_str());
        SendMessageW(details_, EM_SETSEL, 0, 0);
        SendMessageW(details_, EM_SCROLLCARET, 0, 0);
    }

    void highlightTile(HDC dc, const RECT& client) const {
        if (tileColumn_ < 0 || tileRow_ < 0) return;
        const int availableWidth = client.right - client.left, availableHeight = client.bottom - client.top;
        const double scale = std::min(static_cast<double>(availableWidth) / Renderer::Width,
            static_cast<double>(availableHeight) / Renderer::Height);
        const int width = static_cast<int>(std::floor(Renderer::Width * scale));
        const int height = static_cast<int>(std::floor(Renderer::Height * scale));
        const int left = client.left + (availableWidth - width) / 2;
        const int top = client.top + (availableHeight - height) / 2;
        const int x1 = left + MulDiv(tileColumn_ * TileSize, width, Renderer::Width);
        const int y1 = top + MulDiv(HudHeight + tileRow_ * TileSize, height, Renderer::Height);
        const int x2 = left + MulDiv((tileColumn_ + 1) * TileSize, width, Renderer::Width);
        const int y2 = top + MulDiv(HudHeight + (tileRow_ + 1) * TileSize, height, Renderer::Height);
        const auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        HPEN pen = CreatePen(PS_SOLID, std::max(1, px(2)), RGB(255, 224, 0));
        const auto oldPen = SelectObject(dc, pen);
        Rectangle(dc, x1, y1, x2, y2);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
    }

    LRESULT dispatch(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE: createControls(); return 0;
        case UpdatePreview:
            if (previewQueued_) renderSelectedBoard();
            return 0;
        case WM_SIZE: if (wParam != SIZE_MINIMIZED) layout(); return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam);
            updateFont();
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* bounds = reinterpret_cast<MINMAXINFO*>(lParam);
            RECT minimum{0, 0, px(920), px(620)};
            AdjustFrame(minimum, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, dpi_);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
            bounds->ptMinTrackSize.x = std::min(minimum.right - minimum.left, monitor.rcWork.right - monitor.rcWork.left);
            bounds->ptMinTrackSize.y = std::min(minimum.bottom - minimum.top, monitor.rcWork.bottom - monitor.rcWork.top);
            return 0;
        }
        case DM_GETDEFID: return MAKELRESULT(PlayId, DC_HASDEFID);
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if ((id == DifficultyId || id == KindId || id == SortId) && HIWORD(wParam) == CBN_SELCHANGE) {
                rebuildList();
                return 0;
            }
            if (id == PlayId && selected_) { result_ = selected_; DestroyWindow(window_); return 0; }
            if (id == CloseId) { DestroyWindow(window_); return 0; }
            break;
        }
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->hwndFrom != list_) break;
            if (header->code == LVN_GETDISPINFOW) {
                auto* information = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                const int row = information->item.iItem, column = information->item.iSubItem;
                if ((information->item.mask & LVIF_TEXT) && row >= 0 && column >= 0 && column < 6 &&
                    static_cast<std::size_t>(row) < visible_.size())
                    information->item.pszText = const_cast<LPWSTR>(
                        rows_[visible_[static_cast<std::size_t>(row)]][static_cast<std::size_t>(column)].c_str());
                return 0;
            }
            if (header->code == LVN_ODFINDITEMW) {
                const auto* find = reinterpret_cast<const NMLVFINDITEMW*>(lParam);
                if (!(find->lvfi.flags & (LVFI_STRING | LVFI_PARTIAL)) ||
                    !find->lvfi.psz || visible_.empty()) return -1;
                const std::wstring text = find->lvfi.psz;
                const auto start = static_cast<std::size_t>(std::max(0, find->iStart));
                for (std::size_t offset = 0; offset < visible_.size(); ++offset) {
                    const auto item = start + offset;
                    if (item >= visible_.size() && !(find->lvfi.flags & LVFI_WRAP)) break;
                    const auto row = item % visible_.size();
                    const auto& id = rows_[visible_[row]][0];
                    if ((find->lvfi.flags & LVFI_PARTIAL) ? id.starts_with(text) : id == text)
                        return static_cast<LRESULT>(row);
                }
                return -1;
            }
            if (header->code == LVN_ITEMCHANGED && !populating_) {
                const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
                if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED) &&
                    change->iItem >= 0 && static_cast<std::size_t>(change->iItem) < visible_.size())
                    selectBoard(visible_[static_cast<std::size_t>(change->iItem)]);
            } else if (header->code == LVN_COLUMNCLICK) {
                const auto* click = reinterpret_cast<const NMLISTVIEW*>(lParam);
                if (click->iSubItem == 0 || click->iSubItem == 3) {
                    const int sort = click->iSubItem == 0 ? 0 : currentSort() == BoardSort::TimeAscending ? 2 : 1;
                    SendMessageW(sort_, CB_SETCURSEL, sort, 0);
                    rebuildList();
                }
            } else if (header->code == NM_DBLCLK) {
                const auto* click = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
                if (click->iItem >= 0 && selected_) {
                    result_ = selected_;
                    DestroyWindow(window_);
                }
            }
            return 0;
        }
        case WM_CLOSE: DestroyWindow(window_); return 0;
        case WM_DESTROY: closed_ = true; return 0;
        default: break;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }
};
}

std::optional<std::size_t> ShowBoardBrowser(HWND owner, HINSTANCE instance,
    const std::vector<Level>& levels, int currentLevelId) {
    if (levels.empty()) return std::nullopt;
    Browser browser(owner, instance, levels, currentLevelId);
    return browser.show();
}
}
