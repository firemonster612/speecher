#include "frontend/mac/SpeecherBridge.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "app/UpdateController.h"
#include "core/SecretStore.h"
#include "core/ShortcutBinding.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationSession.h"
#include "frontend/mac/MacCustomRows.h"
// The schema context: what this machine can offer the descriptors. Shared with
// the Qt front end rather than reassembled, because the device and provider
// lists are the same lists.
#include "frontend/qt/SchemaSettingsPage.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderRegistry.h"
#include "providers/ProviderSignIn.h"
#include "transcribe/FileTranscriptionSession.h"
#include "ui/Theme.h"

#include <QDebug>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#import <AppKit/AppKit.h>

using speecher::AppSettings;
using speecher::ApplicationController;
using speecher::Capabilities;
using speecher::CollectionColumn;
using speecher::CollectionDescriptor;
using speecher::ColumnKind;
using speecher::PaneLayout;
using speecher::RowKind;
using speecher::RowOption;
using speecher::SettingsRow;
using speecher::SettingsSchema;
using speecher::SettingsStore;

namespace {

const QString kAppSettingsKeyAuthMode = QStringLiteral("settings");

SpeecherRowKind bridgedKind(RowKind kind)
{
    switch (kind) {
    case RowKind::Choice:
        return SpeecherRowKindChoice;
    case RowKind::Toggle:
        return SpeecherRowKindToggle;
    case RowKind::Text:
        return SpeecherRowKindText;
    case RowKind::Number:
        return SpeecherRowKindNumber;
    case RowKind::Action:
        return SpeecherRowKindAction;
    case RowKind::Info:
        return SpeecherRowKindInfo;
    case RowKind::Collection:
        return SpeecherRowKindCollection;
    case RowKind::Custom:
        return SpeecherRowKindCustom;
    }
}

SpeecherColumnKind bridgedColumnKind(ColumnKind kind)
{
    switch (kind) {
    case ColumnKind::Text:
        return SpeecherColumnKindText;
    case ColumnKind::Choice:
        return SpeecherColumnKindChoice;
    case ColumnKind::Toggle:
        return SpeecherColumnKindToggle;
    case ColumnKind::ReadOnly:
        return SpeecherColumnKindReadOnly;
    }
}

SpeecherPaneLayout bridgedPaneLayout(PaneLayout layout)
{
    switch (layout) {
    case PaneLayout::Sections:
        return SpeecherPaneLayoutSections;
    case PaneLayout::Alternatives:
        return SpeecherPaneLayoutAlternatives;
    case PaneLayout::Shortcut:
        return SpeecherPaneLayoutShortcut;
    case PaneLayout::Transcribe:
        return SpeecherPaneLayoutTranscribe;
    }
}

NSArray<NSString *> *bridgedStrings(const QStringList &strings)
{
    NSMutableArray<NSString *> *bridged = [NSMutableArray array];
    for (const QString &string : strings) {
        [bridged addObject:string.toNSString()];
    }
    return bridged;
}

// A record's values keep their type across the wall, because the keys no column
// shows are timestamps and confidences that a round trip through a string would
// quietly round off.
id bridgedRecordValue(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return @(value.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return @(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return @(value.toDouble());
    default:
        return value.toString().toNSString();
    }
}

QVariant coreRecordValue(id value)
{
    if ([value isKindOfClass:[NSNumber class]]) {
        const char type = *[value objCType];
        if (type == @encode(BOOL)[0] || type == @encode(char)[0]) {
            return QVariant([value boolValue]);
        }
        if (type == @encode(double)[0] || type == @encode(float)[0]) {
            return QVariant([value doubleValue]);
        }
        return QVariant(static_cast<qlonglong>([value longLongValue]));
    }
    return QVariant(QString::fromNSString([value description]));
}

NSDictionary<NSString *, id> *bridgedRecord(const QVariantMap &record)
{
    NSMutableDictionary<NSString *, id> *bridged = [NSMutableDictionary dictionary];
    for (auto it = record.constBegin(); it != record.constEnd(); ++it) {
        bridged[it.key().toNSString()] = bridgedRecordValue(it.value());
    }
    return bridged;
}

QVariantMap coreRecord(NSDictionary<NSString *, id> *record)
{
    QVariantMap core;
    for (NSString *key in record) {
        core.insert(QString::fromNSString(key), coreRecordValue(record[key]));
    }
    return core;
}

NSArray<NSDictionary<NSString *, id> *> *bridgedRecords(const QList<QVariantMap> &records)
{
    NSMutableArray<NSDictionary<NSString *, id> *> *bridged = [NSMutableArray array];
    for (const QVariantMap &record : records) {
        [bridged addObject:bridgedRecord(record)];
    }
    return bridged;
}

QList<QVariantMap> coreRecords(NSArray<NSDictionary<NSString *, id> *> *records)
{
    QList<QVariantMap> core;
    for (NSDictionary<NSString *, id> *record in records) {
        core.append(coreRecord(record));
    }
    return core;
}

id bridgedValue(const SettingsRow &row, const AppSettings &settings)
{
    if (!row.value) {
        return nil;
    }
    switch (row.kind) {
    case RowKind::Toggle:
        return @(row.value(settings).toBool());
    case RowKind::Number:
        return @(row.value(settings).toInt());
    default:
        return row.value(settings).toString().toNSString();
    }
}

QVariant coreValue(const SettingsRow &row, id value)
{
    if (!value) {
        return QVariant();
    }
    switch (row.kind) {
    case RowKind::Toggle:
        return QVariant([value boolValue]);
    case RowKind::Number:
        return QVariant(static_cast<int>([value integerValue]));
    default:
        return QVariant(QString::fromNSString([value isKindOfClass:[NSString class]]
                                                  ? value
                                                  : [value description]));
    }
}

// "CSV files (*.csv);;All files (*)" names the types an open panel may accept;
// the bare "*" is every type, which a panel says by allowing none.
NSArray<NSString *> *fileExtensions(const QString &filter)
{
    NSMutableArray<NSString *> *extensions = [NSMutableArray array];
    static const QRegularExpression pattern(QStringLiteral("\\*\\.([A-Za-z0-9]+)"));
    QRegularExpressionMatchIterator matches = pattern.globalMatch(filter);
    while (matches.hasNext()) {
        [extensions addObject:matches.next().captured(1).toNSString()];
    }
    return extensions;
}

struct SchemaState {
    SettingsStore *store = nullptr;
    SettingsSchema schema;
    AppSettings draft;
    AppSettings loaded;
    Capabilities capabilities;
    // Choices that cost a device enumeration stay out of a snapshot until the
    // front end has painted and asked for them.
    bool expensiveReady = false;
};

struct BridgeState {
    QPointer<ApplicationController> controller;
    // Owns the signal connections, so they end when the bridge does.
    QObject lifetime;
    QFileSystemWatcher credentialWatcher;
    // The transcript survives the dictation that produced it, so the menu bar
    // panel can still offer it once the panel that showed it has gone.
    QString lastTranscript;
    // The setup assistant's microphone meter. Made per start and destroyed on
    // stop: an input object kept past the assistant can hold the capture
    // source open alongside dictation's own.
    speecher::AudioInput *setupMeter = nullptr;
    // A round of provider checks that a newer round replaced answers to
    // nobody. Speech supersession is per provider — probes claim their slot
    // with a fresh number from checkRound, so re-probing one changed sign-in
    // cannot strand the other rows' in-flight verdicts. Refinement has no
    // single-provider re-probe: it supersedes per round on its own
    // refinementCheckGeneration counter.
    quint64 checkRound = 0;
    QHash<QString, quint64> speechProbeGeneration;
    quint64 refinementCheckGeneration = 0;
    // The CLI Proxy API opt-in shared with the other assistants, created on
    // first use so its opt-out memory spans the assistant's lifetime.
    std::unique_ptr<speecher::ProviderSignIn> setupSignIn;
};

// Runs a provider's prepare or refresh job off the main thread and answers on
// the bridge's lifetime object, dropping a verdict stillCurrent disowns.
// Thread lifetime is runProviderProbe's business: its registry join covers
// the job (a prepare job reads provider objects the controller owns; a thread
// left running past them was observed as heap corruption in tests), and its
// owner guard covers this callback.
template <typename Job, typename Report>
void probeInBackground(BridgeState *state,
                       std::function<bool()> stillCurrent,
                       Job &&job,
                       Report report)
{
    auto probeJob = std::make_shared<std::decay_t<Job>>(std::forward<Job>(job));
    using ProbeResult = decltype(probeJob->run());
    speecher::runProviderProbe<ProbeResult>(
        state->controller->providerRegistry(),
        &state->lifetime,
        [probeJob] { return probeJob->run(); },
        [state, probeJob, stillCurrent = std::move(stillCurrent), report](
            const ProbeResult &result) {
            if (!stillCurrent()) {
                return;
            }
            // The job's apply closure belongs to a provider the controller
            // owns; a bridge kept alive past its controller (a probe callback
            // can retain it through the Swift flow model) must not run it.
            if (!state->controller) {
                return;
            }
            if (probeJob->apply) {
                probeJob->apply(result);
            }
            report(result.ok, result.message);
        });
}

void refreshCredentialWatch(QFileSystemWatcher *watcher, const QString &credentialsPath)
{
    if (!watcher->files().isEmpty()) {
        watcher->removePaths(watcher->files());
    }
    if (!watcher->directories().isEmpty()) {
        watcher->removePaths(watcher->directories());
    }
    if (QFileInfo::exists(credentialsPath)) {
        watcher->addPath(credentialsPath);
    }
    QString directory = QFileInfo(credentialsPath).absolutePath();
    while (!QFileInfo::exists(directory)) {
        const QString parent = QFileInfo(directory).absolutePath();
        if (parent == directory) {
            return;
        }
        directory = parent;
    }
    watcher->addPath(directory);
}

// The Qt key an NSEvent's unmodified characters stand for. Qt's key enum uses
// the unshifted ASCII code for every printable key the shortcut binder accepts,
// so the binder's own table stays the only list of what macOS can register.
int qtKeyForCharacters(NSString *characters)
{
    if (characters.length != 1) {
        return 0;
    }
    const unichar character = [characters characterAtIndex:0];
    if (character >= NSF1FunctionKey && character <= NSF12FunctionKey) {
        return Qt::Key_F1 + (character - NSF1FunctionKey);
    }
    switch (character) {
    case ' ':
        return Qt::Key_Space;
    case '\r':
        return Qt::Key_Return;
    case '\t':
        return Qt::Key_Tab;
    case 0x1b:
        return Qt::Key_Escape;
    default:
        break;
    }
    return QChar(character).toUpper().unicode();
}

// Qt maps the Mac keyboard onto its portable enum: the Command key arrives as
// Qt::ControlModifier and the Control key as Qt::MetaModifier.
Qt::KeyboardModifiers qtModifiersForFlags(NSUInteger flags)
{
    Qt::KeyboardModifiers modifiers;
    if (flags & NSEventModifierFlagCommand) {
        modifiers |= Qt::ControlModifier;
    }
    if (flags & NSEventModifierFlagControl) {
        modifiers |= Qt::MetaModifier;
    }
    if (flags & NSEventModifierFlagOption) {
        modifiers |= Qt::AltModifier;
    }
    if (flags & NSEventModifierFlagShift) {
        modifiers |= Qt::ShiftModifier;
    }
    return modifiers;
}

} // namespace

@interface RowOptionModel ()
@property (nonatomic, copy) NSString *rowOptionId;
@property (nonatomic, copy) NSString *label;
@property (nonatomic, copy) NSString *help;
@property (nonatomic) BOOL enabled;
@end

@implementation RowOptionModel
@end

@interface SpeecherProviderModel ()
@property (nonatomic, copy) NSString *providerId;
@property (nonatomic, copy) NSString *label;
@property (nonatomic, copy) NSString *credentialSource;
@property (nonatomic, copy) NSString *setupHint;
@property (nonatomic, copy) NSString *summary;
@end

@implementation SpeecherProviderModel
@end

@implementation SpeecherTranscribeOptions
@end

@interface SpeecherTranscriptResult ()
@property (nonatomic, copy) NSString *path;
@property (nonatomic, copy) NSString *raw;
@property (nonatomic, copy) NSString *refined;
@property (nonatomic, copy) NSString *savedPath;
@property (nonatomic, copy) NSString *error;
@property (nonatomic) BOOL failed;
@end

@implementation SpeecherTranscriptResult
@end

static SpeecherTranscriptResult *bridgedTranscriptResult(const speecher::TranscribeFileResult &result)
{
    SpeecherTranscriptResult *bridged = [[SpeecherTranscriptResult alloc] init];
    bridged.path = result.path.toNSString();
    bridged.raw = result.raw.toNSString();
    bridged.refined = result.refined.toNSString();
    bridged.savedPath = result.savedPath.toNSString();
    bridged.error = result.error.toNSString();
    bridged.failed = result.failed();
    return bridged;
}

@interface CollectionColumnModel ()
@property (nonatomic, copy) NSString *columnId;
@property (nonatomic, copy) NSString *title;
@property (nonatomic) SpeecherColumnKind kind;
@property (nonatomic, copy) NSArray<RowOptionModel *> *options;
@property (nonatomic) BOOL stretch;
@end

@implementation CollectionColumnModel
@end

@interface CollectionModel ()
@property (nonatomic, copy) NSArray<CollectionColumnModel *> *columns;
@property (nonatomic) NSInteger lockedRecordCount;
@property (nonatomic, copy) SpeecherRecord *blankRecord;
@property (nonatomic, copy) NSString *addLabel;
@property (nonatomic, copy) NSString *importLabel;
@property (nonatomic, copy) NSArray<NSString *> *importFileExtensions;
@property (nonatomic, copy) NSArray<RowOptionModel *> *actions;
@property (nonatomic) NSInteger minimumHeight;
@end

@implementation CollectionModel
@end

@interface SettingsRowModel ()
@property (nonatomic, copy) NSString *rowId;
@property (nonatomic, copy) NSString *label;
@property (nonatomic, copy) NSString *help;
@property (nonatomic) SpeecherRowKind kind;
@property (nonatomic, copy) NSString *actionLabel;
@property (nonatomic) NSInteger minimum;
@property (nonatomic) NSInteger maximum;
@property (nonatomic) NSInteger step;
@property (nonatomic, copy) NSString *suffix;
@property (nonatomic, strong, nullable) id value;
@property (nonatomic, copy) NSArray<RowOptionModel *> *options;
@property (nonatomic, copy) NSArray<RowOptionModel *> *suggestions;
@property (nonatomic) BOOL enabled;
@property (nonatomic, copy) NSString *tooltip;
@property (nonatomic, copy) NSString *disabledHelp;
@property (nonatomic, copy) NSString *disabledAction;
@property (nonatomic, copy) NSString *disabledActionLabel;
@property (nonatomic, strong, nullable) CollectionModel *collection;
@end

@implementation SettingsRowModel
@end

@interface SettingsSectionModel ()
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *help;
@property (nonatomic, copy) NSArray<SettingsRowModel *> *rows;
@end

@implementation SettingsSectionModel
@end

@interface SettingsPageModel ()
@property (nonatomic, copy) NSString *pageId;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *symbolName;
@property (nonatomic, copy) NSArray<SettingsSectionModel *> *sections;
@end

@implementation SettingsPageModel
@end

@interface SettingsPaneGroupModel ()
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *help;
@property (nonatomic, copy) NSArray<NSString *> *rows;
@end

@implementation SettingsPaneGroupModel
@end

@interface SettingsPaneModel ()
@property (nonatomic, copy) NSString *paneId;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *symbolName;
@property (nonatomic, copy) NSArray<NSString *> *schemaPages;
@property (nonatomic) SpeecherPaneLayout layout;
@property (nonatomic, copy) NSArray<SettingsPaneGroupModel *> *groups;
@end

@implementation SettingsPaneModel
@end

@interface CollectionImportResult ()
@property (nonatomic, copy, nullable) NSArray<SpeecherRecord *> *records;
@property (nonatomic, copy) NSString *problem;
@end

@implementation CollectionImportResult
@end

// Declared so the bridge below can call it; the C++ types keep it out of the
// public header.
@interface SettingsSchemaModel (Cxx)
- (instancetype)initWithStore:(SettingsStore *)store
                       schema:(const SettingsSchema &)schema
                 capabilities:(const Capabilities &)capabilities;
- (NSArray<RowOptionModel *> *)bridgedOptions:(const QList<RowOption> &)options;
- (void)setTargetAccessibility:(BOOL)available;
- (void)setLaunchAtLoginAccepted:(BOOL)accepted;
// The settings as they stand, including edits not yet committed, which is what
// the credential row has to read the chosen auth mode from.
- (const AppSettings &)draft;
@end

@implementation SettingsSchemaModel {
    SchemaState *_state;
}

- (instancetype)initWithStore:(SettingsStore *)store
                       schema:(const SettingsSchema &)schema
                 capabilities:(const Capabilities &)capabilities
{
    self = [super init];
    if (self) {
        _state = new SchemaState;
        _state->store = store;
        _state->schema = schema;
        _state->draft = _state->loaded = store->snapshot();
        _state->capabilities = capabilities;
    }
    return self;
}

- (void)dealloc
{
    delete _state;
}

- (const CollectionDescriptor *)collectionForRow:(const SettingsRow &)row
{
    return row.collection.records ? &row.collection : nullptr;
}

- (const SettingsRow *)rowWithId:(NSString *)rowId
{
    const QString id = QString::fromNSString(rowId);
    for (const speecher::SettingsPage &page : _state->schema.pages) {
        for (const speecher::SettingsSection &section : page.sections) {
            for (const SettingsRow &row : section.rows) {
                if (row.id == id) {
                    return &row;
                }
            }
        }
    }
    return nullptr;
}

- (NSArray<RowOptionModel *> *)bridgedOptions:(const QList<RowOption> &)options
{
    NSMutableArray<RowOptionModel *> *bridged = [NSMutableArray array];
    for (const RowOption &option : options) {
        RowOptionModel *model = [[RowOptionModel alloc] init];
        model.rowOptionId = option.id.toNSString();
        model.label = option.label.toNSString();
        model.help = option.help.toNSString();
        model.enabled = option.enabled;
        [bridged addObject:model];
    }
    return bridged;
}

- (NSArray<RowOptionModel *> *)optionsForRow:(const SettingsRow &)row
{
    // An expensive row's choices are the ones a front end fetches after it has
    // painted — a device enumeration — so a snapshot leaves them out until it
    // has said it is ready for them.
    if (row.expensive && !_state->expensiveReady) {
        return @[];
    }
    if (row.kind == RowKind::Custom) {
        return [self bridgedOptions:speecher::mac::customRowOptions(row.id,
                                                                    _state->draft,
                                                                    *_state->store)];
    }
    return row.options ? [self bridgedOptions:row.options(_state->draft)] : @[];
}

- (CollectionModel *)collectionModel:(const CollectionDescriptor &)collection
{
    NSMutableArray<CollectionColumnModel *> *columns = [NSMutableArray array];
    for (const CollectionColumn &column : collection.columns) {
        CollectionColumnModel *model = [[CollectionColumnModel alloc] init];
        model.columnId = column.id.toNSString();
        model.title = column.title.toNSString();
        model.kind = bridgedColumnKind(column.kind);
        model.options = column.options ? [self bridgedOptions:column.options()] : @[];
        model.stretch = column.stretch;
        [columns addObject:model];
    }
    CollectionModel *model = [[CollectionModel alloc] init];
    model.columns = columns;
    model.lockedRecordCount = collection.lockedRecordCount ? collection.lockedRecordCount() : 0;
    model.blankRecord = bridgedRecord(collection.blankRecord);
    model.addLabel = collection.addLabel.toNSString();
    model.importLabel = collection.supportsImport.actionLabel.toNSString();
    model.importFileExtensions = collection.supportsImport.parse
        ? fileExtensions(collection.supportsImport.fileFilter)
        : @[];
    model.actions = [self bridgedOptions:collection.actions];
    model.minimumHeight = collection.minimumHeight;
    return model;
}

- (SettingsRowModel *)rowModel:(const SettingsRow &)row
{
    SettingsRowModel *model = [[SettingsRowModel alloc] init];
    model.rowId = row.id.toNSString();
    model.label = row.label.toNSString();
    model.help = (row.helpValue ? row.helpValue(_state->draft) : row.help).toNSString();
    model.kind = bridgedKind(row.kind);
    model.actionLabel = row.actionLabel.toNSString();
    model.minimum = row.range.minimum;
    model.maximum = row.range.maximum;
    model.step = row.range.step;
    model.suffix = row.range.suffix.toNSString();
    model.options = [self optionsForRow:row];
    model.suggestions = row.suggestions ? [self bridgedOptions:row.suggestions(_state->draft)] : @[];
    model.enabled = !row.enabled || row.enabled(_state->draft, _state->capabilities);
    model.tooltip = row.tooltip.toNSString();
    model.disabledHelp = row.disabledHelp.toNSString();
    model.disabledAction = row.disabledAction.toNSString();
    model.disabledActionLabel = row.disabledActionLabel.toNSString();
    if (const CollectionDescriptor *collection = [self collectionForRow:row]) {
        model.collection = [self collectionModel:*collection];
        model.value = bridgedRecords(collection->records(_state->draft));
        return model;
    }
    model.value = bridgedValue(row, _state->draft);
    return model;
}

- (NSArray<SettingsPageModel *> *)pages
{
    NSMutableArray<SettingsPageModel *> *pages = [NSMutableArray array];
    for (const speecher::SettingsPage &page : _state->schema.pages) {
        NSMutableArray<SettingsSectionModel *> *sections = [NSMutableArray array];
        for (const speecher::SettingsSection &section : page.sections) {
            NSMutableArray<SettingsRowModel *> *rows = [NSMutableArray array];
            for (const SettingsRow &row : section.rows) {
                if (row.visible && !row.visible(_state->draft, _state->capabilities)) {
                    continue;
                }
                [rows addObject:[self rowModel:row]];
            }
            SettingsSectionModel *sectionModel = [[SettingsSectionModel alloc] init];
            sectionModel.title = section.title.toNSString();
            sectionModel.help = section.help.toNSString();
            sectionModel.rows = rows;
            [sections addObject:sectionModel];
        }
        SettingsPageModel *pageModel = [[SettingsPageModel alloc] init];
        pageModel.pageId = page.id.toNSString();
        pageModel.title = page.title.toNSString();
        pageModel.symbolName = page.symbolName.toNSString();
        pageModel.sections = sections;
        [pages addObject:pageModel];
    }
    return pages;
}

- (NSArray<SettingsPaneModel *> *)panes
{
    NSMutableArray<SettingsPaneModel *> *panes = [NSMutableArray array];
    for (const speecher::SettingsPane &pane : _state->schema.panes) {
        NSMutableArray<SettingsPaneGroupModel *> *groups = [NSMutableArray array];
        for (const speecher::SettingsPaneGroup &group : pane.groups) {
            SettingsPaneGroupModel *groupModel = [[SettingsPaneGroupModel alloc] init];
            groupModel.title = group.title.toNSString();
            groupModel.help = group.help.toNSString();
            groupModel.rows = bridgedStrings(group.rows);
            [groups addObject:groupModel];
        }
        SettingsPaneModel *paneModel = [[SettingsPaneModel alloc] init];
        paneModel.paneId = pane.id.toNSString();
        paneModel.title = pane.title.toNSString();
        paneModel.symbolName = pane.symbolName.toNSString();
        paneModel.schemaPages = bridgedStrings(pane.schemaPages);
        paneModel.layout = bridgedPaneLayout(pane.layout);
        paneModel.groups = groups;
        [panes addObject:paneModel];
    }
    return panes;
}

- (NSArray<NSArray<NSString *> *> *)sidebarRuns
{
    NSMutableArray<NSArray<NSString *> *> *runs = [NSMutableArray array];
    for (const QStringList &run : _state->schema.sidebarRuns) {
        [runs addObject:bridgedStrings(run)];
    }
    return runs;
}

- (void)setValue:(id)value forRowId:(NSString *)rowId
{
    const SettingsRow *row = [self rowWithId:rowId];
    if (!row) {
        qWarning() << "no settings row" << QString::fromNSString(rowId) << "to write";
        return;
    }
    if (const CollectionDescriptor *collection = [self collectionForRow:*row]) {
        collection->apply(_state->draft, coreRecords(value));
        return;
    }
    if (!row->apply) {
        qWarning() << "settings row" << row->id << "holds no value to write";
        return;
    }
    row->apply(_state->draft, coreValue(*row, value));
}

- (void)commit
{
    _state->store->applySnapshot(mergeSettingsDraft(
        _state->schema, _state->loaded, _state->draft, _state->store->snapshot()));
    // What the Qt front end does after a save, and the reason a theme change
    // reaches NSApp.appearance as well as Qt's own palette.
    speecher::Theme::apply(_state->store->theme());
    _state->draft = _state->loaded = _state->store->snapshot();
}

- (void)reloadDraft
{
    _state->draft = _state->loaded = _state->store->snapshot();
}

- (void)loadExpensiveRows
{
    _state->expensiveReady = YES;
}

- (void)setTargetAccessibility:(BOOL)available
{
    _state->capabilities.targetAccessibility = available;
}

- (void)setLaunchAtLoginAccepted:(BOOL)accepted
{
    _state->capabilities.launchAtLoginAccepted = accepted;
}

- (NSArray<NSString *> *)problemsWith:(NSArray<SpeecherRecord *> *)records forRowId:(NSString *)rowId
{
    const SettingsRow *row = [self rowWithId:rowId];
    const CollectionDescriptor *collection = row ? [self collectionForRow:*row] : nullptr;
    if (!collection || !collection->validate) {
        return @[];
    }
    NSMutableArray<NSString *> *problems = [NSMutableArray array];
    for (const QString &problem : collection->validate(coreRecords(records))) {
        [problems addObject:problem.toNSString()];
    }
    return problems;
}

- (NSArray<NSString *> *)saveRecords:(NSArray<SpeecherRecord *> *)records
                    previousRecords:(NSArray<SpeecherRecord *> *)previous
                           forRowId:(NSString *)rowId
{
    NSArray<NSString *> *problems = [self problemsWith:records forRowId:rowId];
    if (problems.count > 0) return problems;
    const SettingsRow *row = [self rowWithId:rowId];
    const CollectionDescriptor *collection = row ? [self collectionForRow:*row] : nullptr;
    if (collection) collection->apply(_state->loaded, coreRecords(previous));
    [self setValue:records forRowId:rowId];
    [self commit];
    return @[];
}

- (CollectionImportResult *)recordsImportedFrom:(NSData *)data
                                           into:(NSArray<SpeecherRecord *> *)records
                                       forRowId:(NSString *)rowId
{
    CollectionImportResult *result = [[CollectionImportResult alloc] init];
    result.problem = @"";
    const SettingsRow *row = [self rowWithId:rowId];
    const CollectionDescriptor *collection = row ? [self collectionForRow:*row] : nullptr;
    if (!collection || !collection->supportsImport.parse) {
        result.problem = @"This collection cannot be filled from a file.";
        return result;
    }
    QString error;
    const QByteArray bytes(static_cast<const char *>(data.bytes), static_cast<qsizetype>(data.length));
    const QList<QVariantMap> imported = collection->supportsImport.parse(bytes, &error);
    if (!error.isEmpty()) {
        result.problem = error.toNSString();
        return result;
    }
    const QList<QVariantMap> merged = coreRecords(records) + imported;
    if (collection->validate) {
        const QStringList problems = collection->validate(merged);
        if (!problems.isEmpty()) {
            result.problem = problems.join(QLatin1Char('\n')).toNSString();
            return result;
        }
    }
    result.records = bridgedRecords(merged);
    return result;
}

- (NSString *)tooltipForColumn:(NSString *)columnId
                      inRowId:(NSString *)rowId
                       record:(SpeecherRecord *)record
{
    const SettingsRow *row = [self rowWithId:rowId];
    const CollectionDescriptor *collection = row ? [self collectionForRow:*row] : nullptr;
    if (!collection) {
        return @"";
    }
    const QString id = QString::fromNSString(columnId);
    for (const CollectionColumn &column : collection->columns) {
        if (column.id != id) {
            continue;
        }
        return column.recordTooltip ? column.recordTooltip(coreRecord(record)).toNSString()
                                    : column.tooltip.toNSString();
    }
    return @"";
}

- (const AppSettings &)draft
{
    return _state->draft;
}

@end

// The meter's callbacks live on the bridge rather than in BridgeState so ARC
// owns the blocks, and a stop can drop them while the meter object stays.
@interface SpeecherBridge ()
@property (nonatomic, copy, nullable) void (^setupMeterLevel)(float level);
@property (nonatomic, copy, nullable) void (^setupMeterFailure)(NSString *message);
@end

@implementation SpeecherBridge {
    BridgeState *_state;
}

- (instancetype)initWithController:(ApplicationController *)controller
{
    self = [super init];
    if (!self) {
        return nil;
    }
    // Not braced: QObject's constructor is explicit, so lifetime has to be
    // default-initialised rather than copy-initialised from {}.
    _state = new BridgeState;
    _state->controller = controller;
    Capabilities capabilities{controller->accessibilitySupported()
                                  && controller->accessibilityEnabled(),
                              controller->updates()->supportsAutomaticDownloads()};
    // Assigned rather than listed: the caution beside the launch-at-login
    // toggle is the fourth member, and naming it is clearer than spelling out
    // the colour-scheme default in between.
    capabilities.launchAtLoginAccepted = controller->launchAtLoginAccepted();
    _settingsSchema = [[SettingsSchemaModel alloc]
        initWithStore:controller->settings()
               schema:speecher::buildSettingsSchema(
                          speecher::qtSchemaContext(*controller->platform(),
                                                    *controller->providerRegistry(),
                                                    controller->pendingWhatsNewVersion()))
         capabilities:capabilities];
    __weak SpeecherBridge *weakSelf = self;
    BridgeState *state = _state;
    const QString credentialsPath = controller->settings()->claudeCredentialsPath();
    refreshCredentialWatch(&state->credentialWatcher, credentialsPath);
    const auto credentialsChanged = [weakSelf, state, credentialsPath] {
        refreshCredentialWatch(&state->credentialWatcher, credentialsPath);
        SpeecherBridge *bridge = weakSelf;
        if (bridge.anthropicCredentialsChanged) {
            bridge.anthropicCredentialsChanged();
        }
    };
    // Claude Code updates Keychain without touching the credentials file.
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged,
                     &state->lifetime, [credentialsChanged](Qt::ApplicationState state) {
                         if (state == Qt::ApplicationActive) credentialsChanged();
                     });
    QObject::connect(&state->credentialWatcher,
                     &QFileSystemWatcher::fileChanged,
                     &state->lifetime,
                     [credentialsChanged](const QString &) { credentialsChanged(); });
    QObject::connect(&state->credentialWatcher,
                     &QFileSystemWatcher::directoryChanged,
                     &state->lifetime,
                     [credentialsChanged](const QString &) { credentialsChanged(); });
    QObject::connect(controller,
                     &ApplicationController::statusChanged,
                     &_state->lifetime,
                     [weakSelf](const QString &status) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.statusChanged) {
                             bridge.statusChanged(status.toNSString());
                         }
                     });
    QObject::connect(controller,
                     &ApplicationController::audioLevelChanged,
                     &_state->lifetime,
                     [weakSelf](float level) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.audioLevelChanged) {
                             bridge.audioLevelChanged(level);
                         }
                     });
    QObject::connect(controller,
                     &ApplicationController::accessibilityStateChanged,
                     &_state->lifetime,
                     [weakSelf](bool supported, bool enabled, bool) {
                         SpeecherBridge *bridge = weakSelf;
                         [bridge.settingsSchema setTargetAccessibility:supported && enabled];
                         if (bridge.accessibilityChanged) {
                             bridge.accessibilityChanged();
                         }
                     });
    // The same schema-state refresh the Accessibility grant gets: a launch-at-
    // login change this computer refused is what puts the caution beside the
    // toggle, and the row only shows it once the pages are re-read.
    QObject::connect(controller,
                     &ApplicationController::launchAtLoginAcceptedChanged,
                     &_state->lifetime,
                     [weakSelf, controller] {
                         SpeecherBridge *bridge = weakSelf;
                         [bridge.settingsSchema
                             setLaunchAtLoginAccepted:controller->launchAtLoginAccepted()];
                         if (bridge.accessibilityChanged) {
                             bridge.accessibilityChanged();
                         }
                     });
    QObject::connect(controller,
                     &ApplicationController::whatsNewChanged,
                     &_state->lifetime,
                     [weakSelf] {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.whatsNewChanged) {
                             bridge.whatsNewChanged();
                         }
                     });
    QObject::connect(controller->updates(),
                     &speecher::UpdateController::changed,
                     &_state->lifetime,
                     [weakSelf] {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.updateChanged) {
                             bridge.updateChanged();
                         }
                     });
    [self connectPanelTo:controller->session()];
    [self connectTranscriptionTo:controller->fileTranscription()];
    return self;
}

