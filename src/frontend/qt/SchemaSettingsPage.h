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
    // Too wide to sit in a row's control column, so it gets the whole card
    // width under a heading of its own.
    bool fullWidth = false;
};

// How a front end hands the renderer a widget for a Custom row it recognises.
// A page whose custom rows need something the renderer cannot reach, such as
// the settings store, supplies its own.
using SchemaCustomRowFactory = std::function<
    SchemaCustomRow(const QString &rowId, QWidget *parent, std::function<void()> notifyChanged)>;

// Renders one SettingsPage as the Qt front end's settings page, and drives
// load, appendToDraft and hasChanges from the descriptors rather than from a
// hand-written line per field.
class SchemaSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit SchemaSettingsPage(const SettingsPage &page,
                                QWidget *parent = nullptr,
                                SchemaCustomRowFactory customRows = {});

    void load(const AppSettings &settings);
    // Empty when every collection on the page is consistent.
    QStringList validate() const;
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
        // The container the row shares with the rest of its group, which is
        // what gets enabled and carries the group's tooltip.
        QWidget *group = nullptr;
        std::function<QVariant()> value;
        std::function<void(const QVariant &)> setValue;
    };

    void addSection(const SettingsSection &section, QVBoxLayout *pageLayout);
    void addRow(const SettingsRow &descriptor, QWidget *host, QWidget *group, bool separator);
    QWidget *makeControl(const SettingsRow &descriptor, QWidget *card, Row &row);
    void applyRow(const Row &row, const AppSettings &settings);
    void refreshEnabledRows();

    SchemaCustomRowFactory m_customRows;
    QList<Row> m_rows;
    Capabilities m_capabilities;
    AppSettings m_loaded;
    bool m_expensiveRowsLoaded = false;
};

} // namespace speecher
