#include "frontend/mac/SpeecherBridge.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/AccessibilityPresentation.h"
#include "app/SetupSteps.h"
#include "app/PlatformComposition.h"
#include "app/UpdateBanner.h"
#include "app/UpdateController.h"
#include "core/InsightsExport.h"
#include "core/InsightsLog.h"
#include "core/InsightsSummary.h"
#include "core/SecretStore.h"
#include "core/ShortcutBinding.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationSession.h"
#include "dictation/DictationTypes.h"
#include "dictation/PopupPresentation.h"
#include "frontend/mac/MacCustomRows.h"
// The schema context: what this machine can offer the descriptors. Shared with
// the Qt front end rather than reassembled, because the device and provider
// lists are the same lists.
#include "frontend/qt/SchemaSettingsPage.h"
#include "providers/CustomEndpoints.h"
#include "providers/LocalModelStore.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderRegistry.h"
#include "providers/ProviderSignIn.h"
#include "providers/TranscriptRefinementPrompt.h"
#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribePresentation.h"
#include "ui/Theme.h"

#include <QDebug>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QKeySequence>
#include <QLocale>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>

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
using speecher::ModelRating;
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
    case PaneLayout::Transcribe:
        return SpeecherPaneLayoutTranscribe;
    case PaneLayout::Home:
        return SpeecherPaneLayoutHome;
    }
}