// The file batch's signals, which only the Transcribe pane renders.
- (void)connectTranscriptionTo:(speecher::FileTranscriptionSession *)session
{
    using speecher::FileTranscriptionSession;
    __weak SpeecherBridge *weakSelf = self;
    QObject *lifetime = &_state->lifetime;
    QObject::connect(session, &FileTranscriptionSession::batchStarted, lifetime, [weakSelf](int count) {
        SpeecherBridge *bridge = weakSelf;
        if (bridge.transcriptionBatchStarted) {
            bridge.transcriptionBatchStarted(count);
        }
    });
    QObject::connect(session,
                     &FileTranscriptionSession::fileStarted,
                     lifetime,
                     [weakSelf](int index, const QString &path) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.transcriptionFileStarted) {
                             bridge.transcriptionFileStarted(index, path.toNSString());
                         }
                     });
    QObject::connect(session,
                     &FileTranscriptionSession::fileDecoded,
                     lifetime,
                     [weakSelf](int index, const QVector<float> &peaks, qint64 durationMs) {
                         SpeecherBridge *bridge = weakSelf;
                         if (!bridge.transcriptionFileDecoded) {
                             return;
                         }
                         NSMutableArray<NSNumber *> *levels =
                             [NSMutableArray arrayWithCapacity:NSUInteger(peaks.size())];
                         for (float peak : peaks) {
                             [levels addObject:@(peak)];
                         }
                         bridge.transcriptionFileDecoded(index, levels, durationMs);
                     });
    QObject::connect(session,
                     &FileTranscriptionSession::fileProgress,
                     lifetime,
                     [weakSelf](int index, qreal fraction) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.transcriptionFileProgress) {
                             bridge.transcriptionFileProgress(index, fraction);
                         }
                     });
    QObject::connect(session,
                     &FileTranscriptionSession::filePartialText,
                     lifetime,
                     [weakSelf](int index, const QString &text) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.transcriptionFilePartial) {
                             bridge.transcriptionFilePartial(index, text.toNSString());
                         }
                     });
    QObject::connect(session, &FileTranscriptionSession::fileRefining, lifetime, [weakSelf](int index) {
        SpeecherBridge *bridge = weakSelf;
        if (bridge.transcriptionFileRefining) {
            bridge.transcriptionFileRefining(index);
        }
    });
    QObject::connect(session,
                     &FileTranscriptionSession::fileFinished,
                     lifetime,
                     [weakSelf](int index, const speecher::TranscribeFileResult &result) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.transcriptionFileFinished) {
                             bridge.transcriptionFileFinished(index, bridgedTranscriptResult(result));
                         }
                     });
    QObject::connect(session,
                     &FileTranscriptionSession::batchFinished,
                     lifetime,
                     [weakSelf](const QList<speecher::TranscribeFileResult> &results, bool cancelled) {
                         SpeecherBridge *bridge = weakSelf;
                         if (!bridge.transcriptionBatchFinished) {
                             return;
                         }
                         NSMutableArray<SpeecherTranscriptResult *> *bridged = [NSMutableArray array];
                         for (const speecher::TranscribeFileResult &result : results) {
                             [bridged addObject:bridgedTranscriptResult(result)];
                         }
                         bridge.transcriptionBatchFinished(bridged, cancelled);
                     });
}

