#include "frontend/win/SettingsModel.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/PlatformComposition.h"
#include "app/UpdateBanner.h"
#include "app/UpdateController.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"
#include "frontend/win/CustomRows.h"
#include "providers/CodexCredentialStorage.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/ProviderRegistry.h"
#include "providers/TranscriptRefinementPrompt.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace speecher::win {

namespace {

const QString kAppSettingsKeyAuthMode = QStringLiteral("settings");

// "CSV files (*.csv);;All files (*)" names the types a file picker may accept;
// the bare "*" is every type, which a picker says by allowing none.
QStringList fileExtensions(const QString &filter)
{
    QStringList extensions;
    static const QRegularExpression pattern(QStringLiteral("\\*\\.([A-Za-z0-9]+)"));
    QRegularExpressionMatchIterator matches = pattern.globalMatch(filter);
    while (matches.hasNext()) {
        extensions.append(matches.next().captured(1));
    }
    return extensions;
}

} // namespace

QList<RowOption> providerOptions(ProviderRole role, const ProviderRegistry &registry)
{
    QList<RowOption> options;
    const QList<ProviderDescriptor> providers =
        role == ProviderRole::Speech ? registry.speechProviders() : registry.refinementProviders();
    for (const ProviderDescriptor &provider : providers) {
        options.append({provider.id, provider.label, provider.summary});
    }
    return options;
}

namespace {

// What qtSchemaContext builds for the other two front ends, assembled here
// because that helper lives in the Qt front end this one must not link.
SchemaContext winSchemaContext(const PlatformComposition &platform,
                               const ProviderRegistry &providers,
                               const QString &lastSeenVersion,
                               const LocalSetup &localSetup)
{
    QList<RefinementProvider> refiners;
    for (const ProviderDescriptor &provider : providers.refinementProviders()) {
        refiners.append({provider.id, provider.label, provider.supportsScreenshotContext});
    }
    return {
        providerOptions(ProviderRole::Speech, providers),
        refiners,
        [&platform] {
            QList<RowOption> options;
            for (const AudioInputDeviceInfo &device : platform.availableAudioInputDevices()) {
                options.append({device.id,
                                device.isDefault
                                    ? QStringLiteral("%1 (default)").arg(device.label)
                                    : device.label});
            }
            return options;
        },
        false,
        QStringLiteral(SPEECHER_VERSION),
        lastSeenVersion,
        [&localSetup] { return localSetup.liveFacts(); },
        [&localSetup](const AppSettings &draft) { return localSetup.liveFacts(draft); },
        builtInDictationSystemPrompt(),
        [&localSetup] { return localSetup.hardware().profile; },
    };
}

// A sign-in that lands in the Windows credential manager instead cannot be
// watched, but the file appearing is what the directory watch is for.
QString codexCredentialsPath()
{
    return CodexCredentialStorage().authFilePath();
}

// The nearest directory at or above the one that would hold this file that
// exists, never above the user profile root. Empty when the path lies outside
// the profile, or when not even the profile root is there.
QString nearestExistingDirectory(const QString &credentialsPath)
{
    const QString home = QDir::cleanPath(QDir::homePath());
    QString directory = QDir::cleanPath(QFileInfo(credentialsPath).absolutePath());
    while (!QFileInfo::exists(directory)) {
        if (directory == home) {
            return {};
        }
        const QString parent = QDir::cleanPath(QFileInfo(directory).absolutePath());
        const bool withinProfile =
            parent == home || parent.startsWith(home + QLatin1Char('/'));
        if (parent == directory || !withinProfile) {
            return {};
        }
        directory = parent;
    }
    return directory;
}

void refreshCredentialWatch(QFileSystemWatcher *watcher, const QStringList &credentialPaths)
{
    if (!watcher->files().isEmpty()) {
        watcher->removePaths(watcher->files());
    }
    if (!watcher->directories().isEmpty()) {
        watcher->removePaths(watcher->directories());
    }
    for (const QString &credentialsPath : credentialPaths) {
        if (QFileInfo::exists(credentialsPath)) {
            watcher->addPath(credentialsPath);
        }
        // On a fresh machine neither the file nor its directory exists — the
        // Codex CLI creates ~/.codex at the first `codex login` — so the watch
        // falls back to the nearest existing ancestor, at worst the user
        // profile root. Every change there re-runs this function, so the watch
        // tightens onto the directory and then the file as they appear, and
        // the broad profile-root watch is dropped again.
        const QString directory = nearestExistingDirectory(credentialsPath);
        if (directory.isEmpty()) {
            continue;
        }
        // Two providers can share a directory; watching it twice is a warning
        // and a duplicate signal.
        if (!watcher->directories().contains(directory)) {
            watcher->addPath(directory);
        }
    }
}


} // namespace