SpeecherModelRating bridgedModelRating(ModelRating rating)
{
    switch (rating) {
    case ModelRating::Recommended:
        return SpeecherModelRatingRecommended;
    case ModelRating::Good:
        return SpeecherModelRatingGood;
    case ModelRating::Situational:
        return SpeecherModelRatingSituational;
    case ModelRating::NotRecommended:
        return SpeecherModelRatingNotRecommended;
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
    // LocalSetup's changes, download and pull progress, at most one refresh
    // per tick: progress arrives per network read.
    QTimer localRefresh;
    bool localRowsChanged = false;
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

@interface SpeecherSetupStep ()
@property (nonatomic, readwrite, copy) NSString *stepId;
@property (nonatomic, readwrite, copy) NSString *title;
@property (nonatomic, readwrite, copy) NSString *intro;
@property (nonatomic, readwrite, copy) NSString *blocked;
@end

@implementation SpeecherSetupStep
@end

@implementation SpeecherTranscribeOptions
@end

@interface SpeecherTranscribeBatchLabels ()
@property (nonatomic) speecher::TranscribeBatchLabels labels;
@end

@implementation SpeecherTranscribeBatchLabels
@end

static speecher::TranscribeOptions coreTranscribeOptions(SpeecherTranscribeOptions *options)
{
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
    return core;
}

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

static speecher::TranscribeFileResult coreTranscriptResult(SpeecherTranscriptResult *result)
{
    speecher::TranscribeFileResult core;
    core.path = QString::fromNSString(result.path);
    core.raw = QString::fromNSString(result.raw);
    core.refined = QString::fromNSString(result.refined);
    core.savedPath = QString::fromNSString(result.savedPath);
    core.error = QString::fromNSString(result.error);
    return core;
}

static QList<speecher::TranscribeFileResult> coreTranscriptResults(NSArray<SpeecherTranscriptResult *> *results)
{
    QList<speecher::TranscribeFileResult> core;
    for (SpeecherTranscriptResult *result in results) {
        core.append(coreTranscriptResult(result));
    }
    return core;
}

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
@property (nonatomic) BOOL multiline;
@end

@implementation CollectionColumnModel
@end

@interface CollectionModel ()
@property (nonatomic, copy) NSArray<CollectionColumnModel *> *columns;
@property (nonatomic) NSInteger lockedRecordCount;
@property (nonatomic, copy) SpeecherRecord *blankRecord;
@property (nonatomic, copy) NSString *addLabel;
@property (nonatomic, copy) NSString *addDialogTitle;
@property (nonatomic, copy) NSString *deleteLabel;
@property (nonatomic, copy) NSString *emptyTitle;
@property (nonatomic, copy) NSString *emptyHelp;
@property (nonatomic, copy) NSString *importLabel;
@property (nonatomic, copy) NSString *importFailureTitle;
@property (nonatomic, copy) NSArray<NSString *> *importFileExtensions;
@property (nonatomic, copy) NSArray<RowOptionModel *> *actions;
@property (nonatomic) NSInteger minimumHeight;
@end

@implementation CollectionModel
@end

@interface SpeecherUpdateBanner ()
@property (nonatomic) BOOL visible;
@property (nonatomic) BOOL showInPopup;
@property (nonatomic) SpeecherBannerTone tone;
@property (nonatomic, copy) NSString *text;
@property (nonatomic) NSInteger progress;
@property (nonatomic, copy) NSString *action;
@property (nonatomic) BOOL actionEnabled;
@property (nonatomic, copy) NSString *later;
@property (nonatomic, copy) NSString *dismiss;
+ (SpeecherUpdateBanner *)bannerWithModel:(const speecher::UpdateBannerModel &)model;
@end

@implementation SpeecherUpdateBanner

+ (SpeecherUpdateBanner *)bannerWithModel:(const speecher::UpdateBannerModel &)model
{
    SpeecherUpdateBanner *banner = [[SpeecherUpdateBanner alloc] init];
    banner.visible = model.visible;
    banner.showInPopup = model.showInPopup;
    banner.tone = SpeecherBannerTone(model.tone);
    banner.text = model.text.toNSString();
    banner.progress = model.progress;
    banner.action = model.action.toNSString();
    banner.actionEnabled = model.actionEnabled;
    banner.later = model.later.toNSString();
    banner.dismiss = model.dismiss.toNSString();
    return banner;
}

+ (SpeecherUpdateBanner *)previewForState:(SpeecherUpdatePreviewState)state
{
    using State = speecher::UpdateController::State;
    speecher::UpdateBannerFacts facts;
    facts.version = QStringLiteral("0.2.0");
    facts.percent = 42;
    facts.bannerVisible = true;
    facts.error = QStringLiteral("The download stopped before it finished.");
    switch (state) {
    case SpeecherUpdatePreviewStateAvailable: facts.state = State::UpdateAvailable; break;
    case SpeecherUpdatePreviewStateDownloading: facts.state = State::Downloading; break;
    case SpeecherUpdatePreviewStateReadyToRestart:
        facts.state = State::ReadyToRestart;
        facts.error.clear();
        break;
    case SpeecherUpdatePreviewStateError: facts.state = State::Error; break;
    case SpeecherUpdatePreviewStateManualInstall:
        facts.state = State::Error;
        facts.manualInstallRequired = true;
        facts.error = QStringLiteral("Download the new version from the release page.");
        break;
    case SpeecherUpdatePreviewStateCheckFailed:
        facts.state = State::CheckFailed;
        facts.error = QStringLiteral("Could not check for updates.");
        break;
    }
    return [self bannerWithModel:speecher::updateBannerModel(facts)];
}

@end

@interface SpeecherWhatsNewBanner ()
@property (nonatomic, copy) NSString *text;
@property (nonatomic, copy) NSString *action;
@property (nonatomic, copy) NSString *dismiss;
+ (SpeecherWhatsNewBanner *)bannerWithModel:(const speecher::WhatsNewBannerModel &)model;
@end

@implementation SpeecherWhatsNewBanner

+ (SpeecherWhatsNewBanner *)bannerWithModel:(const speecher::WhatsNewBannerModel &)model
{
    SpeecherWhatsNewBanner *banner = [[SpeecherWhatsNewBanner alloc] init];
    banner.text = model.text.toNSString();
    banner.action = model.action.toNSString();
    banner.dismiss = model.dismiss.toNSString();
    return banner;
}

+ (SpeecherWhatsNewBanner *)previewForVersion:(NSString *)version
{
    return [self bannerWithModel:speecher::whatsNewBanner(QString::fromNSString(version))];
}

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
@property (nonatomic) BOOL suggests;
@property (nonatomic) BOOL enabled;
@property (nonatomic, copy) NSString *tooltip;
@property (nonatomic, copy) NSString *disabledHelp;
@property (nonatomic, copy) NSString *disabledAction;
@property (nonatomic, copy) NSString *disabledActionLabel;
@property (nonatomic, copy) NSString *groupId;
@property (nonatomic, strong, nullable) CollectionModel *collection;
@property (nonatomic) BOOL secret;
@property (nonatomic, copy) NSString *placeholder;
@property (nonatomic) BOOL multiline;
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
@property (nonatomic, copy) NSArray<SettingsSectionModel *> *sections;
@end

@implementation SettingsPageModel
@end

@interface SettingsPaneGroupModel ()
@property (nonatomic, copy) NSString *view;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *help;
@property (nonatomic, copy) NSArray<NSString *> *rows;
@end

@implementation SettingsPaneGroupModel
@end

@interface SettingsPaneModel ()
@property (nonatomic, copy) NSString *paneId;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *iconId;
@property (nonatomic) SpeecherPaneLayout layout;
@property (nonatomic, copy) NSArray<SettingsPaneGroupModel *> *groups;
@end

@implementation SettingsPaneModel
@end

@interface SidebarGroupModel ()
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSArray<NSString *> *panes;
@end

@implementation SidebarGroupModel
@end

@interface LocalModelInfo ()
@property (nonatomic, copy) NSString *modelId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *fileName;
@property (nonatomic, copy) NSString *sizeText;
@property (nonatomic) double librispeechWer;
@property (nonatomic) double fleursWer;
@property (nonatomic) BOOL streams;
@property (nonatomic, copy) NSString *licence;
@property (nonatomic) SpeecherModelRating rating;
@property (nonatomic, copy) NSString *ratingLabel;
@property (nonatomic, copy) NSString *bestFor;
@property (nonatomic, copy) NSArray<NSString *> *pros;
@property (nonatomic, copy) NSArray<NSString *> *cons;
@property (nonatomic, copy) NSString *fitLabel;
@property (nonatomic) BOOL tooLarge;
@property (nonatomic) BOOL suggested;
@property (nonatomic) BOOL speedTestRunning;
@property (nonatomic, copy) NSString *speedText;
@property (nonatomic, copy) NSString *speedDetail;
@property (nonatomic, copy) NSString *cardFacts;
@property (nonatomic, copy) NSArray<NSString *> *tableCells;
@property (nonatomic) BOOL downloaded;
@property (nonatomic) BOOL downloading;
@property (nonatomic) BOOL inUse;
@property (nonatomic, copy) NSString *problem;
@property (nonatomic) double downloadFraction;
@property (nonatomic, copy) NSString *progressText;
@end

@implementation LocalModelInfo
@end

@interface LocalRunnerInfo ()
@property (nonatomic, copy) NSString *runnerId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *version;
@property (nonatomic, copy) NSArray<NSString *> *models;
@end

@implementation LocalRunnerInfo
@end

@interface CleanupModelInfo ()
@property (nonatomic, copy) NSString *ollamaTag;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *sizeText;
@end

@implementation CleanupModelInfo
@end

@interface LocalRunnerChoice ()
@property (nonatomic, copy) NSString *runnerId;
@property (nonatomic, copy) NSString *runnerName;
@property (nonatomic, copy) NSString *model;
@property (nonatomic, strong, nullable) LocalRunnerInfo *available;
@property (nonatomic) BOOL offerPull;
@property (nonatomic) BOOL showSuggestion;
@end

@implementation LocalRunnerChoice
@end

@interface LocalModelFactNames ()
@property (nonatomic, copy) NSString *bestFor;
@property (nonatomic, copy) NSString *download;
@property (nonatomic, copy) NSString *speedHere;
@property (nonatomic, copy) NSString *wordErrorRate;
@property (nonatomic, copy) NSString *textShows;
@property (nonatomic, copy) NSString *language;
@property (nonatomic, copy) NSString *license;
@end

@implementation LocalModelFactNames
@end

@interface LocalSetupState ()
@property (nonatomic, copy) NSString *hardwareLine;
@property (nonatomic) BOOL hardwareKnown;
@property (nonatomic, copy) NSString *wordErrorRateSources;
@property (nonatomic, strong) LocalModelFactNames *factLabels;
@property (nonatomic, copy) NSArray<LocalModelInfo *> *models;
@property (nonatomic, copy) NSString *speechModelChoice;
@property (nonatomic, strong) LocalRunnerChoice *runnerChoice;
@property (nonatomic) BOOL detectingRunners;
@property (nonatomic, strong, nullable) CleanupModelInfo *cleanupModel;
@property (nonatomic, strong) CleanupPullInfo *pull;
@property (nonatomic, copy) NSString *endpointStatus;
@property (nonatomic, copy) NSArray<NSString *> *endpointModels;
@end

@implementation LocalSetupState
@end

@interface RefinementEndpointForm ()
@property (nonatomic, copy) NSString *format;
@property (nonatomic, copy) NSString *serverUrl;
@property (nonatomic, copy) NSString *apiKey;
@property (nonatomic, copy) NSString *model;
@end

@implementation RefinementEndpointForm
@end

@interface CleanupPullInfo ()
@property (nonatomic) BOOL running;
@property (nonatomic) double fraction;
@property (nonatomic, copy) NSString *progressText;
@property (nonatomic, copy) NSString *error;
@end

@implementation CleanupPullInfo
@end

@interface CollectionImportResult ()
@property (nonatomic, copy, nullable) NSArray<SpeecherRecord *> *records;
@property (nonatomic, copy) NSString *problem;
@end

@implementation CollectionImportResult
@end

@interface SpeecherInsightsDayModel ()
@property (nonatomic, copy) NSDate *date;
@property (nonatomic) NSInteger dictations;
@property (nonatomic) NSInteger words;
@property (nonatomic) NSInteger audioMs;
@property (nonatomic) NSInteger dictationsLevel;
@property (nonatomic) NSInteger wordsLevel;
@property (nonatomic) NSInteger audioLevel;
@property (nonatomic, copy) NSArray<NSString *> *tips;
@end

@implementation SpeecherInsightsDayModel
@end

@interface SpeecherInsightRecordModel ()
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *detail;
@property (nonatomic, copy) NSString *value;
@property (nonatomic) BOOL milestoneBar;
@end

@implementation SpeecherInsightRecordModel
@end

@interface SpeecherInsightsAppModel ()
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *profileLabel;
@property (nonatomic) NSInteger words;
@property (nonatomic) NSInteger percent;
@end

@implementation SpeecherInsightsAppModel
@end

@interface SpeecherInsightTileModel ()
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *iconId;
@property (nonatomic, copy) NSString *value;
@property (nonatomic, copy) NSString *unit;
@property (nonatomic, copy) NSArray<NSString *> *lines;
@property (nonatomic, copy) NSString *firstLineTip;
@property (nonatomic) BOOL showsWeek;
@end

@implementation SpeecherInsightTileModel
@end

@interface SpeecherInsightsModel ()
@property (nonatomic) NSInteger recordCount;
@property (nonatomic) NSInteger words;
@property (nonatomic) NSInteger dictations;
@property (nonatomic, copy) NSArray<NSNumber *> *weekActivity;
@property (nonatomic) NSInteger todayIndex;
@property (nonatomic, copy) NSArray<NSString *> *weekLetters;
@property (nonatomic, copy) NSArray<SpeecherInsightTileModel *> *tiles;
@property (nonatomic, copy) NSArray<NSString *> *heatmapRowLabels;
@property (nonatomic, copy) NSString *activeDaysLastYearText;
@property (nonatomic, copy) NSArray<NSString *> *hourTips;
@property (nonatomic, copy) NSString *personaText;
@property (nonatomic, copy) NSString *peakText;
@property (nonatomic, copy) NSString *shareText;
@property (nonatomic, copy) NSData *json;
@property (nonatomic, copy) NSString *jsonFileName;
@property (nonatomic, copy) NSArray<SpeecherInsightsDayModel *> *heatmap;
@property (nonatomic, copy) NSArray<NSString *> *weekMonthLabels;
@property (nonatomic, copy) NSArray<NSNumber *> *heatStrengths;
@property (nonatomic) NSInteger activeDaysLastYear;
@property (nonatomic, copy) NSArray<NSNumber *> *hourCounts;
@property (nonatomic, copy) NSArray<NSString *> *hourLabels;
@property (nonatomic) NSInteger peakHour;
@property (nonatomic) BOOL hasHourData;
@property (nonatomic) NSInteger wordsPerMinute;
@property (nonatomic) NSInteger typingWordsPerMinute;
@property (nonatomic, copy) NSString *minutesSavedText;
@property (nonatomic, copy) NSString *speedupText;
@property (nonatomic, copy) NSArray<SpeecherInsightsAppModel *> *apps;
@property (nonatomic) NSInteger allTimeWords;
@property (nonatomic) NSInteger nextMilestone;
@property (nonatomic, copy) NSArray<SpeecherInsightRecordModel *> *records;
@property (nonatomic, copy) NSArray<NSString *> *heatmapDescriptions;
@property (nonatomic, copy) NSString *hourChartDescription;
@property (nonatomic, copy) NSString *weekDescription;
@end

@implementation SpeecherInsightsModel
@end

namespace {

speecher::InsightsRange coreInsightsRange(SpeecherInsightsRange range)
{
    switch (range) {
    case SpeecherInsightsRangeLast7Days:
        return speecher::InsightsRange::Last7Days;
    case SpeecherInsightsRangeLast30Days:
        return speecher::InsightsRange::Last30Days;
    case SpeecherInsightsRangeThisYear:
        return speecher::InsightsRange::ThisYear;
    case SpeecherInsightsRangeAllTime:
        return speecher::InsightsRange::AllTime;
    }
    return speecher::InsightsRange::Last30Days;
}

NSDate *bridgedDate(const QDate &date)
{
    return date.startOfDay().toNSDate();
}

// The heatmap's measures in the order the bridge lists them.
constexpr std::array<speecher::HeatMeasure, 3> kHeatMeasures{
    speecher::HeatMeasure::Dictations, speecher::HeatMeasure::Words, speecher::HeatMeasure::Audio};

// Each measure's levels are worked out up front, so switching the measure
// only picks another column.
NSArray<SpeecherInsightsDayModel *> *bridgedHeatmap(const QList<speecher::HeatmapDay> &days)
{
    const speecher::HeatScale dictations(days, speecher::HeatMeasure::Dictations);
    const speecher::HeatScale words(days, speecher::HeatMeasure::Words);
    const speecher::HeatScale audio(days, speecher::HeatMeasure::Audio);
    NSMutableArray<SpeecherInsightsDayModel *> *bridged = [NSMutableArray array];
    for (const speecher::HeatmapDay &day : days) {
        SpeecherInsightsDayModel *model = [[SpeecherInsightsDayModel alloc] init];
        model.date = bridgedDate(day.date);
        model.dictations = day.dictations;
        model.words = day.words;
        model.audioMs = day.audioMs;
        model.dictationsLevel = dictations.level(day);
        model.wordsLevel = words.level(day);
        model.audioLevel = audio.level(day);
        NSMutableArray<NSString *> *tips = [NSMutableArray array];
        for (const speecher::HeatMeasure measure : kHeatMeasures) {
            const speecher::ChartTip tip = speecher::heatmapDayTip(day, measure);
            [tips addObject:(tip.title + u'\n' + tip.detail).toNSString()];
        }
        model.tips = tips;
        [bridged addObject:model];
    }
    return bridged;
}

NSArray<NSString *> *bridgedWeekMonthLabels(const QList<speecher::HeatmapDay> &days)
{
    const int weeks = int((days.size() + 6) / 7);
    const QMap<int, QString> labels = speecher::monthLabels(days, weeks);
    NSMutableArray<NSString *> *bridged = [NSMutableArray array];
    for (int week = 0; week < weeks; ++week) {
        [bridged addObject:labels.value(week).toNSString()];
    }
    return bridged;
}

SpeecherInsightsModel *bridgedInsights(const speecher::InsightsSummary &summary,
                                       speecher::InsightsRange range,
                                       qsizetype recordCount,
                                       const QDate &today)
{
    SpeecherInsightsModel *model = [[SpeecherInsightsModel alloc] init];
    NSMutableArray<SpeecherInsightTileModel *> *tiles = [NSMutableArray array];
    for (const speecher::InsightTileText &text : speecher::insightTiles(summary, today)) {
        SpeecherInsightTileModel *tile = [[SpeecherInsightTileModel alloc] init];
        tile.title = text.title.toNSString();
        tile.iconId = text.iconId.toNSString();
        tile.value = text.value.toNSString();
        tile.unit = text.unit.toNSString();
        tile.lines = bridgedStrings(text.lines);
        tile.firstLineTip = text.firstLineTip.toNSString();
        tile.showsWeek = text.showsWeek;
        [tiles addObject:tile];
    }
    model.tiles = tiles;
    const std::array<QString, 7> rows = speecher::heatmapRowLabels();
    model.heatmapRowLabels = bridgedStrings(QStringList(rows.begin(), rows.end()));
    model.activeDaysLastYearText =
        speecher::activeDaysLastYearText(summary.activeDaysLastYear).toNSString();
    model.personaText = speecher::personaText(summary).toNSString();
    model.peakText = speecher::peakText(summary).toNSString();
    model.shareText = speecher::insightsShareText(summary, range).toNSString();
    const QByteArray json = speecher::insightsJson(summary, range, today);
    model.json = [NSData dataWithBytes:json.constData() length:NSUInteger(json.size())];
    model.jsonFileName = speecher::insightsJsonFileName(today).toNSString();
    model.recordCount = recordCount;
    model.words = summary.words;
    model.dictations = summary.dictations;

    NSMutableArray<NSNumber *> *week = [NSMutableArray array];
    NSMutableArray<NSString *> *letters = [NSMutableArray array];
    for (int day = 0; day < 7; ++day) {
        [week addObject:@(summary.weekActivity[day])];
        [letters addObject:speecher::weekdayLetter(day + 1).toNSString()];
    }
    model.weekActivity = week;
    model.todayIndex = summary.todayIndex;
    model.weekLetters = letters;

    model.heatmap = bridgedHeatmap(summary.heatmap);
    model.weekMonthLabels = bridgedWeekMonthLabels(summary.heatmap);
    NSMutableArray<NSNumber *> *strengths = [NSMutableArray array];
    for (double strength : speecher::kHeatStrengths) {
        [strengths addObject:@(strength)];
    }
    model.heatStrengths = strengths;
    model.activeDaysLastYear = summary.activeDaysLastYear;

    NSMutableArray<NSNumber *> *hours = [NSMutableArray array];
    NSMutableArray<NSString *> *hourLabels = [NSMutableArray array];
    NSMutableArray<NSString *> *hourTips = [NSMutableArray array];
    for (int hour = 0; hour < 24; ++hour) {
        [hours addObject:@(summary.hourCounts[hour])];
        [hourLabels addObject:speecher::hourLabel(hour).toNSString()];
        const speecher::ChartTip tip = speecher::hourTip(hour, summary.hourCounts[hour]);
        [hourTips addObject:(tip.title + u'\n' + tip.detail).toNSString()];
    }
    model.hourCounts = hours;
    model.hourLabels = hourLabels;
    model.hourTips = hourTips;
    model.peakHour = summary.peakHour;
    model.hasHourData = summary.hasHourData;

    model.wordsPerMinute = summary.wordsPerMinute;
    model.typingWordsPerMinute = summary.typingWordsPerMinute;
    model.minutesSavedText = speecher::minutesText(summary.minutesSavedVersusTyping).toNSString();
    model.speedupText = summary.speedupText.toNSString();

    NSMutableArray<SpeecherInsightsAppModel *> *apps = [NSMutableArray array];
    for (const speecher::AppShare &share : summary.apps) {
        SpeecherInsightsAppModel *app = [[SpeecherInsightsAppModel alloc] init];
        app.name = share.name.toNSString();
        app.profileLabel = share.profileLabel.toNSString();
        app.words = share.words;
        app.percent = share.percent;
        [apps addObject:app];
    }
    model.apps = apps;

    model.allTimeWords = summary.allTimeWords;
    model.nextMilestone = summary.nextMilestone;
    NSMutableArray<SpeecherInsightRecordModel *> *records = [NSMutableArray array];
    for (const speecher::InsightRecordText &text : speecher::insightRecords(summary, today)) {
        SpeecherInsightRecordModel *record = [[SpeecherInsightRecordModel alloc] init];
        record.title = text.title.toNSString();
        record.detail = text.detail.toNSString();
        record.value = text.value.toNSString();
        record.milestoneBar = text.milestoneBar;
        [records addObject:record];
    }
    model.records = records;
    NSMutableArray<NSString *> *descriptions = [NSMutableArray array];
    for (const speecher::HeatMeasure measure : kHeatMeasures) {
        [descriptions addObject:speecher::heatmapDescription(summary, measure).toNSString()];
    }
    model.heatmapDescriptions = descriptions;
    model.hourChartDescription = speecher::hourChartDescription(summary).toNSString();
    model.weekDescription = speecher::weekDescription(summary).toNSString();
    return model;
}

} // namespace

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
    if (row.options) {
        return [self bridgedOptions:row.options(_state->draft)];
    }
    return row.kind == RowKind::Custom
        ? [self bridgedOptions:speecher::mac::customRowOptions(row.id, _state->draft, *_state->store)]
        : @[];
}

- (CollectionModel *)collectionModel:(const CollectionDescriptor &)collection
{
    NSMutableArray<CollectionColumnModel *> *columns = [NSMutableArray array];
    for (const CollectionColumn &column : collection.columns) {
        CollectionColumnModel *model = [[CollectionColumnModel alloc] init];
        model.columnId = column.id.toNSString();
        model.title = column.title.toNSString();
        model.kind = bridgedColumnKind(column.kind);
        model.options = column.options ? [self bridgedOptions:column.options(_state->draft)] : @[];
        model.stretch = column.stretch;
        model.multiline = column.multiline;
        [columns addObject:model];
    }
    CollectionModel *model = [[CollectionModel alloc] init];
    model.columns = columns;
    model.lockedRecordCount = collection.lockedRecordCount ? collection.lockedRecordCount() : 0;
    model.blankRecord = bridgedRecord(collection.blankRecord);
    model.addLabel = collection.addLabel.toNSString();
    model.addDialogTitle = collection.addDialogTitle.toNSString();
    model.deleteLabel = collection.deleteLabel.toNSString();
    model.emptyTitle = collection.emptyTitle.toNSString();
    model.emptyHelp = collection.emptyHelp.toNSString();
    model.importLabel = collection.supportsImport.actionLabel.toNSString();
    model.importFailureTitle = collection.supportsImport.failureTitle.toNSString();
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
    model.actionLabel = (row.actionLabelValue ? row.actionLabelValue(_state->draft)
                                              : row.actionLabel)
                            .toNSString();
    model.minimum = row.range.minimum;
    model.maximum = row.range.maximum;
    model.step = row.range.step;
    model.suffix = row.range.suffix.toNSString();
    model.options = [self optionsForRow:row];
    model.suggestions = row.suggestions ? [self bridgedOptions:row.suggestions(_state->draft)] : @[];
    model.suggests = bool(row.suggestions);
    model.enabled = !row.enabled || row.enabled(_state->draft, _state->capabilities);
    model.tooltip = row.tooltip.toNSString();
    model.disabledHelp = row.disabledHelp.toNSString();
    model.disabledAction = row.disabledAction.toNSString();
    model.disabledActionLabel = row.disabledActionLabel.toNSString();
    model.groupId = row.groupId.toNSString();
    model.secret = row.secret;
    model.placeholder = row.placeholder.toNSString();
    model.multiline = row.multiline;
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
            groupModel.view = group.view.toNSString();
            groupModel.title = group.title.toNSString();
            groupModel.help = group.help.toNSString();
            groupModel.rows = bridgedStrings(group.rows);
            [groups addObject:groupModel];
        }
        SettingsPaneModel *paneModel = [[SettingsPaneModel alloc] init];
        paneModel.paneId = pane.id.toNSString();
        paneModel.title = pane.title.toNSString();
        paneModel.iconId = pane.iconId.toNSString();
        paneModel.layout = bridgedPaneLayout(pane.layout);
        paneModel.groups = groups;
        [panes addObject:paneModel];
    }
    return panes;
}