// The dictation panel's signals, which the session emits and no page renders.
- (void)connectPanelTo:(speecher::DictationSession *)session
{
    using speecher::DictationSession;
    __weak SpeecherBridge *weakSelf = self;
    BridgeState *state = _state;
    QObject::connect(session,
                     &DictationSession::popupShowRequested,
                     &_state->lifetime,
                     [weakSelf](quint64 generation) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupShowRequested) {
                             bridge.popupShowRequested(generation);
                         }
                     });
    QObject::connect(session, &DictationSession::popupHideRequested, &_state->lifetime, [weakSelf] {
        SpeecherBridge *bridge = weakSelf;
        if (bridge.popupHideRequested) {
            bridge.popupHideRequested();
        }
    });
    QObject::connect(session,
                     &DictationSession::popupStatusChanged,
                     &_state->lifetime,
                     [weakSelf](const QString &status) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupStatusChanged) {
                             bridge.popupStatusChanged(status.toNSString());
                         }
                     });
    // A completed delivery says so on the same line the status uses, which is
    // what the popup showed before this front end existed.
    QObject::connect(session,
                     &DictationSession::popupMessageRequested,
                     &_state->lifetime,
                     [weakSelf](const QString &message) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupStatusChanged) {
                             bridge.popupStatusChanged(message.toNSString());
                         }
                     });
    QObject::connect(session,
                     &DictationSession::previewDisplayChanged,
                     &_state->lifetime,
                     [weakSelf](const QString &preview) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupPreviewChanged) {
                             bridge.popupPreviewChanged(preview.toNSString());
                         }
                     });
    QObject::connect(session,
                     &DictationSession::popupRefinementPreviewChanged,
                     &_state->lifetime,
                     [weakSelf](const QString &preview) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupRefinementPreviewChanged) {
                             bridge.popupRefinementPreviewChanged(preview.toNSString());
                         }
                     });
    QObject::connect(session,
                     &DictationSession::popupRefiningChanged,
                     &_state->lifetime,
                     [weakSelf](bool refining) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupRefiningChanged) {
                             bridge.popupRefiningChanged(refining);
                         }
                     });
    QObject::connect(session,
                     &DictationSession::popupFrozenChanged,
                     &_state->lifetime,
                     [weakSelf](bool frozen) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupFrozenChanged) {
                             bridge.popupFrozenChanged(frozen);
                         }
                     });
    QObject::connect(session,
                     &DictationSession::popupOAuthRefreshRequested,
                     &_state->lifetime,
                     [weakSelf] {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupOAuthRefreshRequested) {
                             bridge.popupOAuthRefreshRequested();
                         }
                     });
    QObject::connect(session,
                     &DictationSession::popupListeningIndicatorRequested,
                     &_state->lifetime,
                     [weakSelf] {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupListeningIndicatorRequested) {
                             bridge.popupListeningIndicatorRequested();
                         }
                     });
    QObject::connect(session,
                     &DictationSession::popupErrorRequested,
                     &_state->lifetime,
                     [weakSelf](const QString &message) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupErrorRequested) {
                             bridge.popupErrorRequested(message.toNSString());
                         }
                     });
    // The running transcript, kept only while it says something: clearing it at
    // the start of the next dictation would take away the one the menu bar
    // panel is still offering.
    QObject::connect(session,
                     &DictationSession::previewChanged,
                     &_state->lifetime,
                     [weakSelf, state](const QString &transcript) {
                         if (transcript.isEmpty()) {
                             return;
                         }
                         state->lastTranscript = transcript;
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.transcriptChanged) {
                             bridge.transcriptChanged(transcript.toNSString());
                         }
                     });
}

