#include "app/MacSparkleUpdater.h"

#include "core/SettingsStore.h"

#import <Sparkle/Sparkle.h>

namespace speecher {
namespace {

NSString *const stableFeedUrl = @"https://firemonster612.github.io/speecher/appcast-stable.xml";
NSString *const nightlyFeedUrl = @"https://firemonster612.github.io/speecher/appcast-nightly.xml";

} // namespace
} // namespace speecher

@interface SpeecherSparkleDelegate : NSObject <SPUUpdaterDelegate, SUVersionComparison>
- (void)setNightly:(BOOL)nightly allowStableReplacement:(BOOL)allowStableReplacement;
@end

@implementation SpeecherSparkleDelegate {
    BOOL _nightly;
    BOOL _allowStableReplacement;
    NSString *_runningVersion;
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        _runningVersion = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"];
    }
    return self;
}

- (void)setNightly:(BOOL)nightly allowStableReplacement:(BOOL)allowStableReplacement
{
    _nightly = nightly;
    _allowStableReplacement = allowStableReplacement;
}

- (SUAppcastItem *)bestValidUpdateInAppcast:(SUAppcast *)appcast
                                  forUpdater:(SPUUpdater *)
{
    return _allowStableReplacement ? appcast.items.firstObject : nil;
}

- (id<SUVersionComparison>)versionComparatorForUpdater:(SPUUpdater *)
{
    return self;
}

- (NSComparisonResult)compareVersion:(NSString *)versionA toVersion:(NSString *)versionB
{
    if (_allowStableReplacement && ![versionA isEqualToString:versionB]) {
        if ([versionB isEqualToString:_runningVersion]) {
            return NSOrderedDescending;
        }
        if ([versionA isEqualToString:_runningVersion]) {
            return NSOrderedAscending;
        }
    }
    return [[SUStandardVersionComparator defaultComparator]
        compareVersion:versionA
        toVersion:versionB];
}

- (void)updaterDidNotFindUpdate:(SPUUpdater *)
{
    _allowStableReplacement = NO;
}

- (void)updater:(SPUUpdater *)
        userDidMakeChoice:(SPUUserUpdateChoice)
        forUpdate:(SUAppcastItem *)
        state:(SPUUserUpdateState *)
{
    _allowStableReplacement = NO;
}

- (void)updater:(SPUUpdater *)didAbortWithError:(NSError *)
{
    _allowStableReplacement = NO;
}

- (NSString *)feedURLStringForUpdater:(SPUUpdater *)
{
    return _nightly ? speecher::nightlyFeedUrl : speecher::stableFeedUrl;
}

@end

namespace speecher {

struct MacSparkleUpdater::Native {
    __strong SpeecherSparkleDelegate *delegate = nil;
    __strong SPUStandardUpdaterController *controller = nil;
};

MacSparkleUpdater::MacSparkleUpdater(SettingsStore *settings, QObject *parent)
    : UpdateController(parent)
    , m_settings(settings)
    , m_native(std::make_unique<Native>())
{
    m_native->delegate = [[SpeecherSparkleDelegate alloc] init];
    m_native->controller = [[SPUStandardUpdaterController alloc]
        initWithStartingUpdater:NO
              updaterDelegate:m_native->delegate
            userDriverDelegate:nil];
    applySettings();
    connect(settings,
            &SettingsStore::updateSettingsChanged,
            this,
            &MacSparkleUpdater::applySettings);
}

MacSparkleUpdater::~MacSparkleUpdater() = default;

void MacSparkleUpdater::start()
{
    [m_native->controller startUpdater];
}

UpdateController::State MacSparkleUpdater::state() const
{
    return State::Idle;
}

QString MacSparkleUpdater::currentVersion() const
{
    return QStringLiteral(SPEECHER_VERSION);
}

QString MacSparkleUpdater::availableVersion() const { return {}; }
int MacSparkleUpdater::downloadPercent() const { return 0; }
QString MacSparkleUpdater::errorMessage() const { return {}; }
bool MacSparkleUpdater::isAppImage() const { return false; }
bool MacSparkleUpdater::supportsAutomaticDownloads() const { return true; }
bool MacSparkleUpdater::bannerVisible() const { return false; }
bool MacSparkleUpdater::repeatedAutomaticCheckFailure() const { return false; }
bool MacSparkleUpdater::manualInstallRequired() const { return false; }
bool MacSparkleUpdater::stableReplacementAvailable() const { return false; }

void MacSparkleUpdater::checkForUpdates(UpdateChannel channel)
{
    const bool nightly = channel == UpdateChannel::Nightly;
    const bool stableReplacement = !nightly
        && currentVersion().contains(QStringLiteral("-nightly"));
    [m_native->delegate setNightly:nightly allowStableReplacement:stableReplacement];
    [m_native->controller checkForUpdates:nil];
}

void MacSparkleUpdater::updateNow() {}
void MacSparkleUpdater::dismissAvailableVersion() {}

void MacSparkleUpdater::applySettings()
{
    [m_native->delegate
        setNightly:m_settings->updateChannel() == UpdateChannel::Nightly
        allowStableReplacement:NO];
    SPUUpdater *updater = m_native->controller.updater;
    updater.automaticallyChecksForUpdates = m_settings->autoCheckUpdates();
    updater.automaticallyDownloadsUpdates = m_settings->autoInstallUpdates();
}

} // namespace speecher
