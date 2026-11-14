#pragma once
#include <windows.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <functional>

namespace inkball {
// Native controls, rebuilt at the destination DPI; no bitmap-scaled text.
class TextPage {
    HWND window_ = nullptr, text_ = nullptr, close_ = nullptr, refresh_ = nullptr;
    std::function<std::wstring()> snapshot_;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    bool closed_ = false;
    int px(int v) const { return MulDiv(v, dpi_, 96); }
    void layout() {
        RECT r{}; GetClientRect(window_, &r);
        MoveWindow(text_, px(16), px(12), std::max(1L,r.right-px(32)),
            std::max(1L,r.bottom-px(66)), TRUE);
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
        if(font_)DeleteObject(font_);font_=next;
    }
    static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) {
        auto* self=reinterpret_cast<TextPage*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE) {
            self=static_cast<TextPage*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);
            self->window_=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self)return DefWindowProcW(w,m,a,b);
        switch(m) {
        case WM_SIZE: if(self->text_)self->layout();return 0;
        case WM_DPICHANGED: {
            self->dpi_=HIWORD(a);self->font();const auto& r=*reinterpret_cast<RECT*>(b);
            SetWindowPos(w,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
            self->layout();return 0;
        }
        case WM_COMMAND:
            if(LOWORD(a)==1001 && self->snapshot_) {
                try {
                    const auto content=self->snapshot_();SetWindowTextW(self->text_,content.c_str());
                } catch (...) {
                    MessageBoxW(w,L"The statistics snapshot could not be refreshed.",L"InkBall",MB_OK|MB_ICONERROR);
                }
                return 0;
            }
            if(LOWORD(a)==IDOK||LOWORD(a)==IDCANCEL)DestroyWindow(w);return 0;
        case WM_CLOSE: DestroyWindow(w);return 0;
        case WM_DESTROY:self->closed_=true;return 0;
        case WM_NCDESTROY:self->window_=self->text_=self->close_=self->refresh_=nullptr;break;
        }
        return DefWindowProcW(w,m,a,b);
    }
public:
    ~TextPage(){if(IsWindow(window_))DestroyWindow(window_);if(font_)DeleteObject(font_);}
    bool isOpen() const { return window_ && IsWindow(window_); }
    bool translate(MSG& message) { return isOpen() && IsDialogMessageW(window_,&message); }
    void showModeless(HWND owner,const wchar_t* title,std::function<std::wstring()> snapshot) {
        snapshot_=std::move(snapshot);
        if(isOpen()){SetWindowTextW(text_,snapshot_().c_str());ShowWindow(window_,SW_RESTORE);SetForegroundWindow(window_);return;}
        show(owner,title,snapshot_(),false,true);
    }
    void show(HWND owner,const wchar_t* title,const std::wstring& content,bool compact=false,bool modeless=false) {
        closed_=false;
        const auto previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        struct Restore { DPI_AWARENESS_CONTEXT context; ~Restore(){if(context)SetThreadDpiAwarenessContext(context);} } restore{previous};
        HINSTANCE module=GetModuleHandleW(nullptr);
        WNDCLASSW wc{};wc.hInstance=module;wc.lpfnWndProc=proc;wc.lpszClassName=L"InkBall.TextPage";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_WINDOW);
        if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot create the information page.");
        dpi_=GetDpiForWindow(owner);if(!dpi_)dpi_=96;
        MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&mi);
        const int width=std::min(px(compact?480:650),static_cast<int>(mi.rcWork.right-mi.rcWork.left));
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
        font();layout();
        if(modeless){ShowWindow(window_,SW_SHOW);SetFocus(close_);return;}
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
