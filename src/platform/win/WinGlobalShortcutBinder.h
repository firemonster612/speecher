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

    explicit WinGlobalShortcutBinder(
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr);
    ~WinGlobalShortcutBinder() override;

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    ShortcutBinding shortcut() const override;
    bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) override;
    void suspend() override;
    QString resume() override;
    bool removeRegistration(QString *error = nullptr) override;

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

    static std::optional<NativeHotKey> nativeHotKey(const QKeySequence &shortcut,
                                                     QString *error = nullptr);
    static QKeySequence keySequenceForHotKey(quint32 modifiers, quint32 virtualKey);
    // Whether a setShortcut error means another application already owns the
    // combination. Setup tells the user to record a different one only then;
    // a key Windows cannot register at all will not yield to a retry.
    static bool describesConflict(const QString &error);

private:
    friend class ::WinPlatformTests;
    bool registerShortcut(const QKeySequence &shortcut, QString *error);
    void unregisterShortcut();
    void handleRawInput(const RAWINPUT &input);

    QKeySequence m_shortcut;
    // This binder's two hot-key ids are this and the next.
    int m_firstHotKeyId = 0;
    int m_hotKeyId = 0;
    quint32 m_pressedKey = 0;
    bool m_pressed = false;
    // Setup and settings can record concurrently; only the last resume binds.
    int m_suspensionCount = 0;
    bool m_resumeBinding = false;
};

} // namespace speecher
