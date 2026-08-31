#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

#include <QString>

class QComboBox;
class QLabel;
class QPushButton;

namespace speecher {

class SettingsStore;

// The two Output rows no control kind describes: the delivery method, whose
// choices are platform-specific and whose ydotool entry stays closed until the
// virtual keyboard is set up, and the cluster that sets it up. Lives beside the
// page rather than inside it because both reach for the settings store.
class OutputCustomRows {
public:
    explicit OutputCustomRows(SettingsStore &settings);

    SchemaCustomRowFactory factory();
    // Re-probes the virtual keyboard setup and retells both rows about it.
    void refresh();

private:
    SchemaCustomRow makeMethodRow(QWidget *parent, std::function<void()> notifyChanged);
    SchemaCustomRow makeVirtualKeyboardRow(QWidget *parent, std::function<void()> notifyChanged);
    void updateButtons();
    void setUpOrEnable();
    void disable();
    void removeSetup();

    SettingsStore &m_settings;
    QComboBox *m_method = nullptr;
    // A stored method this platform offers no item for, such as ydotool on
    // macOS. The combo shows Automatic instead, but the saved value survives
    // until the user picks something themselves.
    QString m_unlistedMethod;
    QLabel *m_status = nullptr;
    QPushButton *m_setUp = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_disable = nullptr;
    QPushButton *m_remove = nullptr;
    std::function<void()> m_notifyChanged;
};

} // namespace speecher
