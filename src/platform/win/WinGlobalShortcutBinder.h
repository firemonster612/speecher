#pragma once

#include "platform/HotKeyShortcutBinder.h"
#include "platform/SessionShortcutBinder.h"

#include <QAbstractNativeEventFilter>

#include <optional>

#include <windows.h>

class WinPlatformTests;

namespace speecher {

// The dictation shortcut: a combination RegisterHotKey takes from every app
// for good, with its release read from raw input for push-to-talk.
class WinGlobalShortcutBinder : public HotKeyShortcutBinder,
                                public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    struct NativeHotKey {
        quint32 modifiers = 0;
        quint32 virtualKey = 0;
    };

    explicit WinGlobalShortcutBinder(
        SettingsStore *settings,
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr);
    ~WinGlobalShortcutBinder() override;

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

    // bareKeyAllowed lets a combination without a modifier through, for a
    // session shortcut, which holds its hot key only while dictating.
    static std::optional<NativeHotKey> nativeHotKey(const QKeySequence &shortcut,
                                                     QString *error = nullptr,
                                                     bool bareKeyAllowed = false);
    static QKeySequence keySequenceForHotKey(quint32 modifiers, quint32 virtualKey);
    // Whether a setShortcut error means another application already owns the
    // combination. Setup tells the user to record a different one only then;
    // a key Windows cannot register at all will not yield to a retry.
    static bool describesConflict(const QString &error);

protected:
    bool registerKeys(const QKeySequence &keys, QString *error) override;
    void unregisterKeys() override;
    bool registered() const override;

private:
    friend class ::WinPlatformTests;
    // messageTimeMs is the WM_INPUT's time, as listenToRawKeyboard delivers it.
    void handleRawInput(const RAWINPUT &input, qint64 messageTimeMs);

    // This binder's two hot-key ids are this and the next.
    int m_firstHotKeyId = 0;
    int m_hotKeyId = 0;
    quint32 m_pressedKey = 0;
    // The WM_HOTKEY's time, in the tick count's milliseconds.
    DWORD m_pressedAtMs = 0;
    bool m_pressed = false;
};

// A Cancel or Pause Shortcut: RegisterHotKey holds its keys only while
// dictating, so a bare key such as C or Escape types as usual the rest of the
// time. Both act on press alone, so no release is read.
class WinSessionShortcutBinder final : public SessionShortcutBinder,
                                       public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    WinSessionShortcutBinder(SettingsStore *settings, GlobalShortcutAction action, QObject *parent = nullptr);
    ~WinSessionShortcutBinder() override;

    bool supported() const override;
    QString unsupportedReason() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

protected:
    bool take(const QKeySequence &keys) override;
    void letGo() override;

private:
    friend class ::WinPlatformTests;
    const int m_hotKeyId;
    bool m_registered = false;
};

} // namespace speecher
