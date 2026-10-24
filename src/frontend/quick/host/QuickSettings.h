#pragma once

#include "core/ShortcutBinding.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

namespace speecher {

class ApplicationController;

// A shortcut as the keycaps the UI draws: {"Ctrl", "Space"}.
QStringList shortcutKeys(const ShortcutBinding &binding);

// The UI's Settings singleton: the saved settings by the schema's row ids,
// read from and written through the daemon's SettingsStore, so the UI, the
// daemon and every CLI run see the same values. A row the table in the .cpp
// does not save yet reads as absent and greys out (rowEnabled).
class QuickSettings final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)

public:
    explicit QuickSettings(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickSettings() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickSettings *create(QQmlEngine *, QJSEngine *);

    QVariantMap values() const;
    // Saves one row's value. A row this build does not save is refused with
    // a warning in the log.
    Q_INVOKABLE void set(const QString &id, const QVariant &value);
    // Runs an action row, such as runSetup.
    Q_INVOKABLE void act(const QString &id);
    // Whether a schema row (Schema.row(id)) can be used now: the backend
    // saves its value or runs its action, and its enabledWhen holds. Rows the
    // page draws itself (kind "custom") only answer to enabledWhen.
    Q_INVOKABLE bool rowEnabled(const QVariantMap &row) const;

signals:
    void valuesChanged();

private:
    void refresh();

    ApplicationController *m_controller;
    QVariantMap m_values;
};

} // namespace speecher