- (NSArray<NSString *> *)resolvePage:(NSString *)pageId
{
    const speecher::PageId page = speecher::resolvePage(_state->schema, QString::fromNSString(pageId));
    return @[page.pane.toNSString(), page.view.toNSString()];
}

- (NSArray<NSString *> *)searchPanes:(NSString *)query
{
    return bridgedStrings(speecher::searchPanes(_state->schema, QString::fromNSString(query),
                                                _state->draft, _state->capabilities));
}

- (NSArray<SidebarGroupModel *> *)sidebarGroups
{
    NSMutableArray<SidebarGroupModel *> *groups = [NSMutableArray array];
    for (const speecher::SidebarGroup &group : _state->schema.sidebarGroups) {
        SidebarGroupModel *model = [[SidebarGroupModel alloc] init];
        model.title = group.title.toNSString();
        model.panes = bridgedStrings(group.panes);
        [groups addObject:model];
    }
    return groups;
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

- (NSString *)writingProfileDeletionNotice:(NSString *)profileId
{
    return speecher::writingProfileDeletionNotice(_state->draft, QString::fromNSString(profileId))
        .toNSString();
}

- (NSString *)writingProfileDeletionTitle
{
    return speecher::writingProfileDeletionTitle().toNSString();
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

- (NSArray<NSString *> *)badgesFor:(NSArray<SpeecherRecord *> *)records forRowId:(NSString *)rowId
{
    const SettingsRow *row = [self rowWithId:rowId];
    const CollectionDescriptor *collection = row ? [self collectionForRow:*row] : nullptr;
    if (!collection || !collection->badges) {
        return @[];
    }
    NSMutableArray<NSString *> *badges = [NSMutableArray array];
    for (const QString &badge : collection->badges(coreRecords(records), _state->draft)) {
        [badges addObject:badge.toNSString()];
    }
    return badges;
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
    speecher::SchemaContext context = speecher::qtSchemaContext(*controller->platform(),
                                                                *controller->providerRegistry(),
                                                                controller->pendingWhatsNewVersion());
    context.liveFacts = [setup = controller->localSetup()] { return setup->liveFacts(); };
    context.liveFactsForDraft = [setup = controller->localSetup()](const AppSettings &draft) {
        return setup->liveFacts(draft);
    };
    speecher::SettingsSchema schema = speecher::buildSettingsSchema(context);
    speecher::bindCheckForUpdatesRow(schema, controller->updateBanner());
    _settingsSchema = [[SettingsSchemaModel alloc]
        initWithStore:controller->settings()
               schema:schema
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
    QObject::connect(controller->updateBanner(),
                     &speecher::UpdateBanner::changed,
                     &_state->lifetime,
                     [weakSelf] {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.updateChanged) {
                             bridge.updateChanged();
                         }
                     });
    QObject::connect(controller->insightsLog(),
                     &speecher::InsightsLog::changed,
                     &_state->lifetime,
                     [weakSelf] {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.insightsChanged) {
                             bridge.insightsChanged();
                         }
                     });
    QObject::connect(controller,
                     &speecher::ApplicationController::lastRecordChanged,
                     &_state->lifetime,
                     [weakSelf] {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.lastRecordChanged) {
                             bridge.lastRecordChanged();
                         }
                     });
    QObject::connect(controller,
                     &speecher::ApplicationController::lastTranscriptChanged,
                     &_state->lifetime,
                     [weakSelf](const QString &text) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.transcriptChanged) {
                             bridge.transcriptChanged(text.toNSString());
                         }
                     });
    [self connectPanelTo:controller->session()];
    [self connectLocalSetup:controller->localSetup()];
    [self connectTranscriptionTo:controller->fileTranscription()];
    return self;
}

