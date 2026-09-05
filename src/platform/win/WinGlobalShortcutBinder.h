#pragma once

#include "platform/GlobalShortcutBinder.h"

#include <QAbstractNativeEventFilter>

#include <optional>

#include <windows.h>

class WinPlatformTests;

namespace speecher {

class WinGlobalShortcutBinder : public GlobalShortcutBinder,
                                public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    struct NativeHotKey {
        quint32 modifiers = 0;
        quint32 virtualKey = 0;
    };

    explicit WinGlobalShortcutBinder(QObject *parent = nullptr);
    ~WinGlobalShortcutBinder() override;

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    QKeySequence shortcut() const override;
    bool setShortcut(const QKeySequence &shortcut, QString *error = nullptr) override;

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

    static std::optional<NativeHotKey> nativeHotKey(const QKeySequence &shortcut,
                                                     QString *error = nullptr);
    static QKeySequence keySequenceForHotKey(quint32 modifiers, quint32 virtualKey);

private:
    friend class ::WinPlatformTests;
    static LRESULT CALLBACK messageWindowProc(HWND window,
                                               UINT message,
                                               WPARAM wParam,
                                               LPARAM lParam);
    bool registerShortcut(const QKeySequence &shortcut, QString *error);
    bool ensureMessageWindow(QString *error);
    void unregisterShortcut();
    void handleRawInput(HRAWINPUT handle);
    void handleRawInput(const RAWINPUT &input);

    QKeySequence m_shortcut;
    HWND m_messageWindow = nullptr;
    int m_hotKeyId = 0;
    quint32 m_virtualKey = 0;
    bool m_pressed = false;
};

} // namespace speecher
