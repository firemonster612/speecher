#pragma once

#include "platform/GlobalShortcutBinder.h"

namespace speecher {

// Carbon hot keys are the only macOS API that reports key release as well as
// press without an Accessibility grant, which is what push-to-talk needs. macOS
// has no desktop-wide shortcut registry to store the binding in, so it lives in
// Speecher's own settings.
//
// The Cancel and Pause Shortcuts act only during a Dictation Session, so their
// hot key is registered only while armed and not suspended, and may be a bare
// key such as C or Escape: Carbon takes it from other apps just for the
// session. The dictation shortcut stays registered and needs a modifier.
class MacGlobalShortcutBinder : public GlobalShortcutBinder {
    Q_OBJECT

public:
    explicit MacGlobalShortcutBinder(
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr);
    ~MacGlobalShortcutBinder() override;

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    ShortcutBinding shortcut() const override;
    bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) override;
    void suspend() override;
    QString resume() override;
    bool removeRegistration(QString *error = nullptr) override;
    void setArmed(bool armed) override;
    // What this binder's Carbon hot key is registered under.
    quint32 hotKeyIdentifier() const;

private:
    bool registerHotKey(const QKeySequence &shortcut, QString *error);
    void unregisterHotKey();
    void refreshKeyboardLayout();
    // A session shortcut: registers the binding while armed and not
    // suspended, otherwise lets it go. Returns why Carbon refused it.
    QString holdWhileArmed();

    QKeySequence m_shortcut;
    quint32 m_hotKeyIdentifier = 0;
    // Setup and settings can record concurrently; only the last resume binds.
    int m_suspensionCount = 0;
    bool m_resumeBinding = false;
    bool m_armed = false;
    // EventHotKeyRef, EventHandlerRef and EventHandlerUPP, kept opaque so this
    // header stays plain C++ for moc.
    void *m_hotKey = nullptr;
    quint32 m_registeredKeyCode = 0;
    quint32 m_registeredModifiers = 0;
    void *m_eventHandler = nullptr;
    void *m_eventHandlerUpp = nullptr;
};

} // namespace speecher
