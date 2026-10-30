#pragma once

#include "app/SpeecherRemoval.h"
#include "frontend/quick/host/HostSingleton.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

#include <memory>
#include <optional>

namespace speecher {

class VirtualKeyboardSetup;

// The UI's Removal singleton: removing Speecher from this computer, keeping
// the settings or not, and the report of what it did. Only a computer with
// removal steps has it (SpeecherRemoval::onThisComputer, Linux's); elsewhere
// System.canRemove is false and start does nothing.
class QuickRemoval final : public QObject, public HostSingleton<QuickRemoval> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Removal)
    QML_SINGLETON
    Q_PROPERTY(bool running READ running NOTIFY changed)
    // The last removal's report, or empty before one finishes:
    // {files: {removed, absent, failed}, the desktop's item names;
    // steps: [{part, outcome, error}], RemovalStep's, by name;
    // settingsDeleted; programFile}.
    Q_PROPERTY(QVariantMap report READ report NOTIFY changed)

public:
    // virtualKeyboard, which removal waits for, must outlive this.
    QuickRemoval(VirtualKeyboardSetup *virtualKeyboard, std::optional<SpeecherRemoval::Steps> steps,
                 QObject *parent = nullptr);
    ~QuickRemoval() override;

    // Whether this computer has removal steps.
    bool available() const { return m_removal != nullptr; }
    // From start until the report, waiting for the keyboard included.
    bool running() const;
    QVariantMap report() const { return m_report; }

    // Starts once a virtual keyboard operation under way ends; System starts
    // none while this runs. Once the settings are deleted, nothing saves
    // them again (SettingsStore::deleteAll), and quitting is left.
    Q_INVOKABLE void start(bool keepSettings);

signals:
    void changed();

private:
    enum class Asked {
        KeepSettings,
        DeleteSettings,
    };

    // Starts the removal start asked for, once the keyboard is idle.
    void startAsked();

    VirtualKeyboardSetup *m_virtualKeyboard;
    std::unique_ptr<SpeecherRemoval> m_removal;
    // A removal start asked for that waits for the keyboard.
    std::optional<Asked> m_asked;
    QVariantMap m_report;
};

} // namespace speecher
