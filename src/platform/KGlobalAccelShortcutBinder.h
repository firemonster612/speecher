#pragma once

#include "platform/GlobalShortcutBinder.h"

class QAction;

namespace speecher {

// Binds through KDE's global shortcut daemon. A shortcut that acts only
// during a Dictation Session keeps its binding in Speecher's settings and
// registers it with the daemon only while armed, so KWin takes a bare key
// such as C from the focused app just for the session.
class KGlobalAccelShortcutBinder final : public GlobalShortcutBinder {
    Q_OBJECT

public:
    explicit KGlobalAccelShortcutBinder(
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr);

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    ShortcutBinding shortcut() const override;
    bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) override;
    bool removeRegistration(QString *error = nullptr) override;
    void setArmed(bool armed) override;
    void suspend() override;
    QString resume() override;

private:
    QAction *makeShortcutAction();
    // The dictation-only shortcut: registers the stored combination while
    // armed and not suspended, otherwise lets it go. Returns the daemon's
    // refusal when it would not take the keys.
    QString holdWhileArmed();
    bool registerWithDaemon(const QKeySequence &keys, QString *error);
    void releaseFromDaemon();

    QAction *m_action = nullptr;
    ShortcutBinding m_binding;
    bool m_armed = false;
    bool m_suspended = false;
};

} // namespace speecher