SettingsModel::SettingsModel(ApplicationController *controller)
    : m_controller(controller)
    , m_store(controller->settings())
    , m_schema(buildSettingsSchema(winSchemaContext(*controller->platform(),
                                                    *controller->providerRegistry(),
                                                    controller->pendingWhatsNewVersion(),
                                                    *controller->localSetup())))
    , m_draft(m_store->snapshot())
    , m_loaded(m_draft)
    , m_capabilities{controller->accessibilitySupported() && controller->accessibilityEnabled(),
                     controller->updates()->supportsAutomaticDownloads()}
{
    bindCheckForUpdatesRow(m_schema, controller->updateBanner());
    m_capabilities.launchAtLoginAccepted = controller->launchAtLoginAccepted();
    const QStringList credentialPaths{m_store->claudeCredentialsPath(), codexCredentialsPath()};
    refreshCredentialWatch(&m_credentialWatcher, credentialPaths);
    const auto credentialsChanged = [this, credentialPaths] {
        refreshCredentialWatch(&m_credentialWatcher, credentialPaths);
        if (anthropicCredentialsChanged) {
            anthropicCredentialsChanged();
        }
    };
    QObject::connect(&m_credentialWatcher,
                     &QFileSystemWatcher::fileChanged,
                     &m_lifetime,
                     [credentialsChanged](const QString &) { credentialsChanged(); });
    QObject::connect(&m_credentialWatcher,
                     &QFileSystemWatcher::directoryChanged,
                     &m_lifetime,
                     [credentialsChanged](const QString &) { credentialsChanged(); });
    QObject::connect(controller,
                     &ApplicationController::accessibilityStateChanged,
                     &m_lifetime,
                     [this](bool supported, bool enabled, bool) {
                         m_capabilities.targetAccessibility = supported && enabled;
                         if (capabilitiesChanged) {
                             capabilitiesChanged();
                         }
                     });
    QObject::connect(controller,
                     &ApplicationController::launchAtLoginAcceptedChanged,
                     &m_lifetime,
                     [this, controller] {
                         m_capabilities.launchAtLoginAccepted =
                             controller->launchAtLoginAccepted();
                         if (capabilitiesChanged) {
                             capabilitiesChanged();
                         }
                     });
}

SettingsModel::~SettingsModel() = default;

const SettingsRow *SettingsModel::rowWithId(const QString &rowId) const
{
    for (const SettingsPage &page : m_schema.pages) {
        for (const SettingsSection &section : page.sections) {
            for (const SettingsRow &row : section.rows) {
                if (row.id == rowId) {
                    return &row;
                }
            }
        }
    }
    return nullptr;
}

const CollectionDescriptor *SettingsModel::collectionForRow(const SettingsRow &row) const
{
    return row.collection.records ? &row.collection : nullptr;
}

QList<RowOption> SettingsModel::optionsForRow(const SettingsRow &row) const
{
    // An expensive row's choices are the ones a front end fetches after it has
    // painted — a device enumeration — so a snapshot leaves them out until it
    // has said it is ready for them.
    if (row.expensive && !m_expensiveReady) {
        return {};
    }
    if (row.options) {
        return row.options(m_draft);
    }
    return row.kind == RowKind::Custom ? customRowOptions(row.id, m_draft, *m_store) : QList<RowOption>();
}

