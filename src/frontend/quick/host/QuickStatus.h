#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

namespace speecher {

class ApplicationController;

// The UI's Status singleton: the app-wide state the shell shows, the running
// version and an update waiting for a restart from the updater, and the
// dictation shortcut's keys. Its wording is the UI's (Words).
class QuickStatus final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Status)
    QML_SINGLETON
    Q_PROPERTY(QStringList shortcutKeys READ shortcutKeys NOTIFY shortcutKeysChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool updateReady READ updateReady NOTIFY updateChanged)
    Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateChanged)

public:
    explicit QuickStatus(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickStatus() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickStatus *create(QQmlEngine *, QJSEngine *);

    QStringList shortcutKeys() const;
    QString version() const;
    // An update is installed and takes effect once Speecher restarts.
    bool updateReady() const;
    // What the update is called: a release's number, or a nightly's build.
    QString updateVersion() const;

signals:
    void shortcutKeysChanged();
    void updateChanged();

private:
    ApplicationController *m_controller;
};

} // namespace speecher
