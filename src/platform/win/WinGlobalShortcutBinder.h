#pragma once

#include "platform/GlobalShortcutBinder.h"

#include <QAbstractNativeEventFilter>

#include <optional>

#include <windows.h>

class WinPlatformTests;

namespace speecher {

// Binds a combination with RegisterHotKey, which takes it from every app. A
// shortcut that acts only during a Dictation Session holds its hot key only
// while armed, so it may be a bare key such as C or Escape that types as
// usual the rest of the time.
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
    void setArmed(bool armed) override;
    void suspend() override;
    QString resume() override;
    bool removeRegistration(QString *error = nullptr) override;

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

    // bareKeyAllowed lets a combination without a modifier through, for a
    // shortcut that holds its hot key only during a Dictation Session.
    static std::optional<NativeHotKey> nativeHotKey(const QKeySequence &shortcut,
                                                     QString *error = nullptr,
                                                     bool bareKeyAllowed = false);
    static QKeySequence keySequenceForHotKey(quint32 modifiers, quint32 virtualKey);
    // Whether a setShortcut error means another application already owns the
    // combination. Setup tells the user to record a different one only then;
    // a key Windows cannot register at all will not yield to a retry.
    static bool describesConflict(const QString &error);

private:
    friend class ::WinPlatformTests;
    bool registerShortcut(const QKeySequence &shortcut, QString *error);
    void unregisterShortcut();
    // The dictation-only shortcut: registers the binding while armed and not
    // suspended, otherwise lets it go. Returns Windows' refusal, if any.
    QString holdWhileArmed();
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
    bool m_armed = false;
};

} // namespace speecher