RowSnapshot SettingsModel::rowSnapshot(const SettingsRow &row) const
{
    RowSnapshot snapshot;
    snapshot.id = row.id;
    snapshot.label = row.labelValue ? row.labelValue(m_draft) : row.label;
    snapshot.help = row.helpValue ? row.helpValue(m_draft) : row.help;
    snapshot.helpTone = row.helpTone ? row.helpTone(m_draft) : StatusTone::Normal;
    snapshot.kind = row.kind;
    snapshot.actionLabel = row.actionLabelValue ? row.actionLabelValue(m_draft) : row.actionLabel;
    snapshot.targetPage = row.targetPage;
    snapshot.range = row.range;
    snapshot.units = row.units;
    snapshot.contentWidthHint = row.contentWidthHint;
    snapshot.options = optionsForRow(row);
    snapshot.suggestions = row.suggestions ? row.suggestions(m_draft) : QList<RowOption>();
    snapshot.suggests = bool(row.suggestions);
    snapshot.secret = row.secret;
    snapshot.placeholder = row.placeholder;
    snapshot.multiline = row.multiline;
    snapshot.enabled = !row.enabled || row.enabled(m_draft, m_capabilities);
    snapshot.tooltip = row.tooltip;
    snapshot.disabledHelp =
        row.disabledHelpValue ? row.disabledHelpValue(m_draft, m_capabilities) : row.disabledHelp;
    snapshot.disabledAction = row.disabledAction;
    snapshot.disabledActionLabel = row.disabledActionLabel;
    snapshot.groupId = row.groupId;
    snapshot.ratings = row.ratings ? row.ratings(m_draft) : QList<Rating>();
    snapshot.ratedModels = row.ratedModels ? row.ratedModels(m_draft) : QList<RatedModel>();
    if (const CollectionDescriptor *collection = collectionForRow(row)) {
        CollectionSnapshot table;
        const QList<QVariantMap> records = collection->records(m_draft);
        for (const CollectionColumn &column : collection->columns) {
            CollectionColumnSnapshot shown;
            shown.id = column.id;
            shown.title = column.title;
            shown.kind = column.kind;
            shown.options = column.options ? column.options(m_draft) : QList<RowOption>();
            shown.stretch = column.stretch;
            shown.multiline = column.multiline;
            shown.placeholder = column.placeholder;
            shown.dialogOnly = column.dialogOnly;
            shown.help = column.help;
            shown.detailColumn = column.detailColumn;
            shown.everyChoice = column.everyChoice;
            shown.someChoice = column.someChoice;
            shown.iconId = column.iconId;
            shown.ownLine = column.ownLine;
            for (const QVariantMap &record : records) {
                if (column.recordOptions) {
                    shown.recordOptions.append(column.recordOptions(m_draft, record));
                }
                if (column.recordNote || column.ownLine) {
                    shown.recordNotes.append(shownFieldNote(column, m_draft, record));
                }
            }
            table.columns.append(shown);
        }
        table.lockedRecordCount = collection->lockedRecordCount ? collection->lockedRecordCount() : 0;
        table.blankRecord = collection->blankRecord;
        table.addLabel = collection->addLabel;
        table.addDialogTitle = collection->addDialogTitle;
        table.editLabel = collection->editLabel;
        table.deleteLabel = collection->deleteLabel;
        table.emptyTitle = collection->emptyTitle;
        table.emptyHelp = collection->emptyHelp;
        table.importLabel = collection->supportsImport.parse ? collection->supportsImport.actionLabel
                                                             : QString();
        table.importFailureTitle = collection->supportsImport.failureTitle;
        table.importFileExtensions = collection->supportsImport.parse
            ? fileExtensions(collection->supportsImport.fileFilter)
            : QStringList();
        table.actions = collection->actions;
        table.minimumHeight = collection->minimumHeight;
        snapshot.collection = table;
        snapshot.value = QVariant::fromValue(records);
        return snapshot;
    }
    if (row.value) {
        switch (row.kind) {
        case RowKind::Toggle:
            snapshot.value = row.value(m_draft).toBool();
            break;
        case RowKind::Number:
            snapshot.value = row.units.isEmpty() ? QVariant(row.value(m_draft).toInt())
                                                 : QVariant(row.value(m_draft).toMap());
            break;
        default:
            snapshot.value = row.value(m_draft).toString();
            break;
        }
    }
    return snapshot;
}

