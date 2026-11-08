#pragma once

#include "core/SettingsEdit.h"
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
// value has an entry in the table in the .cpp. While the setup assistant
// is open it drafts the rows its steps choose: their edits wait in one
// draft, shown everywhere over the saved settings, until setup saves or
// drops it.
class QuickSettings final : public QObject, public HostSingleton<QuickSettings> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QVariantMap errors READ errors NOTIFY errorsChanged)

public:
    explicit QuickSettings(ApplicationController *controller, QObject *parent = nullptr);

    QVariantMap values() const;
    // The settings values shows: the saved ones with the draft's edits
    // applied, so System's outcomes agree with the rows.
    AppSettings shown() const;
    // Why a row's last save was refused, by row id, such as a shortcut another
    // app owns; a row with nothing refused is absent.
    QVariantMap errors() const;
    // Saves one row's value, or keeps it in the draft while one drafts the
    // row. A row this build does not save is refused with a warning in the
    // log. A shortcut row takes the keys ShortcutField records (["Ctrl",
    // "Space"]) and hands them to the controller, which binds and keeps
    // them; an empty list clears the shortcut. Why the controller refused
    // one lands in errors.
    Q_INVOKABLE void set(const QString &id, const QVariant &value);
    // Opens a draft of the rows ids, replacing any draft open: from now on
    // set keeps their edits, in order, rather than saving them. Shortcut
    // rows can't be drafted; the controller binds them as they are set.
    Q_INVOKABLE void startDraft(const QStringList &ids);
    // Saves the draft's edits over the settings saved by then, in one save,
    // and closes it.
    Q_INVOKABLE void saveDraft();
    // Closes the draft without saving it.
    Q_INVOKABLE void dropDraft();
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
    // Whether the shortcut row id's recorder may record keys, as lastKeyAlone
    // gave them. A key it refuses (singleKeyRecordingRefusal), such as Left
    // Shift, says why in errors and changes nothing saved.
    Q_INVOKABLE bool takesKeyAlone(const QString &id, const QStringList &keys);

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
    // The rows the draft holds, and their edits; empty while none is open.
    QStringList m_draftRows;
    SettingsEdit m_draft;
    // The shortcut row recording, or empty.
    QString m_recordingShortcut;
    // The KeyboardEvent.code of the last key pressed or released while a row
    // records.
    QString m_lastKeyCode;
};

} // namespace speecher