- (void)notePopupPresented:(uint64_t)generation
{
    _state->controller->session()->popupPresented(generation);
}

- (NSString *)lastTranscript
{
    return _state->lastTranscript.toNSString();
}

- (BOOL)shortcutSupported
{
    return _state->controller->globalShortcutsSupported();
}

- (NSString *)shortcutDisplay
{
    return _state->controller->globalShortcut().displayText().toNSString();
}

- (NSString *)bindShortcutWithCharacters:(NSString *)characters modifierFlags:(NSUInteger)flags
{
    const int key = qtKeyForCharacters(characters);
    if (key == 0) {
        return @"That key cannot be part of a shortcut.";
    }
    const Qt::KeyboardModifiers modifiers = qtModifiersForFlags(flags);
    // A shortcut with no modifier would swallow the key everywhere on the
    // desktop, including in whatever the dictation is going into.
    if (modifiers == Qt::NoModifier) {
        return @"Hold ⌘, ⌥, ⌃ or ⇧ as part of the shortcut.";
    }
    QString error;
    const QKeySequence sequence(QKeyCombination(modifiers, Qt::Key(key)));
    if (_state->controller->setGlobalShortcut(sequence, &error)) {
        return nil;
    }
    return error.isEmpty() ? @"That shortcut could not be bound." : error.toNSString();
}

