#include "frontend/win/TrayIcon.h"

#include "app/ApplicationController.h"
#include "frontend/win/TrayFlyout.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <strsafe.h>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QPainter>

#include <array>
#include <cstring>

namespace speecher {
namespace {

constexpr UINT iconId = 1;
constexpr UINT trayMessage = WM_APP + 1;
constexpr UINT startStopCommand = 1;
constexpr UINT settingsCommand = 2;
constexpr UINT quitCommand = 3;
constexpr auto windowClassName = L"SpeecherTrayIcon";

using WindowCallback = std::function<LRESULT(UINT, WPARAM, LPARAM)>;

LRESULT CALLBACK trayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto *callback = reinterpret_cast<WindowCallback *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (callback) {
        const LRESULT result = (*callback)(message, wParam, lParam);
        if (result) {
            return result;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool systemUsesLightTheme()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD,
                 nullptr, &value, &size);
    return value != 0;
}

HICON createListeningIcon(bool lightTaskbar)
{
    constexpr int size = 32;
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QFont font(QStringLiteral("Segoe Fluent Icons"));
    font.setPixelSize(24);
    painter.setFont(font);
    painter.setPen(lightTaskbar ? Qt::black : Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter, QString::fromUtf16(u"\uE720"));
    painter.end();

    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    void *bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO *>(&header),
                                     DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) {
        return nullptr;
    }
    std::memcpy(bits, image.constBits(), image.sizeInBytes());
    std::array<BYTE, size * size / 8> maskBits{};
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO info{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

} // namespace

struct TrayIcon::Native {
    Native(ApplicationController *owner,
           std::function<void()> openSettings,
           TrayIcon *q)
        : controller(owner)
        , showSettings(std::move(openSettings))
        , flyout(owner, q)
    {
        callback = [this](UINT message, WPARAM wParam, LPARAM lParam) {
            return handleMessage(message, wParam, lParam);
        };
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = trayWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = windowClassName;
        RegisterClassW(&windowClass);
        window = CreateWindowExW(0, windowClassName, L"Speecher tray host",
                                 WS_OVERLAPPED, 0, 0, 0, 0,
                                 nullptr, nullptr, windowClass.hInstance, &callback);
        addIcon();

        QObject::connect(controller, &ApplicationController::stateChanged, q,
                         [this](const QString &state) {
                             const QString lowered = state.toLower();
                             listening = lowered == QStringLiteral("starting")
                                 || lowered == QStringLiteral("listening");
                             updateIcon();
                         });
    }

    ~Native()
    {
        NOTIFYICONDATAW data = iconData();
        Shell_NotifyIconW(NIM_DELETE, &data);
        releaseIcon();
        if (window) {
            DestroyWindow(window);
        }
    }

    NOTIFYICONDATAW iconData() const
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = iconId;
        return data;
    }

    void addIcon()
    {
        NOTIFYICONDATAW data = iconData();
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = trayMessage;
        data.hIcon = loadIcon();
        StringCchCopyW(data.szTip, ARRAYSIZE(data.szTip),
                       listening ? L"Speecher is listening" : L"Speecher");
        Shell_NotifyIconW(NIM_ADD, &data);
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }

    void updateIcon()
    {
        if (!window) {
            return;
        }
        NOTIFYICONDATAW data = iconData();
        data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.hIcon = loadIcon();
        StringCchCopyW(data.szTip, ARRAYSIZE(data.szTip),
                       listening ? L"Speecher is listening" : L"Speecher");
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    HICON loadIcon()
    {
        releaseIcon();
        const bool lightTaskbar = systemUsesLightTheme();
        if (listening) {
            currentIcon = createListeningIcon(lightTaskbar);
            ownsCurrentIcon = currentIcon != nullptr;
            if (currentIcon) {
                return currentIcon;
            }
        }
        const QString fileName = lightTaskbar
            ? QStringLiteral("tray-dark.ico")
            : QStringLiteral("tray-light.ico");
        const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
        currentIcon = static_cast<HICON>(LoadImageW(
            nullptr, reinterpret_cast<LPCWSTR>(path.utf16()), IMAGE_ICON,
            0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
        ownsCurrentIcon = currentIcon != nullptr;
        if (!currentIcon) {
            currentIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }
        return currentIcon;
    }

    void releaseIcon()
    {
        if (currentIcon && ownsCurrentIcon) {
            DestroyIcon(currentIcon);
        }
        currentIcon = nullptr;
        ownsCurrentIcon = false;
    }

    RECT iconRect() const
    {
        NOTIFYICONIDENTIFIER identifier{};
        identifier.cbSize = sizeof(identifier);
        identifier.hWnd = window;
        identifier.uID = iconId;
        RECT rect{};
        if (FAILED(Shell_NotifyIconGetRect(&identifier, &rect))) {
            POINT point{};
            GetCursorPos(&point);
            rect = {point.x, point.y, point.x + 1, point.y + 1};
        }
        return rect;
    }

    void showContextMenu(POINT point)
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, startStopCommand,
                    listening ? L"Stop Dictation" : L"Start Dictation");
        AppendMenuW(menu, MF_STRING, settingsCommand, L"Settings...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, quitCommand, L"Quit");
        SetForegroundWindow(window);
        const UINT command = TrackPopupMenu(
            menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        PostMessageW(window, WM_NULL, 0, 0);
        if (command == startStopCommand) {
            controller->toggle();
        } else if (command == settingsCommand) {
            showSettings();
        } else if (command == quitCommand) {
            controller->quitApplication();
        }
    }

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == taskbarCreated) {
            addIcon();
            return 1;
        }
        if (message == WM_SETTINGCHANGE && lParam
            && wcscmp(reinterpret_cast<const wchar_t *>(lParam), L"ImmersiveColorSet") == 0) {
            updateIcon();
            return 1;
        }
        if (message != trayMessage) {
            return 0;
        }
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            flyout.show(iconRect());
            return 1;
        case WM_LBUTTONDBLCLK:
            flyout.hide();
            showSettings();
            return 1;
        case WM_CONTEXTMENU:
            showContextMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
            return 1;
        default:
            return 0;
        }
    }

    ApplicationController *controller;
    std::function<void()> showSettings;
    TrayFlyout flyout;
    WindowCallback callback;
    HWND window = nullptr;
    HICON currentIcon = nullptr;
    UINT taskbarCreated = 0;
    bool ownsCurrentIcon = false;
    bool listening = false;
};

TrayIcon::TrayIcon(ApplicationController *controller,
                   std::function<void()> showSettings,
                   QObject *parent)
    : QObject(parent)
    , m_native(std::make_unique<Native>(controller, std::move(showSettings), this))
{
}

TrayIcon::~TrayIcon() = default;

} // namespace speecher
