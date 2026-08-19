#pragma once

#include "core/settings/SettingsSchema.h"

#include <QScrollArea>

#include <functional>

class QVBoxLayout;

namespace speecher {

class PlatformComposition;
class ProviderRegistry;

// What the Qt front end can tell the schema about this machine.
SchemaContext qtSchemaContext(const PlatformComposition &platform,
                              const ProviderRegistry &providers,
                              const QString &primaryOutputStatus);

// A widget a Custom row supplies, plus the two closures the renderer needs to
// drive it like any other row.
struct SchemaCustomRow {
    QWidget *widget = nullptr;
    std::function<QVariant()> value;
    std::function<void(const QVariant &)> setValue;
};

// Renders one SettingsPage as the Qt front end's settings page, and drives
// load, appendToDraft and hasChanges from the descriptors rather than from a
// hand-written line per field.
class SchemaSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit SchemaSettingsPage(const SettingsPage &page, QWidget *parent = nullptr);

    void load(const AppSettings &settings);
    // The rows the descriptors mark expensive, once the window has painted.
    void loadExpensiveRows(const AppSettings &settings);
    void appendToDraft(AppSettings &draft) const;
    bool hasChanges(const AppSettings &settings) const;
    void setCapabilities(const Capabilities &capabilities);

signals:
    void changed();
    void actionTriggered(const QString &rowId);

private:
    struct Row {
        SettingsRow descriptor;
        QWidget *frame = nullptr;
        QWidget *control = nullptr;
        std::function<QVariant()> value;
        std::function<void(const QVariant &)> setValue;
    };

    void addSection(const SettingsSection &section, QVBoxLayout *pageLayout);
    void addRow(const SettingsRow &descriptor, QWidget *card, bool separator);
    QWidget *makeControl(const SettingsRow &descriptor, QWidget *card, Row &row);
    void applyRow(const Row &row, const AppSettings &settings);
    void refreshEnabledRows();

    QList<Row> m_rows;
    Capabilities m_capabilities;
    AppSettings m_loaded;
    bool m_expensiveRowsLoaded = false;
};

} // namespace speecher
