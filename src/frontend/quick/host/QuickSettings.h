#pragma once

#include "core/ShortcutBinding.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

namespace speecher {

class ApplicationController;

// A shortcut's display text as the keycaps the UI draws: "Ctrl+Space" is
// {"Ctrl", "Space"}. A "+" splits only between two keys, so "Ctrl++" and the
// single key "Numpad +" keep their plus.
QStringList shortcutKeys(const QString &display);

// The UI's Settings singleton: the saved settings by the schema's row ids,
// read from and written through the daemon's SettingsStore, so the UI, the
// daemon and every CLI run see the same values. A row the table in the .cpp
// does not save yet reads as absent, and the UI greys it out (saves).
class QuickSettings final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QVariantMap errors READ errors NOTIFY errorsChanged)

public:
    explicit QuickSettings(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickSettings() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickSettings *create(QQmlEngine *, QJSEngine *);

    QVariantMap values() const;
    // Why a row's last save was refused, by row id, such as a shortcut another
    // app owns; a row with nothing refused is absent.
    QVariantMap errors() const;
    // Saves one row's value. A row this build does not save is refused with
    // a warning in the log. A shortcut row takes the keys ShortcutField
    // records (["Ctrl", "Space"]) and hands them to the controller, which
    // binds and keeps them; an empty list clears the shortcut. Why the
    // controller refused one lands in errors.
    Q_INVOKABLE void set(const QString &id, const QVariant &value);
    // Runs an action row, such as runSetup. On a shortcut row whose keys the
    // desktop picks (desktopPicksKeys), it opens the desktop's chooser.
    Q_INVOKABLE void act(const QString &id);
    // Whether set saves this row, or act runs it. A constant, so QML may call
    // it in a binding.
    Q_INVOKABLE bool saves(const QString &id) const;
    // Whether the desktop picks this shortcut row's keys itself (the portal),
    // so the row opens its chooser through act instead of recording keys. A
    // constant, so QML may call it in a binding.
    Q_INVOKABLE bool desktopPicksKeys(const QString &id) const;
    // While the shortcut row id records, every Global Shortcut lets go of its
    // keys, so pressing the bound ones records them instead of dictating.
    // stopRecordingShortcut takes them back. Recording moves to the latest
    // row started, and a stop for any other row changes nothing, so moving
    // from one row to the next keeps the keys let go.
    Q_INVOKABLE void startRecordingShortcut(const QString &id);
    Q_INVOKABLE void stopRecordingShortcut(const QString &id);

signals:
    void valuesChanged();
    void errorsChanged();

private:
    void setShortcut(const QString &id, GlobalShortcutRole role, const QStringList &keys);
    void setError(const QString &id, const QString &error);
    void refresh();

    ApplicationController *m_controller;
    QVariantMap m_values;
    QVariantMap m_errors;
    // The shortcut row recording, or empty.
    QString m_recordingShortcut;
};

} // namespace speecher