- (NSString *)bindCurrentShortcut
{
    QString error;
    if (_state->controller->setGlobalShortcut(_state->controller->globalShortcut(), &error)) {
        return nil;
    }
    return error.isEmpty() ? @"That shortcut could not be bound." : error.toNSString();
}

- (NSString *)currentSingleKeyCode
{
    const speecher::ShortcutBinding binding = _state->controller->globalShortcut();
    return binding.isSingleKey() ? binding.keyCode().toNSString() : nil;
}

- (NSString *)keyCodeNameForMacKeyCode:(unsigned short)keyCode
{
    const speecher::PhysicalKey *key = speecher::physicalKeyForMac(keyCode);
    return key ? [NSString stringWithUTF8String:key->code] : nil;
}

- (NSString *)displayForSingleKeyCode:(NSString *)code
{
    return speecher::ShortcutBinding::singleKey(QString::fromNSString(code))
        .displayText()
        .toNSString();
}

- (NSString *)unsupportedReasonForSingleKeyCode:(NSString *)code
{
    const QString reason = _state->controller->globalShortcutUnsupportedBindingReason(
        speecher::ShortcutBinding::singleKey(QString::fromNSString(code)));
    return reason.isEmpty() ? nil : reason.toNSString();
}

- (NSString *)bindSingleKeyCode:(NSString *)code
{
    QString error;
    if (_state->controller->setGlobalShortcut(
            speecher::ShortcutBinding::singleKey(QString::fromNSString(code)), &error)) {
        return nil;
    }
    return error.isEmpty() ? @"That key could not be bound." : error.toNSString();
}

