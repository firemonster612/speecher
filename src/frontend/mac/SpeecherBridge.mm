#include "frontend/mac/SpeecherBridge.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationSession.h"
#include "frontend/mac/MacCustomRows.h"
// The schema context: what this machine can offer the descriptors. Shared with
// the Qt front end rather than reassembled, because the device and provider
// lists are the same lists.
#include "frontend/qt/SchemaSettingsPage.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/ProviderRegistry.h"
#include "ui/Theme.h"

#include <QDebug>
#include <QKeySequence>
#include <QObject>
#include <QRegularExpression>

#import <AppKit/AppKit.h>

using speecher::AppSettings;
using speecher::ApplicationController;
using speecher::Capabilities;
using speecher::CollectionColumn;
using speecher::CollectionDescriptor;
using speecher::ColumnKind;
using speecher::RowKind;
using speecher::RowOption;
using speecher::SettingsRow;
using speecher::SettingsSchema;
using speecher::SettingsStore;

namespace {

const QString kWritingProfileGrid = QStringLiteral("writingProfileBehavior");
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
    Capabilities capabilities;
    // The one Custom row this front end draws as a table, kept here because its
    // records are the mac front end's shape rather than the schema's.
    CollectionDescriptor profileGrid = speecher::mac::writingProfileGrid();
    // Choices that cost a device enumeration stay out of a snapshot until the
    // front end has painted and asked for them.
    bool expensiveReady = false;
};

struct BridgeState {
    ApplicationController *controller = nullptr;
    // Owns the signal connections, so they end when the bridge does.
    QObject lifetime;
    // The transcript survives the dictation that produced it, so the menu bar
    // panel can still offer it once the panel that showed it has gone.
    QString lastTranscript;
};

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
- (void)setTargetAccessibility:(BOOL)available;
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
        _state->draft = store->snapshot();
        _state->capabilities = capabilities;
    }
    return self;
}

- (void)dealloc
{
    delete _state;
}

// The descriptor behind a row's table, which is the row's own for a Collection
// row and this front end's for the writing profile grid.
- (const CollectionDescriptor *)collectionForRow:(const SettingsRow &)row
{
    if (row.kind == RowKind::Collection) {
        return &row.collection;
    }
    if (row.id == kWritingProfileGrid) {
        return &_state->profileGrid;
    }
    return nullptr;
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
    model.help = row.help.toNSString();
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
    _state->store->applySnapshot(_state->draft);
    // What the Qt front end does after a save, and the reason a theme change
    // reaches NSApp.appearance as well as Qt's own palette.
    speecher::Theme::apply(_state->store->theme());
    _state->draft = _state->store->snapshot();
}

- (void)loadExpensiveRows
{
    _state->expensiveReady = YES;
}

- (void)setTargetAccessibility:(BOOL)available
{
    _state->capabilities.targetAccessibility = available;
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
    const Capabilities capabilities{controller->accessibilitySupported()
                                    && controller->accessibilityEnabled()};
    _settingsSchema = [[SettingsSchemaModel alloc]
        initWithStore:controller->settings()
               schema:speecher::buildSettingsSchema(
                          speecher::qtSchemaContext(*controller->platform(),
                                                    *controller->providerRegistry(),
                                                    controller->primaryOutputStatus()))
         capabilities:capabilities];
    __weak SpeecherBridge *weakSelf = self;
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
    [self connectPanelTo:controller->session()];
    return self;
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
                             qDebug() << "macOS popup preview delivered length=" << preview.size();
                             bridge.popupPreviewChanged(preview.toNSString());
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
    return _state->controller->globalShortcut().toString(QKeySequence::NativeText).toNSString();
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

- (BOOL)credentialIsEditable
{
    return [_settingsSchema draft].refinement.openAiAuthMode == kAppSettingsKeyAuthMode;
}

- (NSString *)credentialStatus
{
    const AppSettings &draft = [_settingsSchema draft];
    return speecher::OpenAiAuthProvider(_state->controller->secretStore(),
                                        draft.refinement.openAiAuthMode,
                                        draft.refinement.openAiCliproxyAccount,
                                        _state->controller->settings()->cliproxyOauthDir())
        .status()
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

@end
