#pragma once

#include "platform/SingleKeyShortcutBinder.h"

class QTimer;

namespace speecher {

// Watches one physical key through NSEvent global and local monitors, which is
// the Accessibility-grant path: not a CGEventTap, not Input Monitoring. The
// monitors cannot consume the key; the recorder's inline warning covers that.
//
// The failure mode that matters is silence: without the grant a monitor
// registers successfully and never fires. unsupportedBindingReason() therefore
// refuses a single key while AXIsProcessTrusted() says no, and a poll re-binds
// the stored key when the grant arrives — monitors added after the grant work
// without a relaunch.
class MacSingleKeyShortcutBinder final : public SingleKeyShortcutBinder {
    Q_OBJECT

public:
    explicit MacSingleKeyShortcutBinder(
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr);
    ~MacSingleKeyShortcutBinder() override;

    bool supported() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;

protected:
    QString watch(const PhysicalKey &key) override;
    void unwatch() override;

private:
    // NSEvent monitor handles (id), opaque so this header stays plain C++
    // for moc.
    void *m_globalMonitor = nullptr;
    void *m_localMonitor = nullptr;
    QTimer *m_grantPoll = nullptr;
};

} // namespace speecher
