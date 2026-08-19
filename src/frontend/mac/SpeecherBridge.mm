#include "frontend/mac/SpeecherBridge.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
// The schema context: what this machine can offer the descriptors. Shared with
// the Qt front end rather than reassembled, because the device and provider
// lists are the same lists.
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/Theme.h"

#include <QDebug>
#include <QObject>

using speecher::AppSettings;
using speecher::ApplicationController;
using speecher::Capabilities;
using speecher::RowKind;
using speecher::RowOption;
using speecher::SettingsRow;
using speecher::SettingsSchema;
using speecher::SettingsStore;

namespace {

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

id bridgedValue(const SettingsRow &row, const AppSettings &settings)
{
    if (!row.value) {
        return nil;
    }
    const QVariant value = row.value(settings);
    switch (row.kind) {
    case RowKind::Toggle:
        return @(value.toBool());
    case RowKind::Number:
        return @(value.toInt());
    default:
        return value.toString().toNSString();
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

struct SchemaState {
    SettingsStore *store = nullptr;
    SettingsSchema schema;
    AppSettings draft;
    Capabilities capabilities;
};

struct BridgeState {
    ApplicationController *controller = nullptr;
    // Owns the signal connections, so they end when the bridge does.
    QObject lifetime;
};

} // namespace

@interface RowOptionModel ()
@property (nonatomic, copy) NSString *rowOptionId;
@property (nonatomic, copy) NSString *label;
@property (nonatomic) BOOL enabled;
@end

@implementation RowOptionModel
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
@property (nonatomic) BOOL enabled;
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

// Declared so the bridge below can call it; the C++ types keep it out of the
// public header.
@interface SettingsSchemaModel (Cxx)
- (instancetype)initWithStore:(SettingsStore *)store
                       schema:(const SettingsSchema &)schema
                 capabilities:(const Capabilities &)capabilities;
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
        _state = new SchemaState{store, schema, store->snapshot(), capabilities};
    }
    return self;
}

- (void)dealloc
{
    delete _state;
}

- (NSArray<RowOptionModel *> *)optionsForRow:(const SettingsRow &)row
{
    // An expensive row's choices are the ones a front end fetches after it has
    // painted — a device enumeration or a keyring read — so a snapshot leaves
    // them out rather than paying for them on every read.
    if (row.expensive || !row.options) {
        return @[];
    }
    NSMutableArray<RowOptionModel *> *options = [NSMutableArray array];
    for (const RowOption &option : row.options(_state->draft)) {
        RowOptionModel *model = [[RowOptionModel alloc] init];
        model.rowOptionId = option.id.toNSString();
        model.label = option.label.toNSString();
        model.enabled = option.enabled;
        [options addObject:model];
    }
    return options;
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
                model.value = bridgedValue(row, _state->draft);
                model.options = [self optionsForRow:row];
                model.enabled = !row.enabled || row.enabled(_state->draft, _state->capabilities);
                [rows addObject:model];
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
    const QString id = QString::fromNSString(rowId);
    for (const speecher::SettingsPage &page : _state->schema.pages) {
        for (const speecher::SettingsSection &section : page.sections) {
            for (const SettingsRow &row : section.rows) {
                if (row.id != id || !row.apply) {
                    continue;
                }
                row.apply(_state->draft, coreValue(row, value));
                return;
            }
        }
    }
    qWarning() << "no settings row" << id << "to write";
}

- (void)commit
{
    _state->store->applySnapshot(_state->draft);
    // What the Qt front end does after a save, and the reason a theme change
    // reaches NSApp.appearance as well as Qt's own palette.
    speecher::Theme::apply(_state->store->theme());
    _state->draft = _state->store->snapshot();
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
    return self;
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

@end
