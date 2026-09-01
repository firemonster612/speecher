#include "app/MacSparkleUpdater.h"

#include "core/SettingsStore.h"

#import <Sparkle/Sparkle.h>

namespace speecher {
namespace {

NSString *const stableFeedUrl = @"https://firemonster612.github.io/speecher/appcast-stable.xml";
NSString *const nightlyFeedUrl = @"https://firemonster612.github.io/speecher/appcast-nightly.xml";

} // namespace
} // namespace speecher

@interface SpeecherSparkleDelegate : NSObject <SPUUpdaterDelegate>
- (void)setNightly:(BOOL)nightly;
@end

@implementation SpeecherSparkleDelegate {
    BOOL _nightly;
}

- (void)setNightly:(BOOL)nightly
{
    _nightly = nightly;
}

- (NSString *)feedURLStringForUpdater:(SPUUpdater *)updater
{
    Q_UNUSED(updater);
    return _nightly ? speecher::nightlyFeedUrl : speecher::stableFeedUrl;
}

@end

namespace speecher {

struct MacSparkleUpdater::Native {
    __strong SpeecherSparkleDelegate *delegate = nil;
    __strong SPUStandardUpdaterController *controller = nil;
};

MacSparkleUpdater::MacSparkleUpdater(SettingsStore *settings, QObject *parent)
    : QObject(parent)
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
    [m_native->controller startUpdater];
}

MacSparkleUpdater::~MacSparkleUpdater() = default;

void MacSparkleUpdater::checkForUpdates()
{
    [m_native->controller checkForUpdates:nil];
}

void MacSparkleUpdater::applySettings()
{
    [m_native->delegate setNightly:m_settings->updateChannel() == UpdateChannel::Nightly];
    SPUUpdater *updater = m_native->controller.updater;
    updater.automaticallyChecksForUpdates = m_settings->autoCheckUpdates();
    updater.automaticallyDownloadsUpdates = m_settings->autoInstallUpdates();
}

} // namespace speecher
