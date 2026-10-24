#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

namespace speecher {

class ApplicationController;

// The UI's Status singleton: the app-wide state the shell shows, the running
// version and the dictation shortcut's keys. Its wording is the UI's (Words).
class QuickStatus final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Status)
    QML_SINGLETON
    Q_PROPERTY(QStringList shortcutKeys READ shortcutKeys NOTIFY shortcutKeysChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit QuickStatus(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickStatus() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickStatus *create(QQmlEngine *, QJSEngine *);

    QStringList shortcutKeys() const;
    QString version() const;

signals:
    void shortcutKeysChanged();

private:
    ApplicationController *m_controller;
};

} // namespace speecher
