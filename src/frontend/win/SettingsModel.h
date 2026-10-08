#pragma once

#include "core/settings/FallbackPresentation.h"
#include "core/settings/SettingsSchema.h"

#include <QFileSystemWatcher>
#include <QObject>

#include <functional>
#include <optional>

namespace speecher {

class ApplicationController;
class ProviderRegistry;
class SettingsStore;

namespace win {

// SpeecherBridge's value objects, as plain C++: everything a row needs already
// evaluated against the draft, so the renderer never touches a std::function.

struct CollectionColumnSnapshot {
    QString id;
    QString title;
    ColumnKind kind = ColumnKind::Text;
    // Choice and ChoiceSet columns only.
    QList<RowOption> options;
    bool stretch = false;
    // Text columns only: the value may hold several lines.
    bool multiline = false;
    QString placeholder;
    // A field of the record dialog, left out of the table.
    bool dialogOnly = false;
    // Said under the field in the record dialog.
    QString help;
    // Text columns only: the column whose value the cell shows as a muted
    // second line.
    QString detailColumn;
    // ChoiceSet columns only: the record dialog's two choices, every option or
    // the ticked ones.
    QString everyChoice;
    QString someChoice;
    // Icon columns only: the schema's platform-neutral icon id.
    QString iconId;
    // Where records are edited as rows of pickers, a titled line of its own.
    bool ownLine = false;
    // For a column whose options depend on the record: one list per record,
    // in the records' order. Empty otherwise.
    QList<QList<RowOption>> recordOptions;
    // For an own-line column or one whose note depends on the record, what
    // shows under each record's field (shownFieldNote). Empty otherwise.
    QList<FieldNote> recordNotes;
};

struct CollectionSnapshot {
    QList<CollectionColumnSnapshot> columns;
    // Leading records a reader can see but nobody can edit or delete.
    int lockedRecordCount = 0;
    QVariantMap blankRecord;
    // Empty on a collection nothing may be added to by hand.
    QString addLabel;
    QString addDialogTitle;
    // Empty on a collection whose records are only edited in place.
    QString editLabel;
    QString deleteLabel;
    // What an empty editor says; empty on a collection that always has records.
    QString emptyTitle;
    QString emptyHelp;
    // Empty unless the collection can also be filled from a file.
    QString importLabel;
    QString importFailureTitle;
    QStringList importFileExtensions;
    QList<RowOption> actions;
    int minimumHeight = 0;
};

struct RowSnapshot {
    QString id;
    QString label;
    QString help;
    // Negative where help says why something can't work right now.
    StatusTone helpTone = StatusTone::Normal;
    RowKind kind = RowKind::Info;
    QString actionLabel;
    // An Action row that opens a subpage, by its page id.
    QString targetPage;
    NumberRange range;
    QList<NumberUnit> units;
    int contentWidthHint = 0;
    // bool for a Toggle, int for a Number, a QVariantMap of "number" and
    // "unit" for a Number with units, a QList<QVariantMap> for a Collection,
    // a QString otherwise; invalid for a row without a value.
    QVariant value;
    QList<RowOption> options;
    QList<RowOption> suggestions;
    // Offers values even while the list is empty, so the control stays the same.
    bool suggests = false;
    // Text rows only: shown masked.
    bool secret = false;
    QString placeholder;
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
    // Rating rows only: the chosen provider's bars.
    QList<Rating> ratings;
    // ModelList rows only: the models, with any bars of their own.
    QList<RatedModel> ratedModels;
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
    // The panes and rows a search finds, with rows as the draft shows them.
    QList<SearchMatch> search(const QString &query) const;
    void setValue(const QString &rowId, const QVariant &value);
    // A role's fallbacks as the draft and LocalSetup's facts have them.
    FallbackListPresentation fallbackList(ProviderRole role) const;
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
    // Asks the system again whether there is a microphone at all, after a
    // device change or as the window comes back from where its disabled note
    // sent the person. True when the answer changed. Nothing is asked before
    // loadExpensiveRows.
    bool refreshAudioInput();

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
    // Each record's cell in the Icon column columnId, for these records as
    // they stand and the draft's other settings.
    QList<IconCell> iconsFor(const QList<QVariantMap> &records,
                             const QString &rowId,
                             const QString &columnId) const;
    // What a cell says on hover, which a learned correction answers per record.
    QString tooltipForColumn(const QString &columnId,
                             const QString &rowId,
                             const QVariantMap &record) const;
    // What a ChoiceSet cell says for a record's ids, against the draft's options.
    QString choiceSetText(const QString &rowId,
                          const QString &columnId,
                          const QStringList &ids) const;

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
    const CollectionColumn *columnWithId(const QString &rowId, const QString &columnId) const;
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
