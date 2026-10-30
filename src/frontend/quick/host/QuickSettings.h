#pragma once

#include "core/ShortcutBinding.h"
#include "frontend/quick/host/HostSingleton.h"

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
// daemon and every CLI run see the same values. Every schema row with a
// value has an entry in the table in the .cpp.
class QuickSettings final : public QObject, public HostSingleton<QuickSettings> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QVariantMap errors READ errors NOTIFY errorsChanged)

public:
    explicit QuickSettings(ApplicationController *controller, QObject *parent = nullptr);

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
    // Records that the setup assistant finished, so it does not run again,
    // and opens any files that arrived while it was up.
    Q_INVOKABLE void completeSetup();
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
    // While a row records, the key of the last key press or release, as a
    // shortcut row takes one key on its own (["key:AltRight"]), for
    // ShortcutField's keyAlone: a QML key event can't tell Right Alt from
    // Left. Empty for a key the vocabulary lacks.
    Q_INVOKABLE QStringList lastKeyAlone() const;

signals:
    void valuesChanged();
    void errorsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setShortcut(const QString &id, GlobalShortcutRole role, const QStringList &keys);
    void setError(const QString &id, const QString &error);
    void refresh();

    ApplicationController *m_controller;
    QVariantMap m_values;
    QVariantMap m_errors;
    // The shortcut row recording, or empty.
    QString m_recordingShortcut;
    // The KeyboardEvent.code of the last key pressed or released while a row
    // records.
    QString m_lastKeyCode;
};

} // namespace speecher
