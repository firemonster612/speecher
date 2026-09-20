#include "frontend/win/SettingsModel.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "app/UpdateController.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"
#include "frontend/win/CustomRows.h"
#include "providers/CodexCredentialStorage.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/ProviderRegistry.h"

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

// What qtSchemaContext builds for the other two front ends, assembled here
// because that helper lives in the Qt front end this one must not link.
SchemaContext winSchemaContext(const PlatformComposition &platform,
                               const ProviderRegistry &providers,
                               const QString &lastSeenVersion)
{
    QList<RowOption> speech;
    for (const ProviderDescriptor &provider : providers.speechProviders()) {
        speech.append({provider.id, provider.label, provider.summary});
    }
    QList<RefinementProvider> refiners;
    for (const ProviderDescriptor &provider : providers.refinementProviders()) {
        refiners.append({provider.id, provider.label, provider.supportsScreenshotContext});
    }
    return {
        speech,
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
                                                    controller->pendingWhatsNewVersion())))
    , m_draft(m_store->snapshot())
    , m_loaded(m_draft)
    , m_capabilities{controller->accessibilitySupported() && controller->accessibilityEnabled(),
                     controller->updates()->supportsAutomaticDownloads()}
{
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
    if (row.kind == RowKind::Custom) {
        return customRowOptions(row.id, m_draft, *m_store);
    }
    return row.options ? row.options(m_draft) : QList<RowOption>();
}

RowSnapshot SettingsModel::rowSnapshot(const SettingsRow &row) const
{
    RowSnapshot snapshot;
    snapshot.id = row.id;
    snapshot.label = row.label;
    snapshot.help = row.helpValue ? row.helpValue(m_draft) : row.help;
    snapshot.kind = row.kind;
    snapshot.actionLabel = row.actionLabel;
    snapshot.range = row.range;
    snapshot.contentWidthHint = row.contentWidthHint;
    snapshot.options = optionsForRow(row);
    snapshot.suggestions = row.suggestions ? row.suggestions(m_draft) : QList<RowOption>();
    snapshot.enabled = !row.enabled || row.enabled(m_draft, m_capabilities);
    snapshot.tooltip = row.tooltip;
    snapshot.disabledHelp = row.disabledHelp;
    snapshot.disabledAction = row.disabledAction;
    snapshot.disabledActionLabel = row.disabledActionLabel;
    snapshot.groupId = row.groupId;
    if (const CollectionDescriptor *collection = collectionForRow(row)) {
        CollectionSnapshot table;
        for (const CollectionColumn &column : collection->columns) {
            table.columns.append({column.id,
                                  column.title,
                                  column.kind,
                                  column.options ? column.options() : QList<RowOption>(),
                                  column.stretch});
        }
        table.lockedRecordCount = collection->lockedRecordCount ? collection->lockedRecordCount() : 0;
        table.blankRecord = collection->blankRecord;
        table.addLabel = collection->addLabel;
        table.importLabel = collection->supportsImport.parse ? collection->supportsImport.actionLabel
                                                             : QString();
        table.importFileExtensions = collection->supportsImport.parse
            ? fileExtensions(collection->supportsImport.fileFilter)
            : QStringList();
        table.actions = collection->actions;
        table.minimumHeight = collection->minimumHeight;
        snapshot.collection = table;
        snapshot.value = QVariant::fromValue(collection->records(m_draft));
        return snapshot;
    }
    if (row.value) {
        switch (row.kind) {
        case RowKind::Toggle:
            snapshot.value = row.value(m_draft).toBool();
            break;
        case RowKind::Number:
            snapshot.value = row.value(m_draft).toInt();
            break;
        default:
            snapshot.value = row.value(m_draft).toString();
            break;
        }
    }
    return snapshot;
}

QList<PageSnapshot> SettingsModel::pages() const
{
    QList<PageSnapshot> pages;
    for (const SettingsPage &page : m_schema.pages) {
        PageSnapshot pageSnapshot{page.id, page.title, page.iconId, {}};
        for (const SettingsSection &section : page.sections) {
            SectionSnapshot sectionSnapshot{section.title, section.help, {}};
            for (const SettingsRow &row : section.rows) {
                if (row.visible && !row.visible(m_draft, m_capabilities)) {
                    continue;
                }
                sectionSnapshot.rows.append(rowSnapshot(row));
            }
            pageSnapshot.sections.append(sectionSnapshot);
        }
        pages.append(pageSnapshot);
    }
    return pages;
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

void SettingsModel::loadExpensiveRows()
{
    m_expensiveReady = true;
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

QString SettingsModel::tooltipForColumn(const QString &columnId,
                                        const QString &rowId,
                                        const QVariantMap &record) const
{
    const SettingsRow *row = rowWithId(rowId);
    const CollectionDescriptor *collection = row ? collectionForRow(*row) : nullptr;
    if (!collection) {
        return {};
    }
    for (const CollectionColumn &column : collection->columns) {
        if (column.id != columnId) {
            continue;
        }
        return column.recordTooltip ? column.recordTooltip(record) : column.tooltip;
    }
    return {};
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
    return OpenAiAuthProvider(m_controller->secretStore(),
                              m_draft.refinement.openAiAuthMode,
                              m_draft.refinement.openAiCliproxyAccount,
                              m_store->cliproxyOauthDir())
        .status();
}

QString SettingsModel::anthropicCredentialStatus() const
{
    return win::anthropicCredentialStatus(m_draft, *m_store);
}

QString SettingsModel::readApiKey() const
{
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
