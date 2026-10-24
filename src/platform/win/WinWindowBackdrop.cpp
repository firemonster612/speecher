#include "platform/win/WinWindowBackdrop.h"

#include <QOperatingSystemVersion>
#include <QRect>
#include <QSettings>
#include <QWindow>

#include <dwmapi.h>
#include <windows.h>

namespace speecher {

namespace {

// From the Windows 11 22H2 SDK, which older SDKs lack.
constexpr DWORD systemBackdropTypeAttribute = 38; // DWMWA_SYSTEMBACKDROP_TYPE
constexpr int backdropNone = 1;                   // DWMSBT_NONE
constexpr int backdropMica = 2;                   // DWMSBT_MAINWINDOW
constexpr DWORD darkModeAttribute = 20;           // DWMWA_USE_IMMERSIVE_DARK_MODE

} // namespace

bool WinWindowBackdrop::available() const
{
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621)) {
        return false;
    }
    // Settings > Personalization > Colors > Transparency effects.
    const QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalize.value(QStringLiteral("EnableTransparency"), 1).toInt() != 0;
}

// DWM's backdrop has no region or radius: Mica fills the window, and Windows
// 11 rounds the window's corners itself.
void WinWindowBackdrop::set(QWindow *window, const QRect &area, int, bool dark)
{
    if (!window) {
        return;
    }
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL darkMode = dark;
    DwmSetWindowAttribute(hwnd, darkModeAttribute, &darkMode, sizeof darkMode);
    // The frame reaches over the whole client area, so DWM draws its backdrop
    // under the window's transparent pixels.
    const MARGINS margins = area.isEmpty() ? MARGINS{0, 0, 0, 0} : MARGINS{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    const int type = area.isEmpty() ? backdropNone : backdropMica;
    DwmSetWindowAttribute(hwnd, systemBackdropTypeAttribute, &type, sizeof type);
}

} // namespace speecher
