#pragma once

#include "platform/GlobalShortcutBinder.h"

namespace speecher {

class SettingsStore;

// The dictation shortcut as a hot key the system takes from every app for
// good, which is how macOS (Carbon) and Windows (RegisterHotKey) bind a
// combination. Neither keeps a desktop-wide shortcut registry, so the binding
// lives in settings. A hot key is consumed system-wide and never arrives as an
// app key event, so recording one (or any replacement) suspends the
// registration. A subclass supplies the native registration.
class HotKeyShortcutBinder : public GlobalShortcutBinder {
    Q_OBJECT

public:
    HotKeyShortcutBinder(SettingsStore *settings, GlobalShortcutAction action, QObject *parent = nullptr);

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    ShortcutBinding shortcut() const override;
    bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) override;
    void suspend() override;
    QString resume() override;
    bool removeRegistration(QString *error = nullptr) override;

protected:
    // Takes keys from every app. A registration made before is dropped only
    // once the new one holds, so a refused replacement leaves the old keys
    // working. False, with why in error, when the system cannot register the
    // keys or another app owns them.
    virtual bool registerKeys(const QKeySequence &keys, QString *error) = 0;
    // Lets go of the registration; a no-op when there is none.
    virtual void unregisterKeys() = 0;
    virtual bool registered() const = 0;

private:
    SettingsStore *m_settings;
    QKeySequence m_shortcut;
    // Setup and settings can record concurrently; only the last resume binds.
    int m_suspensionCount = 0;
    bool m_resumeBinding = false;
};

} // namespace speecher