- (void)connectLocalSetup:(speecher::LocalSetup *)setup
{
    __weak SpeecherBridge *weakSelf = self;
    BridgeState *state = _state;
    QTimer *refresh = &_state->localRefresh;
    refresh->setSingleShot(true);
    refresh->setInterval(100);
    QObject::connect(refresh, &QTimer::timeout, &_state->lifetime, [weakSelf, state] {
        // A bridge can outlive its controller, and every read the refresh
        // prompts goes through it.
        if (!state->controller) {
            return;
        }
        const bool rowsChanged = std::exchange(state->localRowsChanged, false);
        SpeecherBridge *bridge = weakSelf;
        if (bridge.localSetupChanged) {
            bridge.localSetupChanged(rowsChanged);
        }
    });
    const auto schedule = [refresh] {
        if (!refresh->isActive()) {
            refresh->start();
        }
    };
    QObject::connect(setup, &speecher::LocalSetup::changed, &_state->lifetime, [state, schedule] {
        state->localRowsChanged = true;
        schedule();
    });
    QObject::connect(setup, &speecher::LocalSetup::pullProgress, &_state->lifetime, schedule);
    QObject::connect(&setup->models(), &speecher::LocalModelStore::downloadProgress,
                     &_state->lifetime, schedule);
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
                     [weakSelf, session](const QString &status) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupStatusChanged) {
                             bridge.popupStatusChanged(status.toNSString(),
                                                       static_cast<SpeecherDictationState>(session->state()));
                         }
                     });
    QObject::connect(session,
                     &DictationSession::popupMessageRequested,
                     &_state->lifetime,
                     [weakSelf](const QString &message, speecher::PopupOutcome outcome) {
                         SpeecherBridge *bridge = weakSelf;
                         if (bridge.popupMessageRequested) {
                             bridge.popupMessageRequested(message.toNSString(),
                                                          SpeecherPopupOutcome(outcome));
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
}

- (void)notePopupPresented:(uint64_t)generation
{
    _state->controller->session()->popupPresented(generation);
}

- (NSString *)lastTranscript
{
    return _state->controller->lastTranscript().toNSString();
}

- (NSInteger)lastTranscriptWords
{
    return speecher::countWords(_state->controller->lastTranscript());
}

- (NSString *)lastRecordApp
{
    const std::optional<speecher::DictationRecord> &record = _state->controller->lastRecord();
    return record ? record->appName.toNSString() : @"";
}

- (NSString *)lastRecordDay
{
    const std::optional<speecher::DictationRecord> &record = _state->controller->lastRecord();
    return record ? speecher::relativeDay(record->finishedAt.date(),
                                          _state->controller->insightsToday())
                        .toNSString()
                  : @"";
}

- (BOOL)insightsEnabled
{
    return _state->controller->settings()->insightsEnabled();
}

- (SpeecherInsightsModel *)insightsSummaryForRange:(SpeecherInsightsRange)range
{
    const QList<speecher::DictationRecord> &records = _state->controller->insightsLog()->records();
    const QDate today = _state->controller->insightsToday();
    return bridgedInsights(speecher::summarize(records, coreInsightsRange(range), today,
                                               _state->controller->settings()->writingProfileSettings()),
                           coreInsightsRange(range),
                           records.size(),
                           today);
}

- (BOOL)clearInsights
{
    return _state->controller->clearInsights();
}

- (NSInteger)learnedCorrectionCount
{
    return _state->controller->settings()->learnedCorrections().size();
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

- (NSString *)statusLabel
{
    return _state->controller->statusLabel().toNSString();
}

- (NSString *)toggleLabel
{
    return speecher::dictationToggleAction(_state->controller->stateName()).label.toNSString();
}

- (BOOL)toggleEnabled
{
    return speecher::dictationToggleAction(_state->controller->stateName()).enabled;
}

- (BOOL)listening
{
    return speecher::dictationListeningPresentation(_state->controller->stateName());
}

- (NSString *)accessibilityNoticeText:(BOOL)compact
{
    return speecher::accessibilityNoticeText(false, compact).toNSString();
}

- (NSString *)trayToolTip:(BOOL)listening
{
    return speecher::trayToolTip(listening).toNSString();
}

- (NSString *)traySettingsCaption
{
    return speecher::traySettingsCaption().toNSString();
}

- (NSString *)trayQuitCaption
{
    return speecher::trayQuitCaption().toNSString();
}

- (NSString *)inputLevelLabel
{
    return speecher::inputLevelLabel().toNSString();
}

- (NSString *)copyTranscriptCaption
{
    return speecher::copyTranscriptCaption().toNSString();
}

- (NSString *)copiedCaption
{
    return speecher::copiedCaption().toNSString();
}

+ (NSTimeInterval)copiedFeedbackSeconds
{
    return speecher::kCopiedFeedbackMs / 1000.0;
}

- (NSString *)failureNote
{
    return speecher::dictationFailureNote(_state->controller->stateName(),
                                          _state->controller->session()->lastFailure())
        .toNSString();
}

- (NSDictionary<NSString *, NSString *> *)homeLabels
{
    const speecher::InsightsShareLabels share = speecher::insightsShareLabels();
    return @{
        @"share": share.share.toNSString(),
        @"copyText": share.copyText.toNSString(),
        @"saveJson": share.saveJson.toNSString(),
        @"copied": share.copied.toNSString(),
        @"saved": share.saved.toNSString(),
        @"saveFailed": share.saveFailed.toNSString(),
        @"saveTitle": share.saveTitle.toNSString(),
        @"correctionsTitle": speecher::learnedCorrectionsTitle().toNSString(),
        @"legendLess": speecher::heatLegendLessText().toNSString(),
        @"legendMore": speecher::heatLegendMoreText().toNSString(),
        @"insightsOffTitle": speecher::homeText(speecher::HomeText::InsightsOffTitle).toNSString(),
        @"insightsOffBody": speecher::homeText(speecher::HomeText::InsightsOffBody).toNSString(),
        @"noInsightsTitle": speecher::homeText(speecher::HomeText::NoInsightsTitle).toNSString(),
        @"noInsightsBody": speecher::homeText(speecher::HomeText::NoInsightsBody).toNSString(),
        @"insightsSettings": speecher::homeText(speecher::HomeText::InsightsSettings).toNSString(),
        @"yourDictation": speecher::homeText(speecher::HomeText::YourDictation).toNSString(),
        @"period": speecher::homeText(speecher::HomeText::Period).toNSString(),
        @"activity": speecher::homeText(speecher::HomeText::Activity).toNSString(),
        @"measure": speecher::homeText(speecher::HomeText::Measure).toNSString(),
        @"whenYouTalk": speecher::homeText(speecher::HomeText::WhenYouTalk).toNSString(),
        @"noHourData": speecher::homeText(speecher::HomeText::NoHourData).toNSString(),
        @"pace": speecher::homeText(speecher::HomeText::Pace).toNSString(),
        @"speakingPace": speecher::homeText(speecher::HomeText::SpeakingPace).toNSString(),
        @"savedOverTyping": speecher::homeText(speecher::HomeText::SavedOverTyping).toNSString(),
        @"youSpeaking": speecher::homeText(speecher::HomeText::YouSpeaking).toNSString(),
        @"typicalTyping": speecher::homeText(speecher::HomeText::TypicalTyping).toNSString(),
        @"whereYourWordsGo": speecher::homeText(speecher::HomeText::WhereYourWordsGo).toNSString(),
        @"noDictationInPeriod": speecher::homeText(speecher::HomeText::NoDictationInPeriod).toNSString(),
        @"records": speecher::homeText(speecher::HomeText::Records).toNSString(),
        @"privacyNote": speecher::homeText(speecher::HomeText::PrivacyNote).toNSString(),
        @"clearHistoryQuestion": speecher::homeText(speecher::HomeText::ClearHistoryQuestion).toNSString(),
        @"clearHistoryBody": speecher::homeText(speecher::HomeText::ClearHistoryBody).toNSString(),
        @"clearHistoryConfirm": speecher::homeText(speecher::HomeText::ClearHistoryConfirm).toNSString(),
        @"clearHistoryFailed": speecher::homeText(speecher::HomeText::ClearHistoryFailed).toNSString(),
        @"measureDictations": speecher::heatMeasureLabel(speecher::HeatMeasure::Dictations).toNSString(),
        @"measureWords": speecher::heatMeasureLabel(speecher::HeatMeasure::Words).toNSString(),
        @"measureAudio": speecher::heatMeasureLabel(speecher::HeatMeasure::Audio).toNSString(),
    };
}

- (NSString *)learnedCorrectionsNote
{
    const ApplicationController &controller = *_state->controller;
    const bool accessibility = !controller.accessibilitySupported() || controller.accessibilityEnabled();
    return speecher::learnedCorrectionsNote(int(controller.settings()->learnedCorrections().size()),
                                            controller.settings()->snapshot().correctionLearningEnabled,
                                            accessibility)
        .toNSString();
}

- (NSString *)learnedCorrectionsAction
{
    const ApplicationController &controller = *_state->controller;
    return speecher::learnedCorrectionsAction(int(controller.settings()->learnedCorrections().size()),
                                              controller.settings()->snapshot().correctionLearningEnabled)
        .toNSString();
}

- (NSString *)insightsRangeLabel:(SpeecherInsightsRange)range
{
    return speecher::insightsRangeLabel(coreInsightsRange(range)).toNSString();
}

- (NSString *)dictationShortcutHint:(NSString *)shortcut
{
    return speecher::dictationShortcutHint(QString::fromNSString(shortcut)).toNSString();
}

- (NSString *)learnedCorrectionsCaption:(NSInteger)count
{
    return speecher::learnedCorrectionsCaption(int(count)).toNSString();
}

- (NSString *)noTranscriptYetText
{
    return speecher::noTranscriptYetText().toNSString();
}

- (void)quit
{
    _state->controller->quitApplication();
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

- (SpeecherUpdateBanner *)updateBanner
{
    return [SpeecherUpdateBanner bannerWithModel:_state->controller->updateBanner()->model()];
}

- (SpeecherWhatsNewBanner *)whatsNewBanner
{
    return [SpeecherWhatsNewBanner
        bannerWithModel:speecher::whatsNewBanner(_state->controller->updates()->currentVersion())];
}

- (NSString *)installedVersion
{
    return _state->controller->updates()->currentVersion().toNSString();
}

- (void)runUpdateAction
{
    _state->controller->updateBanner()->runAction();
}

- (void)deferUpdate
{
    _state->controller->updateBanner()->later();
}

- (void)dismissUpdate
{
    _state->controller->updateBanner()->dismiss();
}

+ (NSString *)checkingCredentialsStatus
{
    return speecher::checkingCredentialsStatus().toNSString();
}

+ (NSString *)accessibilityGrantActionLabel
{
    return speecher::accessibilityGrantActionLabel().toNSString();
}

+ (CGFloat)popupErrorWrapWidth
{
    return speecher::kPopupErrorWrapWidth;
}

+ (NSString *)popupDismissCaption
{
    return speecher::popupDismissCaption().toNSString();
}

+ (NSString *)renewingSignInText
{
    return speecher::renewingSignInText().toNSString();
}

+ (NSString *)statusLabelFor:(SpeecherDictationState)state
{
    // SpeecherDictationState mirrors speecher::DictationState value for value.
    static_assert(int(SpeecherDictationStateIdle) == int(speecher::DictationState::Idle));
    static_assert(int(SpeecherDictationStateStarting) == int(speecher::DictationState::Starting));
    static_assert(int(SpeecherDictationStateListening) == int(speecher::DictationState::Listening));
    static_assert(int(SpeecherDictationStateStopping) == int(speecher::DictationState::Stopping));
    static_assert(int(SpeecherDictationStateRefining) == int(speecher::DictationState::Refining));
    static_assert(int(SpeecherDictationStateDelivering) == int(speecher::DictationState::Delivering));
    static_assert(int(SpeecherDictationStateError) == int(speecher::DictationState::Error));
    return speecher::dictationStatusLabel(
               speecher::dictationStateName(static_cast<speecher::DictationState>(state)))
        .toNSString();
}

+ (NSTimeInterval)popupErrorDismissSecondsFor:(NSString *)message
{
    return speecher::popupErrorDismissMs(QString::fromNSString(message)) / 1000.0;
}

+ (NSString *)trimPreview:(NSString *)preview toWidth:(CGFloat)width font:(NSFont *)font
{
    NSDictionary *attributes = @{NSFontAttributeName: font};
    return speecher::trimPreviewToFit(QString::fromNSString(preview), [&](const QString &text) {
               return [text.toNSString() sizeWithAttributes:attributes].width <= width;
           })
        .toNSString();
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
        answer(false, speecher::setupTranscriptionBlocked(false, QString()));
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

- (BOOL)ownModelRefinementReady
{
    const speecher::RefinementSettings refinement = _state->controller->settings()->snapshot().refinement;
    speecher::TranscriptRefiner *refiner =
        _state->controller->providerRegistry()->refinementProvider(refinement.providerId);
    return refiner && refiner->prepare(refinement).ok;
}

- (NSString *)setupSpeechChoiceForSaved:(NSString *)saved
                         readyProviders:(NSArray<NSString *> *)readyProviders
                           localOffered:(BOOL)localOffered
                      proxyAccountFound:(BOOL)proxyAccountFound
{
    QStringList ready;
    for (NSString *id in readyProviders) ready.append(QString::fromNSString(id));
    return speecher::setupSpeechChoice(QString::fromNSString(saved), ready, localOffered, proxyAccountFound,
                                       false).toNSString();
}

- (nullable NSString *)setupRefinementChoiceForSaved:(NSString *)saved
                                      readyProviders:(NSArray<NSString *> *)readyProviders
{
    const speecher::LocalSetup &setup = *_state->controller->localSetup();
    if (setup.detectingRunners()) return nil;
    QStringList ready;
    for (NSString *id in readyProviders) ready.append(QString::fromNSString(id));
    return speecher::setupRefinementChoice(QString::fromNSString(saved), ready,
                                           setup.runnerChoice().available.has_value(),
                                           _state->controller->settings()->refinementProviderChosen()).toNSString();
}

- (BOOL)offersSetupSpeechProvider:(NSString *)providerId saved:(NSString *)saved localAvailable:(BOOL)localAvailable
{
    return speecher::offersSetupSpeechProvider(QString::fromNSString(providerId), QString::fromNSString(saved), localAvailable);
}

- (NSArray<SpeecherSetupStep *> *)setupSteps
{
    NSMutableArray<SpeecherSetupStep *> *steps = [NSMutableArray array];
    for (const speecher::SetupStepInfo &info : speecher::setupSteps()) {
        SpeecherSetupStep *step = [SpeecherSetupStep new];
        step.stepId = info.id.toNSString();
        step.title = info.title.toNSString();
        step.intro = info.intro.toNSString();
        step.blocked = info.blocked.toNSString();
        [steps addObject:step];
    }
    return steps;
}

- (NSString *)setupWindowTitle { return speecher::setupWindowTitle().toNSString(); }
- (NSString *)audioDeviceDefaultLabel { return speecher::audioDeviceDefaultLabel().toNSString(); }
- (NSString *)setupSilentMicrophoneHint { return speecher::setupSilentMicrophoneHint().toNSString(); }
- (NSString *)setupBlockedHeading { return speecher::setupBlockedHeading().toNSString(); }
- (NSString *)setupBlockedFooter { return speecher::setupBlockedFooter().toNSString(); }

- (NSString *)setupStepCounter:(NSInteger)step of:(NSInteger)total
{
    return speecher::setupStepCounter(int(step), int(total)).toNSString();
}

- (NSString *)setupTranscriptionBlocked:(BOOL)localSelected provider:(NSString *)providerLabel
{
    return speecher::setupTranscriptionBlocked(localSelected, QString::fromNSString(providerLabel)).toNSString();
}

- (NSString *)setupMicrophoneBlocked:(BOOL)accessGranted
{
    return speecher::setupMicrophoneBlocked(accessGranted ? speecher::SetupMicrophoneProblem::Silent
                                                          : speecher::SetupMicrophoneProblem::NoAccess)
        .toNSString();
}

- (NSString *)setupReadyIntro:(BOOL)blocked downloading:(BOOL)downloading
{
    return speecher::setupReadyIntro(blocked, downloading).toNSString();
}

- (NSString *)setupActivationInstruction:(NSString *)shortcut
{
    return speecher::setupActivationInstruction(_state->controller->settings()->shortcutActivationMode(),
                                                QString::fromNSString(shortcut))
        .toNSString();
}

- (NSString *)setupProviderVerdict:(NSString *)providerId ready:(BOOL)ready
{
    return speecher::setupProviderVerdict(QString::fromNSString(providerId), ready).toNSString();
}

- (NSString *)setupRefinementStatus:(NSString *)providerId ready:(NSNumber *)ready
{
    return speecher::setupRefinementStatus(QString::fromNSString(providerId),
                                           ready ? std::optional<bool>(ready.boolValue) : std::nullopt)
        .toNSString();
}

- (NSString *)setupChecklistLine:(NSString *)stepId choice:(NSString *)choice
{
    return speecher::setupChecklistLine(QString::fromNSString(stepId), QString::fromNSString(choice)).toNSString();
}

- (NSString *)setupText:(SpeecherSetupText)text
{
    // SpeecherSetupText mirrors speecher::SetupText value for value.
    static_assert(int(SpeecherSetupTextSkipSetup) == int(speecher::SetupText::SkipSetup));
    static_assert(int(SpeecherSetupTextCheckAgain) == int(speecher::SetupText::CheckAgain));
    static_assert(int(SpeecherSetupTextGoToStep) == int(speecher::SetupText::GoToStep));
    static_assert(int(SpeecherSetupTextHowToDictate) == int(speecher::SetupText::HowToDictate));
    static_assert(int(SpeecherSetupTextTranscriptionService) == int(speecher::SetupText::TranscriptionService));
    static_assert(int(SpeecherSetupTextLocalSpeechNote) == int(speecher::SetupText::LocalSpeechNote));
    static_assert(int(SpeecherSetupTextDownloadToContinue) == int(speecher::SetupText::DownloadToContinue));
    static_assert(int(SpeecherSetupTextDownloadContinues) == int(speecher::SetupText::DownloadContinues));
    static_assert(int(SpeecherSetupTextCloseWhileDownloading) == int(speecher::SetupText::CloseWhileDownloading));
    static_assert(int(SpeecherSetupTextCliproxyAccount) == int(speecher::SetupText::CliproxyAccount));
    static_assert(int(SpeecherSetupTextListeningForInput) == int(speecher::SetupText::ListeningForInput));
    static_assert(int(SpeecherSetupTextInputDetected) == int(speecher::SetupText::InputDetected));
    static_assert(int(SpeecherSetupTextUsesYourSignIn) == int(speecher::SetupText::UsesYourSignIn));
    static_assert(int(SpeecherSetupTextYourOwnModels) == int(speecher::SetupText::YourOwnModels));
    static_assert(int(SpeecherSetupTextCleanupProvider) == int(speecher::SetupText::CleanupProvider));
    static_assert(int(SpeecherSetupTextSkipCleanup) == int(speecher::SetupText::SkipCleanup));
    static_assert(int(SpeecherSetupTextLookingForRunners) == int(speecher::SetupText::LookingForRunners));
    static_assert(int(SpeecherSetupTextNoRunnerFound) == int(speecher::SetupText::NoRunnerFound));
    static_assert(int(SpeecherSetupTextNoRunner) == int(speecher::SetupText::NoRunner));
    static_assert(int(SpeecherSetupTextRawUntilRunner) == int(speecher::SetupText::RawUntilRunner));
    static_assert(int(SpeecherSetupTextInstallRunner) == int(speecher::SetupText::InstallRunner));
    static_assert(int(SpeecherSetupTextGetOllama) == int(speecher::SetupText::GetOllama));
    static_assert(int(SpeecherSetupTextDownloadWithOllama) == int(speecher::SetupText::DownloadWithOllama));
    static_assert(int(SpeecherSetupTextEndpointModelHint) == int(speecher::SetupText::EndpointModelHint));
    return speecher::setupText(static_cast<speecher::SetupText>(text)).toNSString();
}

- (NSString *)setupRowLabel:(NSString *)rowId
{
    return speecher::setupSchemaRow(QString::fromNSString(rowId)).label.toNSString();
}

- (NSArray<RowOptionModel *> *)setupRowOptions:(NSString *)rowId
{
    const speecher::SettingsRow &row = speecher::setupSchemaRow(QString::fromNSString(rowId));
    NSMutableArray<RowOptionModel *> *bridged = [NSMutableArray array];
    if (!row.options) return bridged;
    for (const RowOption &option : row.options(AppSettings())) {
        RowOptionModel *model = [[RowOptionModel alloc] init];
        model.rowOptionId = option.id.toNSString();
        model.label = option.label.toNSString();
        model.help = option.help.toNSString();
        model.enabled = option.enabled;
        [bridged addObject:model];
    }
    return bridged;
}

- (NSString *)setupProviderReady:(NSString *)providerLabel
{
    return speecher::setupProviderReady(QString::fromNSString(providerLabel)).toNSString();
}

- (NSString *)setupRefinementNotSignedIn:(NSString *)providerLabel
{
    return speecher::setupRefinementNotSignedIn(QString::fromNSString(providerLabel)).toNSString();
}

- (NSString *)setupLocalSpeechChoice:(NSString *)modelName
{
    return speecher::setupLocalSpeechChoice(QString::fromNSString(modelName)).toNSString();
}

- (NSString *)setupCliproxySpeechChoice:(NSString *)providerLabel
{
    return speecher::setupCliproxySpeechChoice(QString::fromNSString(providerLabel)).toNSString();
}

- (NSString *)setupPasteVerdict:(BOOL)pastes
{
    return speecher::setupPasteVerdict(pastes).toNSString();
}

- (NSString *)ownModelRefinementSummary
{
    return speecher::ownModelRefinementSummary(_state->controller->settings()->snapshot().refinement).toNSString();
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

- (BOOL)localSpeechAvailable
{
    return _state->controller->providerRegistry()->speechProvider(QStringLiteral("local")) != nullptr;
}

- (void)probeLocalHardware
{
    _state->controller->localSetup()->probeHardware();
}

static QString progressText(qint64 done, qint64 total)
{
    return QStringLiteral("%1 of %2").arg(speecher::downloadSizeText(done), speecher::downloadSizeText(total));
}

static LocalModelInfo *bridgedLocalModel(const speecher::LocalSetup &setup, const speecher::LocalModel &model)
{
    using namespace speecher;
    LocalModelInfo *info = [[LocalModelInfo alloc] init];
    info.modelId = model.id.toNSString();
    info.name = model.name.toNSString();
    info.fileName = model.fileName.toNSString();
    info.sizeText = downloadSizeText(model.sizeBytes).toNSString();
    info.librispeechWer = model.librispeechCleanWer;
    info.fleursWer = model.fleursEnglishWer;
    info.streams = model.streams;
    info.licence = model.licence.toNSString();
    info.rating = bridgedModelRating(model.rating);
    info.ratingLabel = modelRatingLabel(model.rating).toNSString();
    info.bestFor = model.bestFor.toNSString();
    info.pros = bridgedStrings(model.pros);
    info.cons = bridgedStrings(model.cons);
    info.fitLabel = setup.fitLabel(model).toNSString();
    info.speedTestRunning = setup.speedTestRunning(model.id);

    const LocalSetup::ModelState state = setup.modelState(model);
    info.tooLarge = state.tooLarge;
    info.suggested = state.suggested;
    info.speedText = state.speedText.toNSString();
    info.speedDetail = state.speedDetail.toNSString();
    info.cardFacts = state.cardFacts.toNSString();
    info.tableCells = bridgedStrings(state.tableCells);
    info.downloaded = state.downloaded;
    info.downloading = state.downloading;
    info.inUse = state.inUse;
    info.problem = state.problem.toNSString();
    const auto progress = setup.downloadProgress(model.id);
    info.downloadFraction = progress && progress->second > 0 ? double(progress->first) / double(progress->second) : 0;
    info.progressText = progress ? progressText(progress->first, model.sizeBytes).toNSString() : @"";
    return info;
}

static LocalRunnerChoice *bridgedRunnerChoice(const speecher::RunnerChoice &resolved)
{
    LocalRunnerChoice *choice = [[LocalRunnerChoice alloc] init];
    choice.runnerId = resolved.selection.runner.toNSString();
    choice.runnerName = speecher::localRunnerName(resolved.selection.runner).toNSString();
    choice.model = resolved.selection.model.toNSString();
    choice.offerPull = resolved.offerPull;
    choice.showSuggestion = resolved.showSuggestion;
    if (const std::optional<speecher::DetectedRunner> &runner = resolved.available) {
        LocalRunnerInfo *info = [[LocalRunnerInfo alloc] init];
        info.runnerId = runner->id.toNSString();
        info.name = runner->name.toNSString();
        info.version = runner->version.toNSString();
        info.models = bridgedStrings(runner->models);
        choice.available = info;
    }
    return choice;
}

- (LocalSetupState *)localSetupState
{
    using namespace speecher;
    const LocalSetup &setup = *_state->controller->localSetup();
    LocalSetupState *state = [[LocalSetupState alloc] init];
    state.hardwareLine = setup.hardwareLine().toNSString();
    state.wordErrorRateSources = speecher::wordErrorRateSources().toNSString();
    const LocalModelFactLabels names;
    LocalModelFactNames *factNames = [LocalModelFactNames new];
    factNames.bestFor = names.bestFor.toNSString();
    factNames.download = names.download.toNSString();
    factNames.speedHere = names.speedHere.toNSString();
    factNames.wordErrorRate = names.wordErrorRate.toNSString();
    factNames.textShows = names.textShows.toNSString();
    factNames.language = names.language.toNSString();
    factNames.license = names.license.toNSString();
    state.factLabels = factNames;
    state.hardwareKnown = setup.hardwareKnown();
    NSMutableArray<LocalModelInfo *> *models = [NSMutableArray array];
    for (const LocalModel &model : localModelCatalog()) {
        [models addObject:bridgedLocalModel(setup, model)];
    }
    state.models = models;
    state.speechModelChoice = setup.speechModelChoice().id.toNSString();
    state.runnerChoice = bridgedRunnerChoice(setup.runnerChoice());
    state.detectingRunners = setup.detectingRunners();
    if (const std::optional<CleanupModel> model = setup.suggestedCleanupModel()) {
        CleanupModelInfo *info = [[CleanupModelInfo alloc] init];
        info.ollamaTag = model->ollamaTag.toNSString();
        info.name = model->name.toNSString();
        info.sizeText = downloadSizeText(model->sizeBytes).toNSString();
        state.cleanupModel = info;
    }
    const LocalSetup::Pull pull = setup.pull();
    CleanupPullInfo *pullInfo = [[CleanupPullInfo alloc] init];
    pullInfo.running = pull.running;
    pullInfo.fraction = pull.totalBytes > 0 ? double(pull.completedBytes) / double(pull.totalBytes) : 0;
    pullInfo.progressText = progressText(pull.completedBytes, pull.totalBytes).toNSString();
    pullInfo.error = pull.error.toNSString();
    state.pull = pullInfo;
    const LiveFacts facts = setup.liveFacts();
    state.endpointStatus = facts.refinementEndpointStatus.toNSString();
    state.endpointModels = bridgedStrings(facts.refinementEndpointModels);
    return state;
}

- (void)initializeSpeechModel
{
    _state->controller->localSetup()->initializeSpeechModel();
}

- (void)chooseSpeechModel:(NSString *)modelId
{
    _state->controller->localSetup()->chooseSpeechModel(QString::fromNSString(modelId));
}

- (void)downloadLocalModel:(NSString *)modelId
{
    if (const speecher::LocalModel *model = speecher::findLocalModel(QString::fromNSString(modelId))) {
        _state->controller->localSetup()->download(*model);
    }
}

- (void)cancelLocalModelDownload:(NSString *)modelId
{
    _state->controller->localSetup()->cancelDownload(QString::fromNSString(modelId));
}

- (void)deleteLocalModel:(NSString *)modelId
{
    if (const speecher::LocalModel *model = speecher::findLocalModel(QString::fromNSString(modelId))) {
        _state->controller->localSetup()->removeModel(*model);
    }
}

- (void)testLocalModelSpeed:(NSString *)modelId
{
    _state->controller->localSetup()->runSpeedTest(QString::fromNSString(modelId));
}

- (void)detectLocalRunners
{
    _state->controller->localSetup()->detectRunners();
}

- (void)pullCleanupModel:(NSString *)ollamaTag
{
    _state->controller->localSetup()->pullCleanupModel(QString::fromNSString(ollamaTag));
}

- (RefinementEndpointForm *)refinementEndpointForm
{
    const speecher::RefinementSettings refinement = _state->controller->settings()->snapshot().refinement;
    const speecher::RefinementEndpoint shown = speecher::resolvedRefinementEndpoint(refinement);
    RefinementEndpointForm *form = [[RefinementEndpointForm alloc] init];
    form.format = refinement.endpoint.format.toNSString();
    form.serverUrl = shown.apiBase.toNSString();
    form.apiKey = shown.apiKey.toNSString();
    form.model = refinement.endpoint.model.toNSString();
    return form;
}

static std::optional<QString> optionalString(NSString *value)
{
    return value ? std::optional<QString>(QString::fromNSString(value)) : std::nullopt;
}

- (void)editRefinementEndpointFormat:(NSString *)format
                           serverUrl:(NSString *)serverUrl
                              apiKey:(NSString *)apiKey
                               model:(NSString *)model
{
    SettingsStore *store = _state->controller->settings();
    AppSettings settings = store->snapshot();
    speecher::editRefinementEndpoint(settings, {optionalString(format), optionalString(serverUrl),
                                                optionalString(apiKey), optionalString(model)});
    store->applySnapshot(settings);
}

- (void)checkRefinementEndpoint
{
    _state->controller->localSetup()->checkRefinementEndpoint(
        _state->controller->settings()->snapshot().refinement);
}


- (NSString *)builtInSystemPrompt
{
    return speecher::builtInDictationSystemPrompt().toNSString();
}

- (NSArray<RowOptionModel *> *)cleanupStrengths
{
    return [_settingsSchema bridgedOptions:speecher::cleanupStrengths(
                                _state->controller->settings()->customCleanupLevels())];
}

- (NSArray<RowOptionModel *> *)writingTones
{
    return [_settingsSchema bridgedOptions:speecher::writingTones(
                                _state->controller->settings()->customTones())];
}

- (NSArray<RowOptionModel *> *)writingProfiles
{
    return [_settingsSchema bridgedOptions:speecher::writingProfileChoices(
                                _state->controller->settings()->writingProfileSettings())];
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
    options.writingProfile = chosen.profile.toNSString();
    options.destination = SpeecherTranscriptDestinationBesideInput;
    options.folder = @"";
    return options;
}

- (NSString *)refinementModelForProvider:(NSString *)providerId
{
    return speecher::refinementModel(QString::fromNSString(providerId),
                                     _state->controller->settings()->snapshot().refinement)
        .toNSString();
}

- (NSArray<NSString *> *)transcribableExtensions
{
    return bridgedStrings(speecher::transcribableExtensions());
}

- (NSString *)mediaFilesHint
{
    return speecher::mediaFilesHint().toNSString();
}

- (NSString *)mediaFilesTooltip
{
    return speecher::mediaFilesTooltip().toNSString();
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
    QString error;
    if (_state->controller->startFileTranscription(files, coreTranscribeOptions(options), &error)) {
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

- (NSString *)transcribePhaseLabel:(SpeecherTranscribePhase)phase
{
    return speecher::transcribePhaseLabel(static_cast<speecher::TranscribePhase>(phase)).toNSString();
}

- (NSString *)transcribeStepLabel:(SpeecherTranscribeStep)step
{
    return speecher::transcribeStepLabel(static_cast<speecher::TranscribeStep>(step)).toNSString();
}

- (NSString *)transcribeStepHint:(SpeecherTranscribeStep)step
{
    return speecher::transcribeStepHint(static_cast<speecher::TranscribeStep>(step)).toNSString();
}

- (NSString *)transcribeText:(SpeecherTranscribeText)text
{
    // SpeecherTranscribeText mirrors speecher::TranscribeText value for value.
    static_assert(int(SpeecherTranscribeTextWindowTitle) == int(speecher::TranscribeText::WindowTitle));
    static_assert(int(SpeecherTranscribeTextAudioFilesSection) == int(speecher::TranscribeText::AudioFilesSection));
    static_assert(int(SpeecherTranscribeTextTranscriptionSection) == int(speecher::TranscribeText::TranscriptionSection));
    static_assert(int(SpeecherTranscribeTextRefinementSection) == int(speecher::TranscribeText::RefinementSection));
    static_assert(int(SpeecherTranscribeTextOutputSection) == int(speecher::TranscribeText::OutputSection));
    static_assert(int(SpeecherTranscribeTextFilesDialogTitle) == int(speecher::TranscribeText::FilesDialogTitle));
    static_assert(int(SpeecherTranscribeTextRemoveFile) == int(speecher::TranscribeText::RemoveFile));
    static_assert(int(SpeecherTranscribeTextService) == int(speecher::TranscribeText::Service));
    static_assert(int(SpeecherTranscribeTextVocabulary) == int(speecher::TranscribeText::Vocabulary));
    static_assert(int(SpeecherTranscribeTextVocabularyHelp) == int(speecher::TranscribeText::VocabularyHelp));
    static_assert(int(SpeecherTranscribeTextRefiner) == int(speecher::TranscribeText::Refiner));
    static_assert(int(SpeecherTranscribeTextRefinerHelp) == int(speecher::TranscribeText::RefinerHelp));
    static_assert(int(SpeecherTranscribeTextNoRefiner) == int(speecher::TranscribeText::NoRefiner));
    static_assert(int(SpeecherTranscribeTextRefinerModel) == int(speecher::TranscribeText::RefinerModel));
    static_assert(int(SpeecherTranscribeTextCleanup) == int(speecher::TranscribeText::Cleanup));
    static_assert(int(SpeecherTranscribeTextCleanupHelp) == int(speecher::TranscribeText::CleanupHelp));
    static_assert(int(SpeecherTranscribeTextWritingProfile) == int(speecher::TranscribeText::WritingProfile));
    static_assert(int(SpeecherTranscribeTextWritingProfileHelp) == int(speecher::TranscribeText::WritingProfileHelp));
    static_assert(int(SpeecherTranscribeTextTone) == int(speecher::TranscribeText::Tone));
    static_assert(int(SpeecherTranscribeTextToneHelp) == int(speecher::TranscribeText::ToneHelp));
    static_assert(int(SpeecherTranscribeTextSaveTranscripts) == int(speecher::TranscribeText::SaveTranscripts));
    static_assert(int(SpeecherTranscribeTextFolder) == int(speecher::TranscribeText::Folder));
    static_assert(int(SpeecherTranscribeTextChangeFolder) == int(speecher::TranscribeText::ChangeFolder));
    static_assert(int(SpeecherTranscribeTextFolderDialogTitle) == int(speecher::TranscribeText::FolderDialogTitle));
    static_assert(int(SpeecherTranscribeTextCancel) == int(speecher::TranscribeText::Cancel));
    static_assert(int(SpeecherTranscribeTextPartialName) == int(speecher::TranscribeText::PartialName));
    static_assert(int(SpeecherTranscribeTextPartialPlaceholder) == int(speecher::TranscribeText::PartialPlaceholder));
    static_assert(int(SpeecherTranscribeTextRefined) == int(speecher::TranscribeText::Refined));
    static_assert(int(SpeecherTranscribeTextRaw) == int(speecher::TranscribeText::Raw));
    static_assert(int(SpeecherTranscribeTextCopyAll) == int(speecher::TranscribeText::CopyAll));
    static_assert(int(SpeecherTranscribeTextCopy) == int(speecher::TranscribeText::Copy));
    static_assert(int(SpeecherTranscribeTextCopied) == int(speecher::TranscribeText::Copied));
    static_assert(int(SpeecherTranscribeTextExportAll) == int(speecher::TranscribeText::ExportAll));
    static_assert(int(SpeecherTranscribeTextExport) == int(speecher::TranscribeText::Export));
    static_assert(int(SpeecherTranscribeTextExportAllDialogTitle) == int(speecher::TranscribeText::ExportAllDialogTitle));
    static_assert(int(SpeecherTranscribeTextExportDialogTitle) == int(speecher::TranscribeText::ExportDialogTitle));
    static_assert(int(SpeecherTranscribeTextTextFiles) == int(speecher::TranscribeText::TextFiles));
    static_assert(int(SpeecherTranscribeTextSaved) == int(speecher::TranscribeText::Saved));
    static_assert(int(SpeecherTranscribeTextRetry) == int(speecher::TranscribeText::Retry));
    static_assert(int(SpeecherTranscribeTextRetrying) == int(speecher::TranscribeText::Retrying));
    static_assert(int(SpeecherTranscribeTextTranscribeMore) == int(speecher::TranscribeText::TranscribeMore));
    static_assert(int(SpeecherTranscribeTextProgressName) == int(speecher::TranscribeText::ProgressName));
    static_assert(int(SpeecherTranscribeTextNoFilesYet) == int(speecher::TranscribeText::NoFilesYet));
    static_assert(int(SpeecherTranscribeTextNeedsRefiner) == int(speecher::TranscribeText::NeedsRefiner));
    static_assert(int(SpeecherTranscribeTextDropToAdd) == int(speecher::TranscribeText::DropToAdd));
    return speecher::transcribeText(static_cast<speecher::TranscribeText>(text)).toNSString();
}

- (NSString *)chooseFilesCaption:(BOOL)anyListed
{
    return speecher::chooseFilesCaption(anyListed).toNSString();
}

- (NSString *)transcribeStartCaption:(NSInteger)fileCount
{
    return speecher::startCaption(int(fileCount)).toNSString();
}

- (NSString *)transcribeResultsTitle:(NSInteger)resultCount
{
    return speecher::resultsTitle(int(resultCount)).toNSString();
}

- (NSString *)destinationLabel:(SpeecherTranscriptDestination)destination
{
    return speecher::destinationLabel(static_cast<speecher::TranscriptDestination>(destination)).toNSString();
}

- (NSString *)destinationHint:(SpeecherTranscriptDestination)destination
{
    return speecher::destinationHint(static_cast<speecher::TranscriptDestination>(destination)).toNSString();
}

- (NSString *)transcriptSaveErrorForPath:(NSString *)path reason:(NSString *)reason
{
    return speecher::transcriptSaveError(QString::fromNSString(path), QString::fromNSString(reason)).toNSString();
}

- (NSString *)percentLabel:(double)progress
{
    return speecher::percentLabel(progress).toNSString();
}

- (NSString *)percentSpoken:(double)progress
{
    return speecher::percentSpoken(progress).toNSString();
}

- (NSString *)paneTitleForRowId:(NSString *)rowId
{
    return speecher::paneTitleForRow(QString::fromNSString(rowId)).toNSString();
}

- (NSString *)transcribeRefinementModelHint
{
    return speecher::refinementModelHint().toNSString();
}

- (double)transcribeLandingSeconds
{
    return speecher::kTranscribeLandingMs / 1000.0;
}

- (double)overallFileProgress:(double)fractionSent
                        phase:(SpeecherTranscribePhase)phase
                      refines:(BOOL)refines
                    msInPhase:(int64_t)msInPhase
{
    return speecher::overallFileProgress(fractionSent, static_cast<speecher::TranscribePhase>(phase),
                                         refines, msInPhase);
}

- (NSString *)durationLabel:(int64_t)durationMs
{
    return speecher::durationLabel(durationMs).toNSString();
}

- (NSString *)audioFileDetailWithBytes:(int64_t)bytes durationMs:(int64_t)durationMs
{
    return speecher::audioFileDetail(bytes, durationMs).toNSString();
}

- (BOOL)refinesTranscripts:(SpeecherTranscribeOptions *)options
{
    return speecher::refinesTranscripts(coreTranscribeOptions(options));
}

- (NSString *)shownTranscript:(SpeecherTranscriptResult *)result raw:(BOOL)raw
{
    return speecher::shownTranscript(coreTranscriptResult(result), raw).toNSString();
}

- (NSString *)resultMeta:(SpeecherTranscriptResult *)result durationMs:(int64_t)durationMs raw:(BOOL)raw
{
    return speecher::resultMeta(coreTranscriptResult(result), durationMs, raw).toNSString();
}

- (NSString *)allTranscripts:(NSArray<SpeecherTranscriptResult *> *)results raw:(BOOL)raw
{
    return speecher::allTranscripts(coreTranscriptResults(results), raw).toNSString();
}

- (NSString *)processingTitleForBatch:(NSArray<NSString *> *)batch current:(NSInteger)current
{
    QStringList paths;
    for (NSString *path in batch) {
        paths.append(QString::fromNSString(path));
    }
    return speecher::processingTitle(paths, int(current)).toNSString();
}

- (SpeecherTranscribeQueueState)queueStateAt:(NSInteger)index
                                     current:(NSInteger)current
                                    finished:(NSArray<SpeecherTranscriptResult *> *)finished
{
    return static_cast<SpeecherTranscribeQueueState>(
        speecher::queueState(int(index), int(current), coreTranscriptResults(finished)));
}

- (NSString *)queueStateLabel:(SpeecherTranscribeQueueState)state phase:(NSString *)phase
{
    return speecher::queueStateLabel(static_cast<speecher::TranscribeQueueState>(state),
                                     QString::fromNSString(phase))
        .toNSString();
}

- (SpeecherTranscribeBatchLabels *)batchLabelsForOptions:(SpeecherTranscribeOptions *)options
{
    SpeecherTranscribeBatchLabels *labels = [[SpeecherTranscribeBatchLabels alloc] init];
    labels.labels = speecher::batchLabels(coreTranscribeOptions(options),
                                            *_state->controller->providerRegistry(),
                                            _state->controller->settings()->snapshot().refinement);
    return labels;
}

- (NSString *)batchSummaryForResults:(NSArray<SpeecherTranscriptResult *> *)results
                           batchSize:(NSInteger)batchSize
                           cancelled:(BOOL)cancelled
                           durations:(NSDictionary<NSString *, NSNumber *> *)durationsMs
                             options:(SpeecherTranscribeOptions *)options
                              labels:(SpeecherTranscribeBatchLabels *)labels
{
    QHash<QString, qint64> durations;
    for (NSString *path in durationsMs) {
        durations.insert(QString::fromNSString(path), durationsMs[path].longLongValue);
    }
    return speecher::batchSummary(coreTranscriptResults(results), int(batchSize), cancelled, durations,
                                  coreTranscribeOptions(options), labels.labels)
        .toNSString();
}

@end
