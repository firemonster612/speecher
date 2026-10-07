#pragma once

#include "platform/GlobalShortcutBinder.h"
#include "platform/SessionShortcutBinder.h"

class QAction;

namespace speecher {

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

private:
    QAction *makeShortcutAction();

    QAction *m_action = nullptr;
};

// A session shortcut (Cancel or Pause) through KDE's global shortcut daemon:
// registered only for the length of a Dictation Session, so KWin takes a bare
// key such as C from the focused app just while it is armed.
class KGlobalAccelSessionShortcutBinder final : public SessionShortcutBinder {
    Q_OBJECT

public:
    explicit KGlobalAccelSessionShortcutBinder(GlobalShortcutAction action,
                                               QObject *parent = nullptr);

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    // Lets go of what an earlier build or a crashed run left registered
    // under this action, which would otherwise keep its keys from every app.
    void dropLeftoverRegistration();

protected:
    bool take(const QKeySequence &keys) override;
    void letGo() override;
    ShortcutBinding storedShortcut() const override;

private:
    QAction *m_action = nullptr;
};

} // namespace speecher