- (NSString *)warningForSingleKeyCode:(NSString *)code
{
    const speecher::ShortcutBinding binding =
        speecher::ShortcutBinding::singleKey(QString::fromNSString(code));
    // The monitor cannot consume the key, and Option's normal job is
    // composing accents — worth a caveat even though it types nothing alone.
    if (binding.keyCode().startsWith(QStringLiteral("Alt"))) {
        return QStringLiteral("Heads up: %1 is still the accent key, so holding it while "
                              "typing composes special characters.")
            .arg(binding.displayText())
            .toNSString();
    }
    return speecher::singleKeyTypingWarning(binding).toNSString();
}

- (void)beginShortcutRecording
{
    if (_state->controller) _state->controller->suspendGlobalShortcut();
}

- (NSString *)endShortcutRecording
{
    if (!_state->controller) return nil;
    const QString error = _state->controller->resumeGlobalShortcut();
    return error.isEmpty() ? nil : error.toNSString();
}

- (void)dealloc
{
    delete _state;
}

- (NSString *)stateName
{
    return _state->controller->stateName().toNSString();
}

- (void)toggle
{
    _state->controller->toggle();
}

- (void)startListening
{
    _state->controller->startListening();
}

- (void)stopListening
{
    _state->controller->stopListening();
}

- (BOOL)whatsNewPending
{
    return !_state->controller->pendingWhatsNewVersion().isEmpty();
}

- (void)clearPendingWhatsNew
{
    _state->controller->clearPendingWhatsNew();
}

- (SpeecherUpdateState)updateState
{
    switch (_state->controller->updates()->state()) {
    case speecher::UpdateController::State::Idle:
        return SpeecherUpdateStateIdle;
    case speecher::UpdateController::State::Checking:
        return SpeecherUpdateStateChecking;
    case speecher::UpdateController::State::CheckFailed:
        return SpeecherUpdateStateCheckFailed;
    case speecher::UpdateController::State::UpToDate:
        return SpeecherUpdateStateUpToDate;
    case speecher::UpdateController::State::UpdateAvailable:
        return SpeecherUpdateStateUpdateAvailable;
    case speecher::UpdateController::State::Downloading:
        return SpeecherUpdateStateDownloading;
    case speecher::UpdateController::State::ReadyToRestart:
        return SpeecherUpdateStateReadyToRestart;
    case speecher::UpdateController::State::RestartPending:
        return SpeecherUpdateStateRestartPending;
    case speecher::UpdateController::State::Restarting:
        return SpeecherUpdateStateRestarting;
    case speecher::UpdateController::State::Error:
        return SpeecherUpdateStateError;
    }
}

- (NSString *)updateVersion
{
    return _state->controller->updates()->availableVersionDisplay().toNSString();
}

- (NSString *)installedVersion
{
    return _state->controller->updates()->currentVersion().toNSString();
}

- (NSInteger)updatePercent
{
    return _state->controller->updates()->downloadPercent();
}

- (NSString *)updateError
{
    return _state->controller->updates()->errorMessage().toNSString();
}

- (BOOL)updateBannerVisible
{
    return _state->controller->updates()->bannerVisible();
}

- (BOOL)updateStableReplacement
{
    return _state->controller->updates()->stableReplacementAvailable();
}

- (void)installUpdateAndRestart
{
    _state->controller->updates()->installAndRestart();
}

- (void)updateNow
{
    _state->controller->updates()->updateNow();
}

- (void)dismissUpdate
{
    _state->controller->updates()->dismissAvailableVersion();
}

- (BOOL)accessibilitySupported
{
    return _state->controller->accessibilitySupported();
}

- (BOOL)accessibilityEnabled
{
    return _state->controller->accessibilityEnabled();
}

- (NSString *)enableAccessibility
{
    QString error;
    if (_state->controller->enableAccessibility(&error)) {
        return nil;
    }
    return error.isEmpty() ? @"Accessibility settings could not be opened." : error.toNSString();
}

static NSArray<NSArray<NSString *> *> *
bridgedStats(const QList<speecher::ProviderDescriptor> &providers, NSString *providerId)
{
    const QString id = QString::fromNSString(providerId);
    NSMutableArray<NSArray<NSString *> *> *stats = [NSMutableArray array];
    for (const speecher::ProviderDescriptor &provider : providers) {
        if (provider.id != id) {
            continue;
        }
        for (const speecher::ProviderStat &stat : provider.stats) {
            [stats addObject:@[ stat.label.toNSString(), stat.value.toNSString() ]];
        }
    }
    return stats;
}

- (NSArray<NSArray<NSString *> *> *)statsForSpeechProvider:(NSString *)providerId
{
    return bridgedStats(_state->controller->providerRegistry()->speechProviders(), providerId);
}

- (NSArray<NSArray<NSString *> *> *)statsForRefinementProvider:(NSString *)providerId
{
    return bridgedStats(_state->controller->providerRegistry()->refinementProviders(), providerId);
}

