#pragma once

#include "platform/SingleKeyShortcutBinder.h"
#include "setup/KeywatchProtocol.h"

class QLocalSocket;
class QTimer;

namespace speecher {

// Watches one key on Wayland through speecher-keywatchd, the sandboxed helper
// that reads the keyboards and reports only whether that one key is down. The
// helper watches only keys that cannot spell text, which is reported per
// binding so the setup page can say so.
class KeywatchShortcutBinder : public SingleKeyShortcutBinder {
    Q_OBJECT

public:
    // socketPath is where the helper listens; a test points it at a stand-in.
    explicit KeywatchShortcutBinder(
        SettingsStore *settings,
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr,
        QString socketPath = QString::fromLatin1(keywatch::socketPath));

    bool supported() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;
    void bind() override;

protected:
    QString watch(const PhysicalKey &key) override;
    void unwatch() override;

private:
    QString exchangeWatch(const PhysicalKey &key);
    void readFromDaemon();
    void reconnectLater(int delayMs);
    void startRecoveryPoll();

    const QString m_socketPath;
    QLocalSocket *m_socket;
    QTimer *m_recoveryPoll = nullptr;
    quint8 m_keyId = 0;
    bool m_replied = false;
    bool m_reconnectPending = false;
    // The helper watches one key per person and our other Global Shortcut
    // holds it. Retrying cannot succeed until that one lets its key go.
    bool m_helperBusy = false;
};

} // namespace speecher
