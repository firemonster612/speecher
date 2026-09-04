#pragma once

#include "core/settings/SettingsSchema.h"

#include <QScrollArea>

#include <functional>

class QVBoxLayout;

namespace speecher {

class PlatformComposition;
class ProviderRegistry;

namespace settings {
class FormRow;
class SettingsCard;
} // namespace settings

// What the Qt front end can tell the schema about this machine.
SchemaContext qtSchemaContext(const PlatformComposition &platform,
                              const ProviderRegistry &providers,
                              const QString &lastSeenVersion = {});

// A widget a Custom row supplies, plus the two closures the renderer needs to
// drive it like any other row.
struct SchemaCustomRow {
    QWidget *widget = nullptr;
    std::function<QVariant()> value;
    std::function<void(const QVariant &)> setValue;
    // Too wide for a row's control column, so it spans the row below the
    // title instead.
    bool fullWidth = false;
    // Shown under the row's subtitle, such as a sign-in status.
    QWidget *detail = nullptr;
};

// How a front end hands the renderer a widget for a row it wants to draw
// itself, whether that is a Custom row or a collection it renders as something
// other than a table. A page whose rows need something the renderer cannot
// reach, such as the settings store, supplies its own. Returning no widget
// leaves the row to the renderer.
using SchemaCustomRowFactory = std::function<
    SchemaCustomRow(const SettingsRow &descriptor, QWidget *parent, std::function<void()> notifyChanged)>;

// Renders one SettingsPage as a column of cards, one per section, and drives
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
    void refresh();

signals:
    void changed();
    void actionTriggered(const QString &rowId);

private:
    struct Row {
        SettingsRow descriptor;
        settings::FormRow *frame = nullptr;
        QWidget *control = nullptr;
        // The row-shaped explanation (and fix) shown above the row while its
        // gate says no. Rows of one group share it.
        settings::FormRow *gateNote = nullptr;
        std::function<QVariant()> value;
        std::function<void(const QVariant &)> setValue;
    };

    // A card only earns its place on screen while at least one of its rows
    // does.
    struct Section {
        settings::SettingsCard *card = nullptr;
        int rowStart = 0;
        int rowEnd = 0;
    };

    void addSection(const SettingsSection &section, QVBoxLayout *column);
    void addRow(const SettingsRow &descriptor,
                settings::SettingsCard *card,
                settings::FormRow *gateNote);
    settings::FormRow *addGateNote(const SettingsRow &descriptor, settings::SettingsCard *card);
    SchemaCustomRow supplyRow(const SettingsRow &descriptor,
                              QWidget *host,
                              const std::function<void()> &notifyChanged);
    QWidget *makeControl(const SettingsRow &descriptor, QWidget *host, Row &row);
    void applyRow(const Row &row, const AppSettings &settings);
    void refreshRows();

    SchemaCustomRowFactory m_customRows;
    QList<Row> m_rows;
    QList<Section> m_sections;
    Capabilities m_capabilities;
    AppSettings m_loaded;
    bool m_expensiveRowsLoaded = false;
};

} // namespace speecher
