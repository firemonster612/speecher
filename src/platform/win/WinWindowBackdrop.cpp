#include "platform/win/WinWindowBackdrop.h"

#include <QOperatingSystemVersion>
#include <QRect>
#include <QSettings>
#include <QWinEventNotifier>
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
constexpr DWORD cornerAttribute = 33;             // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr int cornersDefault = 0;                 // DWMWCP_DEFAULT
constexpr int cornersSquare = 1;                  // DWMWCP_DONOTROUND
constexpr int cornersRound = 2;                   // DWMWCP_ROUND

// Settings > Personalization > Colors > Transparency effects.
const wchar_t personalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

} // namespace

WinWindowBackdrop::WinWindowBackdrop(QObject *parent)
    : WindowBackdrop(parent)
    , m_available(available())
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, personalizeKey, 0, KEY_NOTIFY, &key) != ERROR_SUCCESS) {
        return;
    }
    m_personalize = key;
    m_changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_notifier = new QWinEventNotifier(m_changed, this);
    connect(m_notifier, &QWinEventNotifier::activated, this, [this] {
        watchPersonalize();
        if (available() != m_available) {
            m_available = !m_available;
            emit availableChanged();
        }
    });
    watchPersonalize();
}

WinWindowBackdrop::~WinWindowBackdrop()
{
    delete m_notifier;
    if (m_changed) {
        CloseHandle(m_changed);
    }
    if (m_personalize) {
        RegCloseKey(static_cast<HKEY>(m_personalize));
    }
}

void WinWindowBackdrop::watchPersonalize()
{
    RegNotifyChangeKeyValue(static_cast<HKEY>(m_personalize), FALSE, REG_NOTIFY_CHANGE_LAST_SET, m_changed, TRUE);
}

bool WinWindowBackdrop::available() const
{
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621)) {
        return false;
    }
    const QSettings personalize(QStringLiteral("HKEY_CURRENT_USER\\") + QString::fromWCharArray(personalizeKey),
                                QSettings::NativeFormat);
    return personalize.value(QStringLiteral("EnableTransparency"), 1).toInt() != 0;
}

// DWM's backdrop has no region: Mica fills the window, and DWM gives it a
// shadow and rounds its corners, or keeps them square for a radius of 0.
void WinWindowBackdrop::set(QWindow *window, const QRect &area, int radius, bool dark)
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
    // A frameless window is square unless asked.
    const int corners = area.isEmpty() ? cornersDefault : radius > 0 ? cornersRound : cornersSquare;
    DwmSetWindowAttribute(hwnd, cornerAttribute, &corners, sizeof corners);
}

} // namespace speecher
