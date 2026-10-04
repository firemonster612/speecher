#pragma once

#include "platform/GlobalShortcutBinder.h"
#include "platform/SessionShortcutBinder.h"

#include <memory>

namespace speecher {

// One Carbon hot key, shared by both binders below; defined in the .mm so
// this header stays plain C++ for moc.
class CarbonHotKey;

// Carbon hot keys are the only macOS API that reports key release as well as
// press without an Accessibility grant, which is what push-to-talk needs. macOS
// has no desktop-wide shortcut registry to store the binding in, so it lives in
// Speecher's own settings.
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

private:
    QKeySequence m_shortcut;
    // Setup and settings can record concurrently; only the last resume binds.
    int m_suspensionCount = 0;
    bool m_resumeBinding = false;
    std::unique_ptr<CarbonHotKey> m_hotKey;
};

// A session shortcut (Cancel or Pause) as a Carbon hot key registered only for
// the length of a Dictation Session, so Carbon takes a bare key such as C or
// Escape from other apps just while it is armed.
class MacSessionShortcutBinder final : public SessionShortcutBinder {
    Q_OBJECT

public:
    explicit MacSessionShortcutBinder(GlobalShortcutAction action, QObject *parent = nullptr);
    ~MacSessionShortcutBinder() override;

    bool supported() const override;
    QString unsupportedReason() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;

protected:
    bool take(const QKeySequence &keys) override;
    void letGo() override;

private:
    std::unique_ptr<CarbonHotKey> m_hotKey;
};

} // namespace speecher