static NSArray<SpeecherProviderModel *> *
bridgedProviders(const QList<speecher::ProviderDescriptor> &providers)
{
    NSMutableArray<SpeecherProviderModel *> *bridged = [NSMutableArray array];
    for (const speecher::ProviderDescriptor &provider : providers) {
        SpeecherProviderModel *model = [[SpeecherProviderModel alloc] init];
        model.providerId = provider.id.toNSString();
        model.label = provider.label.toNSString();
        model.credentialSource =
            speecher::credentialSourceLabel(provider.id, provider.label).toNSString();
        model.setupHint = provider.setupHint.toNSString();
        model.summary = provider.summary.toNSString();
        [bridged addObject:model];
    }
    return bridged;
}

- (NSArray<SpeecherProviderModel *> *)speechProviders
{
    return bridgedProviders(_state->controller->providerRegistry()->speechProviders());
}

- (NSArray<SpeecherProviderModel *> *)refinementProviders
{
    return bridgedProviders(_state->controller->providerRegistry()->refinementProviders());
}

// One speech provider's probe, superseded per provider: each probe claims the
// provider's slot with a fresh round number, and only the newest claim's
// verdict lands.
static void probeSpeechProvider(BridgeState *state,
                                const speecher::ProviderDescriptor &descriptor,
                                const speecher::SpeechSettings &speech,
                                void (^report)(NSString *providerId, BOOL ready, NSString *message))
{
    NSString *providerId = descriptor.id.toNSString();
    const auto answer = [report, providerId](bool ready, const QString &message) {
        report(providerId, ready, message.toNSString());
    };
    speecher::ProviderRegistry *registry = state->controller->providerRegistry();
    speecher::SpeechTranscriber *provider = registry->speechProvider(descriptor.id);
    if (!provider) {
        answer(false, QStringLiteral("No transcription service is available."));
        return;
    }
    std::optional<speecher::SpeechPrepareJob> job = provider->createPrepareJob(speech);
    if (!job || !job->run) {
        const speecher::SpeechPrepareResult result = provider->prepare(speech);
        answer(result.ok, result.message);
        return;
    }
    const quint64 generation = ++state->checkRound;
    state->speechProbeGeneration.insert(descriptor.id, generation);
    probeInBackground(state,
                      [state, id = descriptor.id, generation] {
                          return state->speechProbeGeneration.value(id) == generation;
                      },
                      std::move(*job), answer);
}

- (void)checkSpeechProviders:(void (^)(NSString *providerId, BOOL ready, NSString *message))report
{
    report = [report copy];
    BridgeState *state = _state;
    const speecher::SpeechSettings speech = _state->controller->settings()->snapshot().speech;
    for (const speecher::ProviderDescriptor &descriptor :
         _state->controller->providerRegistry()->speechProviders()) {
        probeSpeechProvider(state, descriptor, speech, report);
    }
}

// The single-provider re-probe behind a sign-in change: only the changed
// provider's verdict is stale, and a probe can be an OAuth refresh over the
// network, so the others are left alone — matching the Qt and WinUI
// assistants.
- (void)checkSpeechProviderNamed:(NSString *)providerId
                          report:(void (^)(NSString *providerId, BOOL ready, NSString *message))report
{
    report = [report copy];
    BridgeState *state = _state;
    const QString wanted = QString::fromNSString(providerId);
    const speecher::SpeechSettings speech = _state->controller->settings()->snapshot().speech;
    for (const speecher::ProviderDescriptor &descriptor :
         _state->controller->providerRegistry()->speechProviders()) {
        if (descriptor.id == wanted) {
            probeSpeechProvider(state, descriptor, speech, report);
            return;
        }
    }
}

- (void)checkRefinementProviders:(void (^)(NSString *providerId, BOOL ready, NSString *message))report
{
    report = [report copy];
    const quint64 generation = ++_state->refinementCheckGeneration;
    BridgeState *state = _state;
    speecher::ProviderRegistry *registry = _state->controller->providerRegistry();
    const speecher::RefinementSettings refinement =
        _state->controller->settings()->snapshot().refinement;
    for (const speecher::ProviderDescriptor &descriptor : registry->refinementProviders()) {
        NSString *providerId = descriptor.id.toNSString();
        const auto answer = [report, providerId](bool ready, const QString &message) {
            report(providerId, ready, message.toNSString());
        };
        speecher::TranscriptRefiner *refiner = registry->refinementProvider(descriptor.id);
        if (!refiner) {
            answer(false, QStringLiteral("Not available."));
            continue;
        }
        std::optional<speecher::RefinementRefreshJob> job = refiner->createRefreshJob(refinement);
        if (!job || !job->run) {
            const speecher::RefinementPrepareResult result = refiner->prepare(refinement);
            answer(result.ok, result.message);
            continue;
        }
        probeInBackground(state,
                          [state, generation] {
                              return generation == state->refinementCheckGeneration;
                          },
                          std::move(*job), answer);
    }
}

static speecher::ProviderSignIn &ensureSetupSignIn(BridgeState *state)
{
    if (!state->setupSignIn) {
        state->setupSignIn =
            std::make_unique<speecher::ProviderSignIn>(*state->controller->settings());
    }
    return *state->setupSignIn;
}

- (BOOL)setupCliproxyAccountsAvailable
{
    QStringList providerIds;
    for (const speecher::ProviderDescriptor &provider :
         _state->controller->providerRegistry()->speechProviders()) {
        providerIds.append(provider.id);
    }
    return ensureSetupSignIn(_state).anyUsableAccount(providerIds);
}

- (BOOL)setupSupportsCliproxyForProvider:(NSString *)providerId
{
    return speecher::ProviderSignIn::supportsCliproxy(QString::fromNSString(providerId));
}

- (BOOL)setupUsesCliproxyForProvider:(NSString *)providerId
{
    return ensureSetupSignIn(_state).usingCliproxy(QString::fromNSString(providerId));
}

- (void)setSetupUseCliproxy:(BOOL)use forProvider:(NSString *)providerId
{
    ensureSetupSignIn(_state).setUseCliproxy(QString::fromNSString(providerId), use);
}

- (NSArray<RowOptionModel *> *)setupCliproxyAccountOptionsForProvider:(NSString *)providerId
{
    const QString provider = QString::fromNSString(providerId);
    speecher::ProviderSignIn &signIn = ensureSetupSignIn(_state);
    NSMutableArray<RowOptionModel *> *bridged = [NSMutableArray array];
    for (const RowOption &option : speecher::cliproxyAccountOptions(
             speecher::ProviderSignIn::cliproxyAccountType(provider),
             signIn.cliproxyAccount(provider),
             signIn.resolvedAccountDirectory())) {
        RowOptionModel *model = [[RowOptionModel alloc] init];
        model.rowOptionId = option.id.toNSString();
        model.label = option.label.toNSString();
        model.help = option.help.toNSString();
        model.enabled = option.enabled;
        [bridged addObject:model];
    }
    return bridged;
}

- (NSString *)setupCliproxyAccountForProvider:(NSString *)providerId
{
    return ensureSetupSignIn(_state).cliproxyAccount(QString::fromNSString(providerId)).toNSString();
}

- (void)setSetupCliproxyAccount:(NSString *)account forProvider:(NSString *)providerId
{
    ensureSetupSignIn(_state).setCliproxyAccount(QString::fromNSString(providerId),
                                           QString::fromNSString(account));
}

- (NSString *)setupCliproxyOptInLabel
{
    return speecher::ProviderSignIn::cliproxyOptInLabel().toNSString();
}

- (NSString *)setupCliproxyFoundHint
{
    return speecher::ProviderSignIn::cliproxyAccountsFoundHint().toNSString();
}

- (NSString *)setupCliproxyMissingHint
{
    return speecher::ProviderSignIn::cliproxyAccountsMissingHint().toNSString();
}

- (NSString *)setupCliproxyDirectory
{
    return ensureSetupSignIn(_state).configuredAccountDirectory().toNSString();
}

- (NSString *)setupCliproxyDirectoryPlaceholder
{
    return ensureSetupSignIn(_state).resolvedAccountDirectory().toNSString();
}