const SettingsSchema &SettingsModel::schema() const
{
    return m_schema;
}

QList<SearchMatch> SettingsModel::search(const QString &query) const
{
    return searchSettings(m_schema, query, m_draft, m_capabilities);
}

SectionSnapshot SettingsModel::section(const SettingsPaneGroup &group) const
{
    SectionSnapshot snapshot{group.title, group.help, {}};
    for (const QString &rowId : group.rows) {
        const SettingsRow *row = rowWithId(rowId);
        if (row && (!row->visible || row->visible(m_draft, m_capabilities))) {
            snapshot.rows.append(rowSnapshot(*row));
        }
    }
    return snapshot;
}

void SettingsModel::setValue(const QString &rowId, const QVariant &value)
{
    const SettingsRow *row = rowWithId(rowId);
    if (!row) {
        qWarning() << "no settings row" << rowId << "to write";
        return;
    }
    if (const CollectionDescriptor *collection = collectionForRow(*row)) {
        collection->apply(m_draft, value.value<QList<QVariantMap>>());
        return;
    }
    if (!row->apply) {
        qWarning() << "settings row" << row->id << "holds no value to write";
        return;
    }
    row->apply(m_draft, value);
}

FallbackListPresentation SettingsModel::fallbackList(ProviderRole role) const
{
    return fallbackListPresentation(role, m_draft, m_controller->localSetup()->liveFacts(m_draft),
                                    providerOptions(role, *m_controller->providerRegistry()),
                                    FallbackSurface::Settings);
}

void SettingsModel::commit()
{
    m_store->applySnapshot(mergeSettingsDraft(m_schema, m_loaded, m_draft, m_store->snapshot()));
    // Where the other front ends call Theme::apply: the WinUI root's
    // RequestedTheme is the Windows equivalent, and the window owns the root.
    if (themeChanged) {
        themeChanged();
    }
    m_draft = m_loaded = m_store->snapshot();
}

void SettingsModel::reloadDraft()
{
    m_draft = m_loaded = m_store->snapshot();
}

void SettingsModel::syncWithStore()
{
    const AppSettings current = m_store->snapshot();
    m_draft = mergeSettingsDraft(m_schema, m_loaded, m_draft, current);
    m_loaded = current;
}

void SettingsModel::loadExpensiveRows()
{
    m_expensiveReady = true;
    refreshAudioInput();
}

bool SettingsModel::refreshAudioInput()
{
    if (!m_expensiveReady) {
        return false;
    }
    const bool audioInput = !m_controller->platform()->availableAudioInputDevices().isEmpty();
    const bool changed = audioInput != m_capabilities.audioInput;
    m_capabilities.audioInput = audioInput;
    return changed;
}

QStringList SettingsModel::problemsWith(const QList<QVariantMap> &records,
                                        const QString &rowId) const
{
    const SettingsRow *row = rowWithId(rowId);
    const CollectionDescriptor *collection = row ? collectionForRow(*row) : nullptr;
    if (!collection || !collection->validate) {
        return {};
    }
    return collection->validate(records);
}

QStringList SettingsModel::save(const QList<QVariantMap> &records, const QString &rowId,
                                 const QList<QVariantMap> &previous)
{
    const QStringList problems = problemsWith(records, rowId);
    if (problems.isEmpty()) {
        const SettingsRow *row = rowWithId(rowId);
        const CollectionDescriptor *collection = row ? collectionForRow(*row) : nullptr;
        if (collection) collection->apply(m_loaded, previous);
        setValue(rowId, QVariant::fromValue(records));
        commit();
    }
    return problems;
}

