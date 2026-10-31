#pragma once

#include "platform/GlobalShortcutBinder.h"
#include "platform/KeyHold.h"
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
    // Plasma consumes a registered combination everywhere, so recording
    // lets go of the action; the daemon keeps its keys for resume().
    void suspend() override;
    QString resume() override;
    // Delivery leaves the registration alone, as it did before recording
    // suspended it: letting go and taking it back costs blocking D-Bus round
    // trips on the UI thread at every delivery.
    void suspendForDelivery() override {}
    void resumeAfterDelivery() override {}
    bool removeRegistration(QString *error = nullptr) override;

private:
    QAction *makeShortcutAction();
    void releaseAction();

    QAction *m_action = nullptr;
    // Setup and settings can record concurrently; only the last resume binds.
    int m_suspensionCount = 0;
    bool m_resumeBinding = false;
};

// Takes keys through KDE's global shortcut daemon. While they are held,
// Plasma lists them in its shortcut settings under the id and description.
class KGlobalAccelKeyHold final : public KeyHold {
    Q_OBJECT

public:
    KGlobalAccelKeyHold(QString id, QString description, QObject *parent = nullptr);

    // Whether this desktop runs the daemon.
    static bool available();
    bool take(const QKeySequence &keys) override;
    void letGo() override;
    // Lets go of what an earlier build or a crashed run left registered
    // under this id, which would otherwise keep its keys from every app.
    void dropLeftoverRegistration();

private:
    QString m_id;
    QString m_description;
    QAction *m_action = nullptr;
};

// A session shortcut (Cancel or Pause) through KDE's global shortcut daemon:
// registered only for the length of a Dictation Session, so KWin takes a bare
// key such as C from the focused app just while it is armed.
class KGlobalAccelSessionShortcutBinder final : public SessionShortcutBinder {
    Q_OBJECT

public:
    KGlobalAccelSessionShortcutBinder(SettingsStore *settings,
                                      GlobalShortcutAction action,
                                      QObject *parent = nullptr);

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;

protected:
    bool take(const QKeySequence &keys) override;
    void letGo() override;
    ShortcutBinding storedShortcut() const override;

private:
    KGlobalAccelKeyHold *m_keys;
};

} // namespace speecher
