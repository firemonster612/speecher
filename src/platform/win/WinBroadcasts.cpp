#include "platform/win/WinBroadcasts.h"

#include <QObject>

#include <windows.h>

#include <cwchar>
#include <optional>

namespace speecher::win {
namespace {

constexpr auto windowClassName = L"SpeecherBroadcasts";

// Answers a message the hidden window takes, or nothing to leave it to
// DefWindowProc.
using MessageHandler = std::function<std::optional<LRESULT>(UINT, WPARAM, LPARAM)>;

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    const auto *handle = reinterpret_cast<const MessageHandler *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (handle) {
        if (const std::optional<LRESULT> result = (*handle)(message, wParam, lParam)) {
            return *result;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void onTopLevelMessage(QObject *context, MessageHandler handle)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = windowClassName;
    RegisterClassW(&windowClass);
    auto *handler = new MessageHandler(std::move(handle));
    // Top-level and never shown: a message-only window (HWND_MESSAGE) is left
    // out of broadcasts.
    HWND window = CreateWindowExW(0, windowClassName, L"Speecher", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                  nullptr, instance, handler);
    QObject::connect(context, &QObject::destroyed, [window, handler] {
        if (window) {
            DestroyWindow(window);
        }
        delete handler;
    });
}

} // namespace

void quitWhenWindowsEndsSession(QObject *context, std::function<void()> quit)
{
    onTopLevelMessage(context, [quit = std::move(quit)](UINT message, WPARAM wParam, LPARAM) -> std::optional<LRESULT> {
        // Restart Manager sends WM_QUERYENDSESSION, which DefWindowProc
        // agrees to, then WM_ENDSESSION. Ignoring it leaves Setup waiting 30
        // seconds and then failing. Asked to close, DefWindowProc would
        // destroy the window and leave Speecher running with nothing left to
        // answer the next request.
        if ((message == WM_ENDSESSION && wParam) || message == WM_CLOSE) {
            quit();
            return 0;
        }
        return std::nullopt;
    });
}

void onThemeChanged(QObject *context, std::function<void()> changed)
{
    onTopLevelMessage(context, [changed = std::move(changed)](UINT message, WPARAM, LPARAM lParam) -> std::optional<LRESULT> {
        if (message == WM_SETTINGCHANGE && lParam
            && std::wcscmp(reinterpret_cast<const wchar_t *>(lParam), L"ImmersiveColorSet") == 0) {
            changed();
        }
        return std::nullopt;
    });
}

} // namespace speecher::win
