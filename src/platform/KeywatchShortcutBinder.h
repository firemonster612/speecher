#pragma once

#include "platform/SingleKeyShortcutBinder.h"

class QLocalSocket;
class QTimer;

namespace speecher {

// Watches one key on Wayland through speecher-keywatchd, the sandboxed helper
// that reads the keyboards and reports only whether that one key is down. The
// helper watches only keys that cannot spell text, which is reported per
// binding so the setup page can say so.
class KeywatchShortcutBinder final : public SingleKeyShortcutBinder {
    Q_OBJECT

public:
    explicit KeywatchShortcutBinder(
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr);

    bool supported() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;
    void bind() override;

protected:
    QString watch(const PhysicalKey &key) override;
    void unwatch() override;

private:
    void readFromDaemon();
    void reconnectLater(int delayMs);
    void startRecoveryPoll();

    QLocalSocket *m_socket;
    QTimer *m_recoveryPoll = nullptr;
    quint8 m_keyId = 0;
    bool m_replied = false;
    bool m_reconnectPending = false;
};

} // namespace speecher
