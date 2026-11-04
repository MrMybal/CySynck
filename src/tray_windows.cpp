#include "desktop_support.hpp"
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

namespace cysynck::desktop {
static std::wstring wide(const std::string &text) {
    auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                      static_cast<int>(text.size()), nullptr, 0);
    std::wstring value(length, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), value.data(),
                        length);
    return value;
}
struct Tray::Impl {
    static constexpr UINT callback_message = WM_APP + 7;
    HWND window = nullptr;
    HICON icon = nullptr;
    UINT restarted = RegisterWindowMessageW(L"TaskbarCreated");
    bool active = false;
    std::string error;
    TrayLabels labels;
    std::deque<TrayAction> actions;
    void enqueue(TrayAction action) {
        if (actions.size() < 32)
            actions.push_back(action);
    }
    void make_icon() {
        if (icon)
            DestroyIcon(icon);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 32;
        info.bmiHeader.biHeight = -32;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *pixels = nullptr;
        auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        unsigned char mask[128]{};
        auto monochrome = CreateBitmap(32, 32, 1, 1, mask);
        if (bitmap && monochrome) {
            auto color = static_cast<unsigned>(labels.blue) |
                         (static_cast<unsigned>(labels.green) << 8) |
                         (static_cast<unsigned>(labels.red) << 16) | 0xff000000U;
            auto data = static_cast<unsigned *>(pixels);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) {
                    bool top = (x >= 5 && x <= 23 && y >= 8 && y <= 11) ||
                               (x >= 21 && x <= 28 && std::abs(y - 10) <= 28 - x);
                    bool bottom = (x >= 8 && x <= 26 && y >= 21 && y <= 24) ||
                                  (x >= 3 && x <= 10 && std::abs(y - 22) <= x - 3);
                    data[y * 32 + x] = top || bottom ? color : 0;
                }
            ICONINFO details{};
            details.fIcon = TRUE;
            details.hbmMask = monochrome;
            details.hbmColor = bitmap;
            icon = CreateIconIndirect(&details);
        }
        if (bitmap)
            DeleteObject(bitmap);
        if (monochrome)
            DeleteObject(monochrome);
    }
    bool publish(DWORD operation) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = 1;
        data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = callback_message;
        data.hIcon = icon ? icon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
        auto tip = wide(labels.tooltip);
        wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
        if (!Shell_NotifyIconW(operation, &data))
            return false;
        if (operation == NIM_ADD) {
            data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data);
        }
        return true;
    }
    void menu() {
        auto popup = CreatePopupMenu();
        if (!popup)
            return;
        auto add = [&](TrayAction action, const std::string &label) {
            auto text = wide(label);
            AppendMenuW(popup, MF_STRING, static_cast<UINT_PTR>(action), text.c_str());
        };
        add(TrayAction::show, labels.show);
        add(TrayAction::open_web, labels.open_web);
        add(TrayAction::backup_all, labels.backup_all);
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        add(TrayAction::quit, labels.quit);
        SetMenuDefaultItem(popup, static_cast<UINT>(TrayAction::show), FALSE);
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(window);
        auto selected = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                         point.x, point.y, window, nullptr);
        if (selected >= 1 && selected <= 4)
            enqueue(static_cast<TrayAction>(selected));
        PostMessageW(window, WM_NULL, 0, 0);
        DestroyMenu(popup);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<Impl *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            self->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self)
            try {
                if (message == WM_ENDSESSION && w) {
                    self->enqueue(TrayAction::quit);
                    return 0;
                }
                if (message == self->restarted) {
                    self->active = self->publish(NIM_ADD);
                    return 0;
                }
                if (message == callback_message) {
                    auto event = LOWORD(l);
                    if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP)
                        self->menu();
                    else if (event == NIN_SELECT || event == NIN_KEYSELECT ||
                             event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK)
                        self->enqueue(TrayAction::show);
                    return 0;
                }
                if (message == WM_COMMAND && LOWORD(w) >= 1 && LOWORD(w) <= 4) {
                    self->enqueue(static_cast<TrayAction>(LOWORD(w)));
                    return 0;
                }
            } catch (...) {
            }
        return DefWindowProcW(window, message, w, l);
    }
    explicit Impl(const TrayLabels &value) : labels(value) {
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type);
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"CySynck.Tray.Window";
        type.lpfnWndProc = procedure;
        RegisterClassExW(&type);
        window = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"CySynck notification area",
                                 WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, this);
        if (!window) {
            error = "Cannot create the notification window";
            return;
        }
        make_icon();
        active = publish(NIM_ADD);
        if (!active)
            error = "The Windows notification area is unavailable";
    }
    ~Impl() {
        if (window) {
            publish(NIM_DELETE);
            DestroyWindow(window);
        }
        if (icon)
            DestroyIcon(icon);
    }
};
Tray::Tray(const TrayLabels &labels) : impl_(std::make_unique<Impl>(labels)) {}
Tray::~Tray() = default;
bool Tray::available() const { return impl_->active; }
std::string Tray::reason() const { return impl_->error; }
void Tray::update(const TrayLabels &labels) {
    bool color = impl_->labels.red != labels.red || impl_->labels.green != labels.green ||
                 impl_->labels.blue != labels.blue;
    bool tip = impl_->labels.tooltip != labels.tooltip;
    impl_->labels = labels;
    if (color)
        impl_->make_icon();
    if ((color || tip) && impl_->active)
        impl_->active = impl_->publish(NIM_MODIFY);
}
void Tray::pump() {}
bool Tray::next(TrayAction &action) {
    if (impl_->actions.empty())
        return false;
    action = impl_->actions.front();
    impl_->actions.pop_front();
    return true;
}
} // namespace cysynck::desktop
