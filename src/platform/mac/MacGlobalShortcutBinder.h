#pragma once

#include "platform/HotKeyShortcutBinder.h"
#include "platform/SessionShortcutBinder.h"

#include <memory>

namespace speecher {

// One Carbon hot key, shared by both binders below; defined in the .mm so
// this header stays plain C++ for moc.
class CarbonHotKey;

// Carbon hot keys are the only macOS API that reports key release as well as
// press without an Accessibility grant, which is what push-to-talk needs.
class MacGlobalShortcutBinder final : public HotKeyShortcutBinder {
    Q_OBJECT

public:
    explicit MacGlobalShortcutBinder(
        SettingsStore *settings,
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr);
    ~MacGlobalShortcutBinder() override;

protected:
    bool registerKeys(const QKeySequence &keys, QString *error) override;
    void unregisterKeys() override;
    bool registered() const override;

private:
    std::unique_ptr<CarbonHotKey> m_hotKey;
};

// A session shortcut (Cancel or Pause) as a Carbon hot key registered only for
// the length of a Dictation Session, so Carbon takes a bare key such as C or
// Escape from other apps just while it is armed.
class MacSessionShortcutBinder final : public SessionShortcutBinder {
    Q_OBJECT

public:
    MacSessionShortcutBinder(SettingsStore *settings, GlobalShortcutAction action, QObject *parent = nullptr);
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