- (void)setSetupCliproxyDirectory:(NSString *)directory
{
    ensureSetupSignIn(_state).setAccountDirectory(QString::fromNSString(directory).trimmed());
}

- (void)startMicrophoneMeterOnLevel:(void (^)(float level))onLevel
                            failure:(void (^)(NSString *message))onFailure
{
    [self stopMicrophoneMeter];
    self.setupMeterLevel = onLevel;
    self.setupMeterFailure = onFailure;
    _state->setupMeter = _state->controller->platform()->createAudioInput(
        _state->controller->settings(), &_state->lifetime);
    __weak SpeecherBridge *weakSelf = self;
    QObject::connect(_state->setupMeter,
                     &speecher::AudioInput::levelChanged,
                     &_state->lifetime,
                     [weakSelf](float level) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.setupMeterLevel) {
                             bridge.setupMeterLevel(level);
                         }
                     });
    QObject::connect(_state->setupMeter,
                     &speecher::AudioInput::failed,
                     &_state->lifetime,
                     [weakSelf](const QString &message) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.setupMeterFailure) {
                             bridge.setupMeterFailure(message.toNSString());
                         }
                     });
    QString error;
    if (!_state->setupMeter->start(&error) && self.setupMeterFailure) {
        self.setupMeterFailure(error.toNSString());
    }
}

- (void)stopMicrophoneMeter
{
    self.setupMeterLevel = nil;
    self.setupMeterFailure = nil;
    if (_state->setupMeter) {
        _state->setupMeter->stop();
        // deleteLater rather than delete: a stop can arrive from inside the
        // meter's own failure signal.
        _state->setupMeter->deleteLater();
        _state->setupMeter = nullptr;
    }
}

- (float)microphoneInputVolume
{
    const std::optional<float> volume = _state->controller->platform()->inputVolume();
    return volume ? *volume : -1.0f;
}

- (void)refreshAccessibilityState
{
    _state->controller->refreshAccessibilityState();
}

- (NSString *)requestAccessibilityGrant
{
    _state->controller->platform()->requestAccessibility();
    QString error;
    _state->controller->enableAccessibility(&error);
    return error.isEmpty() ? nil : error.toNSString();
}

- (void)completeSetup
{
    _state->controller->completeSetup();
}

- (void)relaunch
{
    _state->controller->platform()->relaunch();
}

- (BOOL)credentialIsEditable
{
    return [_settingsSchema draft].refinement.openAiAuthMode == kAppSettingsKeyAuthMode;
}

- (NSString *)credentialStatus
{
    const AppSettings &draft = [_settingsSchema draft];
    // The remote CLI Proxy fields decide which credential the status
    // describes; passing them matches the Qt call site (ProviderCustomRows).
    return speecher::OpenAiAuthProvider(_state->controller->secretStore(),
                                        draft.refinement.openAiAuthMode,
                                        draft.refinement.openAiCliproxyAccount,
                                        _state->controller->settings()->cliproxyOauthDir(),
                                        {},
                                        {},
                                        draft.refinement.cliproxyBaseUrl,
                                        draft.refinement.cliproxyApiKey)
        .status()
        .toNSString();
}

- (NSString *)anthropicCredentialStatus
{
    return speecher::mac::anthropicCredentialStatus(
               [_settingsSchema draft], *_state->controller->settings())
        .toNSString();
}

- (NSString *)readApiKey
{
    return _state->controller->secretStore()->apiKey().toNSString();
}

- (NSString *)saveApiKey:(NSString *)apiKey
{
    speecher::SecretStore *secrets = _state->controller->secretStore();
    if (secrets->saveApiKey(QString::fromNSString(apiKey).trimmed())) {
        return nil;
    }
    return secrets->status().toNSString();
}

- (NSArray<RowOptionModel *> *)cleanupStrengths
{
    return [_settingsSchema bridgedOptions:speecher::cleanupStrengths()];
}

- (NSArray<RowOptionModel *> *)writingTones
{
    return [_settingsSchema bridgedOptions:speecher::writingTones()];
}

- (NSArray<RowOptionModel *> *)writingProfiles
{
    using speecher::WritingProfile;
    QList<RowOption> profiles;
    for (WritingProfile profile : {WritingProfile::Work, WritingProfile::Email, WritingProfile::Personal,
                                   WritingProfile::AiCoding, WritingProfile::Other}) {
        profiles.append({speecher::writingProfileName(profile), speecher::writingProfileLabel(profile)});
    }
    return [_settingsSchema bridgedOptions:profiles];
}

- (SpeecherTranscribeOptions *)transcribeOptionsWithWritingProfile:(NSString *)profile
{
    const AppSettings settings = _state->controller->settings()->snapshot();
    const QString profileName = profile ? QString::fromNSString(profile)
                                        : settings.refinement.defaultWritingProfile;
    const speecher::WritingProfileSettings chosen = speecher::writingProfileSettingsFor(
        settings.refinement.writingProfiles, speecher::writingProfileFromName(profileName));
    SpeecherTranscribeOptions *options = [[SpeecherTranscribeOptions alloc] init];
    options.speechProviderId = settings.speech.providerId.toNSString();
    options.applyVocabulary = YES;
    options.refinementProviderId = settings.refinement.providerId.toNSString();
    options.cleanupStrength = chosen.cleanupStrength.toNSString();
    options.tone = chosen.tone.toNSString();
    options.writingProfile = speecher::writingProfileName(chosen.profile).toNSString();
    options.destination = SpeecherTranscriptDestinationBesideInput;
    options.folder = @"";
    return options;
}

- (NSString *)refinementModelForProvider:(NSString *)providerId
{
    const speecher::RefinementSettings refinement = _state->controller->settings()->snapshot().refinement;
    const QString provider = QString::fromNSString(providerId);
    if (provider == QStringLiteral("openai")) {
        return refinement.openAiModel.toNSString();
    }
    if (provider == QStringLiteral("anthropic")) {
        return refinement.anthropicModel.toNSString();
    }
    return @"";
}

- (NSArray<NSString *> *)audioFilesAmong:(NSArray<NSString *> *)paths
{
    NSMutableArray<NSString *> *audio = [NSMutableArray array];
    for (NSString *path in paths) {
        if (speecher::isAudioFile(QString::fromNSString(path))) {
            [audio addObject:path];
        }
    }
    return audio;
}

- (NSString *)startTranscribingFiles:(NSArray<NSString *> *)paths options:(SpeecherTranscribeOptions *)options
{
    QStringList files;
    for (NSString *path in paths) {
        files.append(QString::fromNSString(path));
    }
    speecher::TranscribeOptions core;
    core.speechProviderId = QString::fromNSString(options.speechProviderId);
    core.applyVocabulary = options.applyVocabulary;
    core.refinementProviderId = QString::fromNSString(options.refinementProviderId);
    core.cleanupStrength = QString::fromNSString(options.cleanupStrength);
    core.tone = QString::fromNSString(options.tone);
    core.writingProfile = QString::fromNSString(options.writingProfile);
    switch (options.destination) {
    case SpeecherTranscriptDestinationFolder:
        core.destination = speecher::TranscriptDestination::Folder;
        break;
    case SpeecherTranscriptDestinationNowhere:
        core.destination = speecher::TranscriptDestination::None;
        break;
    case SpeecherTranscriptDestinationBesideInput:
        core.destination = speecher::TranscriptDestination::BesideInput;
        break;
    }
    core.folder = QString::fromNSString(options.folder);
    QString error;
    if (_state->controller->startFileTranscription(files, core, &error)) {
        return nil;
    }
    return error.isEmpty() ? @"There are no audio files to transcribe." : error.toNSString();
}

- (void)cancelTranscription
{
    _state->controller->fileTranscription()->cancel();
}

- (NSString *)saveTranscript:(NSString *)text forAudioFile:(NSString *)audioPath inFolder:(NSString *)folder
{
    QString error;
    speecher::saveTranscript(QString::fromNSString(audioPath), QString::fromNSString(folder),
                             QString::fromNSString(text), &error);
    return error.isEmpty() ? nil : error.toNSString();
}

@end
