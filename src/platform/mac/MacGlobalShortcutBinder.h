#pragma once

#include "platform/GlobalShortcutBinder.h"

namespace speecher {

// Carbon hot keys are the only macOS API that reports key release as well as
// press without an Accessibility grant, which is what push-to-talk needs. macOS
// has no desktop-wide shortcut registry to store the binding in, so it lives in
// Speecher's own settings.
class MacGlobalShortcutBinder : public GlobalShortcutBinder {
    Q_OBJECT

public:
    explicit MacGlobalShortcutBinder(QObject *parent = nullptr);
    ~MacGlobalShortcutBinder() override;

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    QKeySequence shortcut() const override;
    bool setShortcut(const QKeySequence &shortcut, QString *error = nullptr) override;

private:
    bool registerHotKey(const QKeySequence &shortcut, QString *error);
    void unregisterHotKey();

    QKeySequence m_shortcut;
    // EventHotKeyRef, EventHandlerRef and EventHandlerUPP, kept opaque so this
    // header stays plain C++ for moc.
    void *m_hotKey = nullptr;
    void *m_eventHandler = nullptr;
    void *m_eventHandlerUpp = nullptr;
};

} // namespace speecher