SettingsModel::ImportResult SettingsModel::recordsImportedFrom(const QByteArray &bytes,
                                                               const QList<QVariantMap> &into,
                                                               const QString &rowId) const
{
    const SettingsRow *row = rowWithId(rowId);
    const CollectionDescriptor *collection = row ? collectionForRow(*row) : nullptr;
    if (!collection || !collection->supportsImport.parse) {
        return {{}, QStringLiteral("This collection cannot be filled from a file.")};
    }
    QString error;
    const QList<QVariantMap> imported = collection->supportsImport.parse(bytes, &error);
    if (!error.isEmpty()) {
        return {{}, error};
    }
    const QList<QVariantMap> merged = into + imported;
    if (collection->validate) {
        const QStringList problems = collection->validate(merged);
        if (!problems.isEmpty()) {
            return {{}, problems.join(QLatin1Char('\n'))};
        }
    }
    return {merged, {}};
}

QString SettingsModel::writingProfileDeletionNotice(const QString &profileId) const
{
    return speecher::writingProfileDeletionNotice(m_draft, profileId);
}

const CollectionColumn *SettingsModel::columnWithId(const QString &rowId,
                                                   const QString &columnId) const
{
    const SettingsRow *row = rowWithId(rowId);
    const CollectionDescriptor *collection = row ? collectionForRow(*row) : nullptr;
    if (!collection) {
        return nullptr;
    }
    for (const CollectionColumn &column : collection->columns) {
        if (column.id == columnId) {
            return &column;
        }
    }
    return nullptr;
}

QString SettingsModel::tooltipForColumn(const QString &columnId,
                                        const QString &rowId,
                                        const QVariantMap &record) const
{
    const CollectionColumn *column = columnWithId(rowId, columnId);
    if (!column) {
        return {};
    }
    return column->recordTooltip ? column->recordTooltip(record) : column->tooltip;
}

QList<IconCell> SettingsModel::iconsFor(const QList<QVariantMap> &records,
                                        const QString &rowId,
                                        const QString &columnId) const
{
    const CollectionColumn *column = columnWithId(rowId, columnId);
    return column && column->icons ? column->icons(records, m_draft) : QList<IconCell>();
}

QString SettingsModel::choiceSetText(const QString &rowId,
                                     const QString &columnId,
                                     const QStringList &ids) const
{
    const CollectionColumn *column = columnWithId(rowId, columnId);
    return column ? speecher::choiceSetText(*column, ids, m_draft) : QString();
}

const AppSettings &SettingsModel::draft() const
{
    return m_draft;
}

SettingsStore *SettingsModel::store() const
{
    return m_store;
}

bool SettingsModel::credentialIsEditable() const
{
    return m_draft.refinement.openAiAuthMode == kAppSettingsKeyAuthMode;
}

QString SettingsModel::credentialStatus() const
{
    // The schema marks openAiAuth expensive: resolving the status can enter
    // the keyring, which the window defers until readApiKey()'s dispatcher
    // turn. Until then, render a loading value instead of blocking the paint.
    if (!m_credentialReady) {
        return checkingCredentialsStatus();
    }
    // The remote CLI Proxy fields decide which credential the status
    // describes; passing them matches the Qt call site (ProviderCustomRows).
    return OpenAiAuthProvider(m_controller->secretStore(),
                              m_draft.refinement.openAiAuthMode,
                              m_draft.refinement.openAiCliproxyAccount,
                              m_store->cliproxyOauthDir(),
                              {},
                              {},
                              m_draft.refinement.cliproxyBaseUrl,
                              m_draft.refinement.cliproxyApiKey)
        .status()
        .text;
}

QString SettingsModel::anthropicCredentialStatus() const
{
    return win::anthropicCredentialStatus(m_draft, *m_store);
}

QString SettingsModel::readApiKey()
{
    // This is the deferred keyring turn: any unlock prompt happens here, and
    // the store caches the read, so the status may resolve from now on. The
    // window rebuilds the providers pane right after, replacing the loading
    // value.
    m_credentialReady = true;
    return m_controller->secretStore()->apiKey();
}

QString SettingsModel::saveApiKey(const QString &apiKey)
{
    SecretStore *secrets = m_controller->secretStore();
    if (secrets->saveApiKey(apiKey.trimmed())) {
        return {};
    }
    return secrets->status();
}

} // namespace speecher::win
