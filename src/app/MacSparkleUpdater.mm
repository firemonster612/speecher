#include "app/MacSparkleUpdater.h"

#include "core/SettingsStore.h"

#include <QTimer>

#include <cstdlib>

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
    const char *overrideUrl = std::getenv("SPEECHER_APPCAST_URL");
    if (overrideUrl != nullptr && overrideUrl[0] != '\0') {
        return [NSString stringWithUTF8String:overrideUrl];
    }
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
    if (qEnvironmentVariableIsSet("SPEECHER_E2E_CHECK")) {
        QTimer::singleShot(1000, this, [this] {
            [m_native->controller.updater checkForUpdatesInBackground];
        });
    }
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
bool MacSparkleUpdater::bannerVisible() const { return false; }

void MacSparkleUpdater::checkForUpdates(UpdateChannel channel)
{
    [m_native->delegate setNightly:channel == UpdateChannel::Nightly];
    [m_native->controller checkForUpdates:nil];
}

void MacSparkleUpdater::updateNow() {}
void MacSparkleUpdater::dismissAvailableVersion() {}

void MacSparkleUpdater::applySettings()
{
    [m_native->delegate setNightly:m_settings->updateChannel() == UpdateChannel::Nightly];
    SPUUpdater *updater = m_native->controller.updater;
    updater.automaticallyChecksForUpdates = m_settings->autoCheckUpdates();
    updater.automaticallyDownloadsUpdates = m_settings->autoInstallUpdates();
}

} // namespace speecher
