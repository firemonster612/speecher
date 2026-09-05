#pragma once

#include "core/settings/SettingsSchema.h"

#include <QFileSystemWatcher>
#include <QObject>

#include <functional>
#include <optional>

namespace speecher {

class ApplicationController;
class SettingsStore;

namespace win {

// SpeecherBridge's value objects, as plain C++: everything a row needs already
// evaluated against the draft, so the renderer never touches a std::function.

struct CollectionColumnSnapshot {
    QString id;
    QString title;
    ColumnKind kind = ColumnKind::Text;
    // Choice columns only.
    QList<RowOption> options;
    bool stretch = false;
};

struct CollectionSnapshot {
    QList<CollectionColumnSnapshot> columns;
    // Leading records a reader can see but nobody can edit or delete.
    int lockedRecordCount = 0;
    QVariantMap blankRecord;
    // Empty on a collection nothing may be added to by hand.
    QString addLabel;
    // Empty unless the collection can also be filled from a file.
    QString importLabel;
    QStringList importFileExtensions;
    QList<RowOption> actions;
    int minimumHeight = 0;
};

struct RowSnapshot {
    QString id;
    QString label;
    QString help;
    RowKind kind = RowKind::Info;
    QString actionLabel;
    NumberRange range;
    int contentWidthHint = 0;
    // bool for a Toggle, int for a Number, a QList<QVariantMap> for a
    // Collection, a QString otherwise; invalid for a row without a value.
    QVariant value;
    QList<RowOption> options;
    QList<RowOption> suggestions;
    bool enabled = true;
    QString tooltip;
    QString disabledHelp;
    QString disabledAction;
    QString disabledActionLabel;
    QString groupId;
    // Set on a Collection row, and on the one Custom row that is a table.
    std::optional<CollectionSnapshot> collection;
};

struct SectionSnapshot {
    QString title;
    QString help;
    QList<RowSnapshot> rows;
};

struct PageSnapshot {
    QString id;
    QString title;
    QString iconId;
    QList<SectionSnapshot> sections;
};

// The settings surface as the schema describes it, over a draft of the stored
// settings — SpeecherBridge's SchemaState for the Windows front end. Reading
// pages() re-derives every row's value, choices and enabled flag from the
// draft, so a reader sees the effect of its own writes.
class SettingsModel {
public:
    explicit SettingsModel(ApplicationController *controller);
    ~SettingsModel();

    QList<PageSnapshot> pages() const;
    void setValue(const QString &rowId, const QVariant &value);
    // Writes the draft back to the store, applies the theme and re-reads it.
    void commit();
    // Discards edits left from the last showing and re-reads the store.
    void reloadDraft();
    // Lets the rows whose choices are slow to gather — a device enumeration —
    // offer them from now on. Called once the window has painted.
    void loadExpensiveRows();

    // Empty when these records are consistent; otherwise one message per
    // problem, ready to show to a person.
    QStringList problemsWith(const QList<QVariantMap> &records, const QString &rowId) const;
    // Empty when the records were consistent, in which case they are saved.
    QStringList save(const QList<QVariantMap> &records, const QString &rowId);

    struct ImportResult {
        // The records already there with the file's merged in, or nothing when
        // the file could not be used.
        std::optional<QList<QVariantMap>> records;
        QString problem;
    };
    ImportResult recordsImportedFrom(const QByteArray &bytes,
                                     const QList<QVariantMap> &into,
                                     const QString &rowId) const;
    // What a cell says on hover, which a learned correction answers per record.
    QString tooltipForColumn(const QString &columnId,
                             const QString &rowId,
                             const QVariantMap &record) const;

    const AppSettings &draft() const;
    SettingsStore *store() const;

    // The OpenAI credential as the Accounts pane shows it: a secret to type
    // while the app settings key is the chosen source, and the resolved status
    // of whichever source it is otherwise.
    bool credentialIsEditable() const;
    QString credentialStatus() const;
    QString anthropicCredentialStatus() const;
    QString readApiKey() const;
    // Empty when the keyring took it, otherwise why it refused.
    QString saveApiKey(const QString &apiKey);

    // The Claude credentials file changed, so the Anthropic status line is
    // worth re-reading.
    std::function<void()> anthropicCredentialsChanged;
    // A commit went through, taking the theme with it — the Windows stand-in
    // for the Theme::apply call the other front ends make.
    std::function<void()> themeChanged;
    // targetAccessibility moved, so gated rows re-derive.
    std::function<void()> capabilitiesChanged;

private:
    const SettingsRow *rowWithId(const QString &rowId) const;
    const CollectionDescriptor *collectionForRow(const SettingsRow &row) const;
    RowSnapshot rowSnapshot(const SettingsRow &row) const;
    QList<RowOption> optionsForRow(const SettingsRow &row) const;

    ApplicationController *m_controller;
    SettingsStore *m_store;
    SettingsSchema m_schema;
    AppSettings m_draft;
    Capabilities m_capabilities;
    // The one Custom row this front end draws as a table, kept here because its
    // records are this front end's shape rather than the schema's.
    CollectionDescriptor m_profileGrid;
    // Choices that cost a device enumeration stay out of a snapshot until the
    // front end has painted and asked for them.
    bool m_expensiveReady = false;
    // Owns the signal connections, so they end when the model does.
    QObject m_lifetime;
    QFileSystemWatcher m_credentialWatcher;
};

} // namespace win
} // namespace speecher
