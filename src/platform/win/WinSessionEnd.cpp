#include "platform/win/WinSessionEnd.h"

#include <QObject>

#include <windows.h>

namespace speecher::win {
namespace {

constexpr auto windowClassName = L"SpeecherSessionEnd";

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    const auto *quit = reinterpret_cast<const std::function<void()> *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    // Restart Manager sends WM_QUERYENDSESSION, which DefWindowProc agrees
    // to, then WM_ENDSESSION. Ignoring it leaves Setup waiting 30 seconds and
    // then failing.
    if (quit && message == WM_ENDSESSION && wParam) {
        (*quit)();
        return 0;
    }
    // DefWindowProc would destroy the window and leave Speecher running with
    // nothing left to answer the next request.
    if (quit && message == WM_CLOSE) {
        (*quit)();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

void quitWhenWindowsEndsSession(QObject *context, std::function<void()> quit)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = windowClassName;
    RegisterClassW(&windowClass);
    auto *callback = new std::function<void()>(std::move(quit));
    // Top-level and never shown: a message-only window (HWND_MESSAGE) is left
    // out of the session-end broadcast.
    HWND window = CreateWindowExW(0, windowClassName, L"Speecher", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                  nullptr, instance, callback);
    QObject::connect(context, &QObject::destroyed, [window, callback] {
        if (window) {
            DestroyWindow(window);
        }
        delete callback;
    });
}

} // namespace speecher::win
