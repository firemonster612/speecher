#pragma once

#include "platform/GlobalShortcutBinder.h"

namespace speecher {

// One binder over two backends: a key combination goes to the platform's
// shortcut service (KGlobalAccel or the portal on Linux, Carbon hot keys on
// macOS), a single key to the backend that watches the key itself (XInput2 on
// X11, the key-watch helper on Wayland, NSEvent monitors on macOS). There is
// one binding, so setting either kind lets go of the other.
class RoutingShortcutBinder final : public GlobalShortcutBinder {
    Q_OBJECT

public:
    RoutingShortcutBinder(GlobalShortcutBinder *combination,
                          GlobalShortcutBinder *singleKey,
                          QObject *parent = nullptr);

    // Supported when either backend can take a binding, so a desktop without
    // a shortcut service (X11 outside Plasma) still offers a single key. The
    // desktop's chooser stays in use unless only the single key works.
    bool supported() const override;
    bool supportKnown() const override;
    bool usesDesktopShortcutChooser() const override;
    QString unsupportedReason() const override;
    void bind() override;
    ShortcutBinding shortcut() const override;
    QString shortcutDisplay() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;
    bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) override;
    void setArmed(bool armed) override;
    void suspend() override;
    QString resume() override;
    bool removeRegistration(QString *error = nullptr) override;
    void registerShortcut() override;

private:
    GlobalShortcutBinder *m_combination;
    GlobalShortcutBinder *m_singleKey;
};

} // namespace speecher
