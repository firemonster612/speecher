#pragma once

#include "core/settings/SettingsSchema.h"

#include <QScrollArea>

#include <functional>

class QVBoxLayout;
class QLabel;
class QPushButton;

namespace speecher {

class InlineMessage;
class PlatformComposition;
class ProviderRegistry;

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
    // Too wide to sit in a row's control column, so it gets the whole card
    // width under a heading of its own.
    bool fullWidth = false;
    // Sits beside the row's title rather than in its control column.
    QWidget *titleAccessory = nullptr;
    // Called with the page's draft each time the page re-derives its rows,
    // for a widget showing something that depends on other settings.
    std::function<void(const AppSettings &)> refresh;
    // Called instead of disabling the widget when its row's gate closes, for a
    // widget that stays readable and only stops taking edits.
    std::function<void(bool)> setEditable;
    // Lays out card rows of its own, titles included, so it goes into the
    // card as it is, with no heading or inset around it.
    bool cardRows = false;
    // Reads under the row's title and description, such as a live status
    // that would otherwise crowd the control column.
    QWidget *detail = nullptr;
    // Take the place of the section's title above its card and its footnote
    // under it, for a row whose heading follows the settings.
    QWidget *header = nullptr;
    QLabel *footer = nullptr;
};

// How a front end hands the renderer a widget for a row it wants to draw
// itself, whether that is a Custom row or a collection it renders as something
// other than a table. A page whose rows need something the renderer cannot
// reach, such as the settings store, supplies its own. Returning no widget
// leaves the row to the renderer.
using SchemaCustomRowFactory = std::function<
    SchemaCustomRow(const SettingsRow &descriptor, QWidget *parent, std::function<void()> notifyChanged)>;

// Renders a pane's sections as the Qt front end's settings page, and drives
// load, appendToDraft and hasChanges from the descriptors rather than from a
// hand-written line per field. The window header carries the page title; an
// intro, when there is one, is the first line under it.
class SchemaSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit SchemaSettingsPage(const QList<SettingsSection> &sections,
                                QWidget *parent = nullptr,
                                SchemaCustomRowFactory customRows = {},
                                const QString &intro = {});

    void load(const AppSettings &settings);
    // Empty when every collection on the page is consistent.
    QStringList validate() const;
    // The rows the descriptors mark expensive, once the window has painted.
    void loadExpensiveRows(const AppSettings &settings);
    void appendToDraft(AppSettings &draft) const;
    bool hasChanges(const AppSettings &settings) const;
    void setCapabilities(const Capabilities &capabilities);
    void refresh();
    // Scrolls the row into view for a search that found it, and with
    // focusControl also gives the row's control the focus.
    void revealRow(const QString &rowId, bool focusControl);

signals:
    void changed();
    void actionTriggered(const QString &rowId);

protected:
    void changeEvent(QEvent *event) override;

private:
    struct Row {
        SettingsRow descriptor;
        QWidget *frame = nullptr;
        QWidget *control = nullptr;
        QLabel *title = nullptr;
        QLabel *description = nullptr;
        // While the gate is closed the description says why, unless a page
        // notice does, or an earlier row of the same
        // group already says it.
        bool explainsGate = false;
        std::function<QVariant()> value;
        std::function<void(const QVariant &)> setValue;
        std::function<void(const AppSettings &)> refresh;
        std::function<void(bool)> setEditable;
        // The button row that opens the dialog this row is shown in, if any.
        QPushButton *opener = nullptr;
        // What a Custom row puts in place of its section's title and footnote.
        QWidget *header = nullptr;
        QLabel *footer = nullptr;
    };

    // A button row standing in for rows shown in a dialog, and what its
    // description says about them.
    struct DialogOpener {
        QPushButton *button = nullptr;
        std::function<QString(const AppSettings &)> summary;
    };

    // One message at the top of the page for every gate that many rows share
    // or an action can lift, such as desktop accessibility, however many rows
    // it holds.
    struct GateNotice {
        QString key;
        QWidget *holder = nullptr;
        InlineMessage *message = nullptr;
    };

    // A section's chrome — its card, title and help note — only earns its
    // place on screen while at least one of its rows does.
    struct Section {
        QWidget *card = nullptr;
        QWidget *label = nullptr;
        // The space above the section, which goes with it.
        QWidget *gap = nullptr;
        // Hidden while it has nothing to say.
        QLabel *note = nullptr;
        int rowStart = 0;
        int rowEnd = 0;
    };

    // A spaced section keeps the gap between sections above it.
    void addSection(const SettingsSection &section, QVBoxLayout *pageLayout, bool spaced);
    // Adds the button row to the card and returns the form of its dialog's card.
    QWidget *addDialog(const RowDialog &dialog, QWidget *cardForm);
    void addRow(const SettingsRow &descriptor, QWidget *host, bool explainsGate);
    void addGateNotice(const SettingsRow &descriptor, QVBoxLayout *pageLayout);
    SchemaCustomRow supplyRow(const SettingsRow &descriptor,
                              QWidget *host,
                              const std::function<void()> &notifyChanged);
    QWidget *makeControl(const SettingsRow &descriptor, QWidget *card, Row &row);
    void applyRow(const Row &row, const AppSettings &settings);
    void refreshRows();

    SchemaCustomRowFactory m_customRows;
    QList<Row> m_rows;
    QList<Section> m_sections;
    QList<GateNotice> m_gateNotices;
    QList<DialogOpener> m_dialogs;
    Capabilities m_capabilities;
    AppSettings m_loaded;
    bool m_expensiveRowsLoaded = false;
};

} // namespace speecher
