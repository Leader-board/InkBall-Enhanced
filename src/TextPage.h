#pragma once
#include <windows.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <functional>
#include <array>
#include <vector>
#include <commctrl.h>

namespace inkball {
struct StatisticsTable {
    std::array<std::wstring,6> columns;
    std::vector<std::array<std::wstring,6>> rows;
    bool operator==(const StatisticsTable&) const = default;
};
// Native controls, rebuilt at the destination DPI; no bitmap-scaled text.
class TextPage {
    static constexpr UINT_PTR RefreshTimer = 1;
    HWND window_ = nullptr, text_ = nullptr, close_ = nullptr, refresh_ = nullptr;
    HWND table_ = nullptr, tableTitle_ = nullptr, tableNote_ = nullptr;
    std::function<std::wstring()> snapshot_;
    std::function<StatisticsTable()> tableSnapshot_;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    bool closed_ = false;
    std::wstring lastContent_;
    StatisticsTable lastTable_;
    bool tableInitialized_ = false;
    int px(int v) const { return MulDiv(v, dpi_, 96); }
    void autoSizeColumn(int column) {
        if(column<0||column>=static_cast<int>(lastTable_.columns.size()))return;
        // Native divider auto-sizing fits cell contents only. Measure the
        // header separately; USEHEADER has special fill behavior on the last
        // column, so it cannot reliably size every column to its own text.
        const HWND header=ListView_GetHeader(table_);
        HDC dc=GetDC(header);
        if(!dc)return;
        const auto font=reinterpret_cast<HFONT>(SendMessageW(header,WM_GETFONT,0,0));
        const auto old=font?SelectObject(dc,font):nullptr;
        SIZE text{};const auto& title=lastTable_.columns[column];
        const BOOL measured=GetTextExtentPoint32W(dc,title.c_str(),static_cast<int>(title.size()),&text);
        if(old)SelectObject(dc,old);ReleaseDC(header,dc);
        if(!measured)return;
        ListView_SetColumnWidth(table_,column,LVSCW_AUTOSIZE);
        const int cells=ListView_GetColumnWidth(table_,column);
        ListView_SetColumnWidth(table_,column,std::max(cells,static_cast<int>(text.cx)+px(20)));
    }
    static LRESULT CALLBACK tableProc(HWND w,UINT message,WPARAM a,LPARAM b,UINT_PTR id,DWORD_PTR reference) {
        auto* self=reinterpret_cast<TextPage*>(reference);
        if(message==WM_NOTIFY){
            const auto* notification=reinterpret_cast<NMHDR*>(b);
            if(notification&&notification->hwndFrom==ListView_GetHeader(w)&&
                (notification->code==HDN_DIVIDERDBLCLICKW||notification->code==HDN_DIVIDERDBLCLICKA)){
                self->autoSizeColumn(reinterpret_cast<NMHEADERW*>(b)->iItem);
                return 0; // Suppress the content-only native auto-size.
            }
        }
        if(message==WM_NCDESTROY)RemoveWindowSubclass(w,tableProc,id);
        return DefSubclassProc(w,message,a,b);
    }
    void layout() {
        RECT r{}; GetClientRect(window_, &r);
        int textTop=px(12);
        const int inner=std::max(1L,r.right-px(32));
        if(table_){
            const int tableHeight=std::max(px(60),std::min(px(220),static_cast<int>(r.bottom/3)));
            MoveWindow(tableTitle_,px(16),px(10),inner,px(22),TRUE);
            MoveWindow(table_,px(16),px(34),inner,tableHeight,TRUE);
            MoveWindow(tableNote_,px(16),px(40)+tableHeight,inner,px(52),TRUE);
            const int widths[]={65,105,115,145,190,115};
            for(int i=0;i<6;++i)ListView_SetColumnWidth(table_,i,
                i==4?std::max(px(widths[i]),inner-px(549)-GetSystemMetricsForDpi(SM_CXVSCROLL,dpi_)):px(widths[i]));
            textTop=px(98)+tableHeight;
        }
        MoveWindow(text_, px(16), textTop, inner,
            std::max(1L,r.bottom-textTop-px(54)), TRUE);
        if(refresh_)MoveWindow(refresh_,px(16),std::max(0L,r.bottom-px(42)),px(100),px(28),TRUE);
        MoveWindow(close_, std::max(0L,r.right-px(104)), std::max(0L,r.bottom-px(42)),px(88),px(28),TRUE);
    }
    void font() {
        HFONT next=CreateFontW(-MulDiv(10,dpi_,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!next) return;
        SendMessageW(text_,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        SendMessageW(close_,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(refresh_)SendMessageW(refresh_,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        for(HWND control:{table_,tableTitle_,tableNote_})
            if(control)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(table_)SendMessageW(ListView_GetHeader(table_),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font_)DeleteObject(font_);font_=next;
    }
    void refreshSnapshot() {
        const auto content=snapshot_();
        const auto data=tableSnapshot_?tableSnapshot_():StatisticsTable{};
        if(content!=lastContent_){
            const auto firstLine=SendMessageW(text_,EM_GETFIRSTVISIBLELINE,0,0);
            DWORD start=0,end=0;SendMessageW(text_,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
            SendMessageW(text_,WM_SETREDRAW,FALSE,0);
            SetWindowTextW(text_,content.c_str());
            SendMessageW(text_,EM_SETSEL,start,end);
            const auto currentLine=SendMessageW(text_,EM_GETFIRSTVISIBLELINE,0,0);
            SendMessageW(text_,EM_LINESCROLL,0,firstLine-currentLine);
            SendMessageW(text_,WM_SETREDRAW,TRUE,0);InvalidateRect(text_,nullptr,FALSE);
            lastContent_=content;
        }
        if(!table_)return;
        if(tableInitialized_&&data==lastTable_)return;
        const int top=ListView_GetTopIndex(table_);
        const int selected=ListView_GetNextItem(table_,-1,LVNI_SELECTED);
        SendMessageW(table_,WM_SETREDRAW,FALSE,0);
        ListView_DeleteAllItems(table_);
        for(int column=0;column<static_cast<int>(data.columns.size());++column){
            LVCOLUMNW c{};c.mask=LVCF_TEXT;c.pszText=const_cast<wchar_t*>(data.columns[column].c_str());
            ListView_SetColumn(table_,column,&c);
            HDITEMW heading{};heading.mask=HDI_TEXT|HDI_FORMAT;
            heading.pszText=const_cast<wchar_t*>(data.columns[column].c_str());heading.fmt=HDF_STRING|HDF_RIGHT;
            Header_SetItem(ListView_GetHeader(table_),column,&heading);
        }
        for(std::size_t row=0;row<data.rows.size();++row){
            LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(row);
            item.pszText=const_cast<wchar_t*>(data.rows[row][0].c_str());ListView_InsertItem(table_,&item);
            for(int column=1;column<static_cast<int>(data.columns.size());++column)
                ListView_SetItemText(table_,static_cast<int>(row),column,const_cast<wchar_t*>(data.rows[row][column].c_str()));
        }
        SetWindowTextW(tableTitle_,data.rows.empty()?L"Completed boards — current run (none yet)":L"Completed boards — current run");
        if(selected>=0&&static_cast<std::size_t>(selected)<data.rows.size())
            ListView_SetItemState(table_,selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        RECT row{};
        if(top>0&&ListView_GetItemRect(table_,0,&row,LVIR_BOUNDS))ListView_Scroll(table_,0,top*(row.bottom-row.top));
        SendMessageW(table_,WM_SETREDRAW,TRUE,0);InvalidateRect(table_,nullptr,TRUE);
        lastTable_=data;tableInitialized_=true;
    }
    static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) {
        auto* self=reinterpret_cast<TextPage*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE) {
            self=static_cast<TextPage*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);
            self->window_=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self)return DefWindowProcW(w,m,a,b);
        switch(m) {
        case WM_TIMER:
            if(a==RefreshTimer&&self->snapshot_){
                try { self->refreshSnapshot(); }
                catch (...) { KillTimer(w,RefreshTimer); }
            }
            return 0;
        case WM_SIZE: if(self->text_)self->layout();return 0;
        case WM_DPICHANGED: {
            self->dpi_=HIWORD(a);self->font();const auto& r=*reinterpret_cast<RECT*>(b);
            SetWindowPos(w,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
            self->layout();return 0;
        }
        case WM_COMMAND:
            if(LOWORD(a)==1001 && self->snapshot_) {
                try {
                    self->refreshSnapshot();
                } catch (...) {
                    MessageBoxW(w,L"The statistics snapshot could not be refreshed.",L"InkBall",MB_OK|MB_ICONERROR);
                }
                return 0;
            }
            if(LOWORD(a)==IDOK||LOWORD(a)==IDCANCEL)DestroyWindow(w);return 0;
        case WM_CLOSE: DestroyWindow(w);return 0;
        case WM_DESTROY:KillTimer(w,RefreshTimer);self->closed_=true;return 0;
        case WM_NCDESTROY:self->window_=self->text_=self->close_=self->refresh_=nullptr;
            self->table_=self->tableTitle_=self->tableNote_=nullptr;break;
        }
        return DefWindowProcW(w,m,a,b);
    }
public:
    ~TextPage(){if(IsWindow(window_))DestroyWindow(window_);if(font_)DeleteObject(font_);}
    bool isOpen() const { return window_ && IsWindow(window_); }
    bool translate(MSG& message) { return isOpen() && IsDialogMessageW(window_,&message); }
    void showModeless(HWND owner,const wchar_t* title,std::function<std::wstring()> snapshot,
        std::function<StatisticsTable()> tableSnapshot={}) {
        snapshot_=std::move(snapshot);
        tableSnapshot_=std::move(tableSnapshot);
        if(isOpen()){refreshSnapshot();ShowWindow(window_,SW_RESTORE);SetForegroundWindow(window_);return;}
        show(owner,title,snapshot_(),false,true);
    }
    void show(HWND owner,const wchar_t* title,const std::wstring& content,bool compact=false,bool modeless=false) {
        closed_=false;
        tableInitialized_=false;lastContent_.clear();lastTable_={};
        const auto previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        struct Restore { DPI_AWARENESS_CONTEXT context; ~Restore(){if(context)SetThreadDpiAwarenessContext(context);} } restore{previous};
        HINSTANCE module=GetModuleHandleW(nullptr);
        WNDCLASSW wc{};wc.hInstance=module;wc.lpfnWndProc=proc;wc.lpszClassName=L"InkBall.TextPage";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_WINDOW);
        if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot create the information page.");
        dpi_=GetDpiForWindow(owner);if(!dpi_)dpi_=96;
        MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&mi);
        const int width=std::min(px(compact?480:tableSnapshot_?940:650),static_cast<int>(mi.rcWork.right-mi.rcWork.left));
        const int height=std::min(px(compact?210:680),static_cast<int>(mi.rcWork.bottom-mi.rcWork.top));
        window_=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,title,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
            mi.rcWork.left+(mi.rcWork.right-mi.rcWork.left-width)/2,mi.rcWork.top+(mi.rcWork.bottom-mi.rcWork.top-height)/2,
            width,height,owner,nullptr,module,this);
        if(!window_)throw std::runtime_error("Cannot create the information page.");
        text_=CreateWindowExW(0,compact?L"STATIC":L"EDIT",content.c_str(),WS_CHILD|WS_VISIBLE|
            (compact?SS_NOPREFIX:(WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL)),
            0,0,1,1,window_,nullptr,module,nullptr);
        close_=CreateWindowExW(0,L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            0,0,1,1,window_,reinterpret_cast<HMENU>(IDOK),module,nullptr);
        if(!text_||!close_){DestroyWindow(window_);throw std::runtime_error("Cannot create information controls.");}
        if(modeless)refresh_=CreateWindowExW(0,L"BUTTON",L"Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            0,0,1,1,window_,reinterpret_cast<HMENU>(1001),module,nullptr);
        if(modeless&&tableSnapshot_){
            INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
            tableTitle_=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,window_,nullptr,module,nullptr);
            table_=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"Completed boards",
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
                0,0,1,1,window_,reinterpret_cast<HMENU>(1002),module,nullptr);
            tableNote_=CreateWindowExW(0,L"STATIC",
                L"Time used is active play, including launch wait; available time is the configured limit. No time bonus.\r\nMaximum includes possible recoloring and breakable squares; it is a scoring upper bound, not a route guarantee.",
                WS_CHILD|WS_VISIBLE,0,0,1,1,window_,nullptr,module,nullptr);
            if(!table_||!tableTitle_||!tableNote_){DestroyWindow(window_);throw std::runtime_error("Cannot create completed-board table.");}
            if(!SetWindowSubclass(table_,tableProc,1,reinterpret_cast<DWORD_PTR>(this))){
                DestroyWindow(window_);throw std::runtime_error("Cannot initialize table column sizing.");
            }
            ListView_SetExtendedListViewStyle(table_,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
            ListView_SetUnicodeFormat(table_,TRUE);
            const auto headings=tableSnapshot_().columns;
            for(int i=0;i<static_cast<int>(headings.size());++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT|LVCF_SUBITEM;
                column.pszText=const_cast<wchar_t*>(headings[i].c_str());column.iSubItem=i;
                column.cx=px(110);column.fmt=LVCFMT_RIGHT;ListView_InsertColumn(table_,i,&column);}
            ShowWindow(ListView_GetHeader(table_),SW_SHOW);
            refreshSnapshot();
        }
        font();layout();
        if(modeless){
            if(!SetTimer(window_,RefreshTimer,1000,nullptr)){
                DestroyWindow(window_);throw std::runtime_error("Cannot start statistics refresh.");
            }
            ShowWindow(window_,SW_SHOW);SetFocus(close_);return;
        }
        const bool enabled=IsWindowEnabled(owner)!=FALSE;if(enabled)EnableWindow(owner,FALSE);
        ShowWindow(window_,SW_SHOW);SetFocus(close_);
        MSG msg{};
        while(!closed_) {
            const auto result=GetMessageW(&msg,nullptr,0,0);
            if(result<=0){if(!result)PostQuitMessage(static_cast<int>(msg.wParam));break;}
            if(!IsDialogMessageW(window_,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        if(IsWindow(window_))DestroyWindow(window_);
        if(enabled&&IsWindow(owner)){EnableWindow(owner,TRUE);SetActiveWindow(owner);}
    }
};
}
