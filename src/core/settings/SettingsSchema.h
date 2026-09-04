#pragma once

#include "core/AppSettings.h"

#include <QList>
#include <QString>
#include <QVariant>

#include <functional>

namespace speecher {

enum class RowKind {
    Choice,
    Toggle,
    Text,
    Number,
    Action,
    Info,
    Collection,
    Custom,
};

struct RowOption {
    QString id;
    QString label;
    QString help;
    bool enabled = true;
};

enum class ColumnKind {
    Text,
    Choice,
    Toggle,
    ReadOnly,
};

// One typed column of a collection. A record's value for the column lives under
// `id` in the record, so a key no column names is metadata the editor carries
// but never shows.
struct CollectionColumn {
    QString id;
    QString title;
    ColumnKind kind = ColumnKind::Text;
    // Choice columns only.
    std::function<QList<RowOption>()> options;
    // The column that takes the leftover width; the others size to content.
    bool stretch = false;
    // Shown on the cells of this column.
    QString tooltip;
    // Shown instead when what to say depends on the record, such as the
    // confidence behind a learned correction.
    std::function<QString(const QVariantMap &)> recordTooltip;
};

// Records a collection can be filled from a file with. Core owns the parse; the
// file chooser and the refusal belong to the front end.
struct CollectionImport {
    QString actionLabel;
    // A name filter, such as "CSV files (*.csv);;All files (*)".
    QString fileFilter;
    // What a front end titles a refusal, whether the parse or the merge failed.
    QString failureTitle;
    // Leaves error empty when every record in the file is usable.
    std::function<QList<QVariantMap>(const QByteArray &, QString *error)> parse;
};

// A table of records with typed columns, plus add and delete. Five surfaces in
// this app are this shape, so describing it once is what lets a front end reach
// for its own table view instead of reimplementing the editor.
struct CollectionDescriptor {
    QList<CollectionColumn> columns;
    std::function<QList<QVariantMap>(const AppSettings &)> records;
    // Receives the editable records only, never the locked ones.
    std::function<void(AppSettings &, const QList<QVariantMap> &)> apply;
    QVariantMap blankRecord;
    // Leading records a reader can see but nobody can edit or delete, such as
    // the built-in application recognition rules.
    std::function<int()> lockedRecordCount;
    // Empty when the records are consistent; otherwise one message per problem,
    // ready to show to a person.
    std::function<QStringList(const QList<QVariantMap> &)> validate;
    // Empty on a collection nothing may be added to by hand.
    QString addLabel;
    // Set when the collection can also be filled from a file.
    CollectionImport supportsImport;
    // Commands beyond add and delete. The schema names them so a second front
    // end can offer the same ones; what they do stays with the front end,
    // because both of today's two undo its own edit history.
    QList<RowOption> actions;
    int minimumHeight = 0;
};

struct NumberRange {
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    QString suffix;
};

// What the machine Speecher is running on can do, for rows that are only
// meaningful when it can. Grows a member when a row needs one, not before.
struct Capabilities {
    bool targetAccessibility = false;
    bool automaticUpdateDownloads = false;
    // The platform theme honours a Light or Dark request. Assumed until a
    // request is seen to be ignored.
    bool colorSchemeOverride = true;
};

struct SettingsRow {
    // Stable across front ends: a renderer uses it to name its control, and a
    // Custom or Action row is recognised by it.
    QString id;
    // First Stable Release that ships this row. Empty for rows that predate
    // release-note discovery or should not appear as something new.
    QString sinceVersion;
    QString label;
    // One line under the label, only where the label alone is not enough.
    QString help;
    std::function<QString(const AppSettings &)> helpValue;
    RowKind kind = RowKind::Info;
    // The caption of an Action row's control, which is not its label.
    QString actionLabel;
    NumberRange range;
    // Room to reserve for a Choice row's value, in characters, so a list that
    // arrives late does not resize the row under the reader. Zero sizes the
    // control to whatever it holds.
    int contentWidthHint = 0;
    // Shown on the control itself, where help sits beneath the label.
    QString tooltip;
    // Replaces tooltip while enabled says no. A front end shows it beside the
    // disabled control, not only on hover.
    QString disabledHelp;
    // An action a front end can run to lift the gate, with the caption of the
    // control that runs it. Empty when nothing in the app can.
    QString disabledAction;
    QString disabledActionLabel;
    // Rows that name the same group render inside one container and are enabled
    // or disabled together, so they must all declare the same gate.
    QString groupId;
    // Set on a Collection row, whose value and apply wrap its records.
    CollectionDescriptor collection;
    std::function<QVariant(const AppSettings &)> value;
    std::function<void(AppSettings &, const QVariant &)> apply;
    std::function<QList<RowOption>(const AppSettings &)> options;
    // Text rows only: values worth offering, though the row still takes any
    // text a person types.
    std::function<QList<RowOption>(const AppSettings &)> suggestions;
    std::function<bool(const AppSettings &, const Capabilities &)> enabled;
    // A row that is only worth showing sometimes, such as a caution about the
    // model currently chosen. Absent means always.
    std::function<bool(const AppSettings &, const Capabilities &)> visible;
    // Populating this row reaches for something slow — a device enumeration, a
    // keyring — so a front end leaves it until it has painted once.
    bool expensive = false;
};

// One card of a page. Both front ends render a page as its sections, so this
// is where the information architecture lives: which rows sit together, under
// what heading, and in what order.
struct SettingsSection {
    QString title;
    // One line under the title, only where the title alone is not enough.
    QString help;
    QList<SettingsRow> rows;
};

struct SettingsPage {
    QString id;
    QString title;
    QString iconName;   // freedesktop icon theme name
    QString symbolName; // SF Symbol name
    QList<SettingsSection> sections;
};

struct SettingsSchema {
    QList<SettingsPage> pages;

    const SettingsPage &page(const QString &id) const;
};

// A refinement provider as the settings surface sees it: what to call it, and
// what it can be asked to do.
struct RefinementProvider {
    QString id;
    QString label;
    bool supportsScreenshotContext = false;
};

// What the descriptors need to be built. A value type, so a test can make one
// without a registry, a sound server or a window.
struct SchemaContext {
    QList<RowOption> speechProviders;
    QList<RefinementProvider> refinementProviders;
    std::function<QList<RowOption>()> audioInputDevices;
    // This build can set up a virtual keyboard, so the Output page carries the
    // Advanced section that drives it.
    bool virtualKeyboardSetup = false;
    QString currentVersion;
    QString lastSeenVersion;
};

SettingsSchema buildSettingsSchema(const SchemaContext &context);

// Nightly metadata does not make a new Stable Release, and dotted components
// are numbers rather than text (0.10 follows 0.2).
int compareBaseVersions(const QString &left, const QString &right);

// The one sentence that describes the restore-clipboard setting, wherever it
// is offered (Output page, setup assistant).
QString restoreClipboardDescription();

// The microphone choice as it is offered: a system-default entry ahead of the
// devices that exist, and a disabled placeholder standing in for a saved device
// that has gone away. Shared with the setup assistant's own device list.
QList<RowOption> audioDeviceOptions(const QList<RowOption> &devices,
                                    const QString &selectedDeviceId);

} // namespace speecher
