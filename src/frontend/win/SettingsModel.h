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
    // Text columns only: the value may hold several lines.
    bool multiline = false;
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
    // Offers values even while the list is empty, so the control stays the same.
    bool suggests = false;
    // Text rows only: shown masked.
    bool secret = false;
    // Text rows only: the value may hold several lines.
    bool multiline = false;
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

// The settings surface as the schema describes it, over a draft of the stored
// settings — SpeecherBridge's SchemaState for the Windows front end. Reading
// section() re-derives every row's value, choices and enabled flag from the
// draft, so a reader sees the effect of its own writes.
class SettingsModel {
public:
    explicit SettingsModel(ApplicationController *controller);
    ~SettingsModel();

    // The pane arrangement, fixed for the life of the model.
    const SettingsSchema &schema() const;
    // A pane group's rows as the draft now has them, for its card.
    SectionSnapshot section(const SettingsPaneGroup &group) const;
    // The panes a sidebar search shows, with rows as the draft shows them.
    QStringList searchPanes(const QString &query) const;
    void setValue(const QString &rowId, const QVariant &value);
    // Writes the draft back to the store, applies the theme and re-reads it.
    void commit();
    // Discards edits left from the last showing and re-reads the store.
    void reloadDraft();
    // Takes in what the store gained since the draft was read, such as the
    // model LocalSetup moved dictation to, keeping the draft's own edits.
    void syncWithStore();
    // Lets the rows whose choices are slow to gather — a device enumeration —
    // offer them from now on. Called once the window has painted.
    void loadExpensiveRows();

    // Empty when these records are consistent; otherwise one message per
    // problem, ready to show to a person.
    QStringList problemsWith(const QList<QVariantMap> &records, const QString &rowId) const;
    // Empty when the records were consistent, in which case they are saved.
    QStringList save(const QList<QVariantMap> &records, const QString &rowId,
                     const QList<QVariantMap> &previous);

    struct ImportResult {
        // The records already there with the file's merged in, or nothing when
        // the file could not be used.
        std::optional<QList<QVariantMap>> records;
        QString problem;
    };
    ImportResult recordsImportedFrom(const QByteArray &bytes,
                                     const QList<QVariantMap> &into,
                                     const QString &rowId) const;
    // What deleting the custom Writing Profile `profileId` changes in the
    // draft, empty when nothing points at it.
    QString writingProfileDeletionNotice(const QString &profileId) const;
    // The pill beside each record's stretch column, empty for none, for these
    // records as they stand and the draft's other settings.
    QStringList badgesFor(const QList<QVariantMap> &records, const QString &rowId) const;
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
    QString readApiKey();
    // Empty when the keyring took it, otherwise why it refused.
    QString saveApiKey(const QString &apiKey);

    // A watched credential file changed — Claude Code's, or the Codex CLI's —
    // so both providers' status lines are worth re-reading.
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
    AppSettings m_loaded;
    Capabilities m_capabilities;
    // Choices that cost a device enumeration stay out of a snapshot until the
    // front end has painted and asked for them.
    bool m_expensiveReady = false;
    // Resolving the OpenAI credential status can enter the keyring, which the
    // window defers to readApiKey()'s turn; until then it renders as loading.
    bool m_credentialReady = false;
    // Owns the signal connections, so they end when the model does.
    QObject m_lifetime;
    QFileSystemWatcher m_credentialWatcher;
};

} // namespace win
} // namespace speecher
