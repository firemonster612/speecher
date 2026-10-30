#include "platform/win/WinRawKeyboard.h"

#include <QList>
#include <QPointer>

namespace speecher::win {
namespace {

constexpr auto windowClassName = L"SpeecherRawKeyboard";

struct Listener {
    QPointer<QObject> context;
    std::function<void(const RAWINPUT &, qint64)> deliver;
};

QList<Listener> &listeners()
{
    static QList<Listener> list;
    return list;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INPUT) {
        // Read before any listener runs: one that spins an event loop makes
        // a later message the last one retrieved. GetMessageTime() is the
        // DWORD tick count cast to LONG.
        const qint64 messageTimeMs = DWORD(GetMessageTime());
        RAWINPUT input{};
        UINT size = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size,
                            sizeof(RAWINPUTHEADER))
            != UINT(-1)) {
            // A copy: what one listener does can destroy another's context.
            const QList<Listener> current = listeners();
            for (const Listener &listener : current) {
                if (listener.context) {
                    listener.deliver(input, messageTimeMs);
                }
            }
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool ensureRegistered(QString *error)
{
    static HWND window = nullptr;
    if (window) {
        return true;
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = windowClassName;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (error) {
            *error = QStringLiteral("Windows could not create the key listener");
        }
        return false;
    }
    HWND created = CreateWindowExW(0, windowClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                   nullptr, instance, nullptr);
    if (!created) {
        if (error) {
            *error = QStringLiteral("Windows could not create the key listener");
        }
        return false;
    }
    RAWINPUTDEVICE keyboard{0x01, 0x06, RIDEV_INPUTSINK, created};
    if (!RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard))) {
        DestroyWindow(created);
        if (error) {
            *error = QStringLiteral("Windows could not watch the keyboard");
        }
        return false;
    }
    window = created;
    return true;
}

} // namespace

bool listenToRawKeyboard(QObject *context,
                         std::function<void(const RAWINPUT &, qint64 messageTimeMs)> listener,
                         QString *error)
{
    if (!ensureRegistered(error)) {
        return false;
    }
    listeners().removeIf([context](const Listener &existing) {
        return !existing.context || existing.context == context;
    });
    listeners().append({context, std::move(listener)});
    return true;
}

} // namespace speecher::win
