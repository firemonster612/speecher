#include "app/UpdateBanner.h"

#include "core/AppSettings.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationSession.h"

namespace speecher {

namespace {

using State = UpdateController::State;

bool dictating(const DictationSession *session)
{
    const DictationState state = session->state();
    return state != DictationState::Idle && state != DictationState::Error;
}

// What retrying does from a failed state: reopen the release page when only a
// manual install can succeed, otherwise check again.
QString retryCaption(const UpdateBannerFacts &facts)
{
    return facts.manualInstallRequired ? QStringLiteral("Open release page")
                                       : QStringLiteral("Try again");
}

// A restart asked for mid-dictation waits for the session to end, and the
// caption says so.
QString restartCaption(const UpdateBannerFacts &facts)
{
    return facts.dictating ? QStringLiteral("Restart after this dictation")
                           : QStringLiteral("Restart now");
}

QString failureText(const UpdateBannerFacts &facts)
{
    return facts.error.isEmpty() ? QStringLiteral("Update check failed") : facts.error;
}

} // namespace

UpdateBannerModel updateBannerModel(const UpdateBannerFacts &facts)
{
    UpdateBannerModel model;
    const bool failedCheckWorthShowing = facts.state == State::CheckFailed
        && facts.repeatedCheckFailure;
    const bool deferredRestart = facts.deferred
        && (facts.state == State::ReadyToRestart || facts.state == State::RestartPending);
    model.visible = (facts.bannerVisible || failedCheckWorthShowing) && !deferredRestart;
    if (!model.visible) {
        return model;
    }
    model.showInPopup = facts.state != State::Checking && facts.state != State::UpToDate;
    const QString dismiss = QStringLiteral("Dismiss");
    switch (facts.state) {
    case State::Idle:
        model.visible = false;
        break;
    case State::Checking:
        model.text = QStringLiteral("Checking for updates…");
        break;
    case State::UpToDate:
        model.tone = UpdateBannerModel::Tone::Positive;
        model.text = QStringLiteral("Speecher is up to date");
        model.dismiss = dismiss;
        break;
    case State::UpdateAvailable:
        model.text = facts.stableReplacement
            ? QStringLiteral("Switch to Stable Release %1 (replaces this Nightly Build)")
                  .arg(facts.version)
            : QStringLiteral("Speecher %1 is available").arg(facts.version);
        // Where Speecher cannot install for itself, the click opens the
        // release page, and the button says so.
        model.action = facts.automaticDownloads ? QStringLiteral("Install and restart")
                                                : QStringLiteral("Open release page");
        // Installing mid-dictation is safe: the restart waits for the session
        // to end. A browser opening would take focus from the Target.
        model.actionEnabled = facts.automaticDownloads || !facts.dictating;
        model.dismiss = dismiss;
        break;
    case State::Downloading:
        model.text = QStringLiteral("Downloading Speecher %1 (%2%)")
                         .arg(facts.version)
                         .arg(facts.percent);
        model.progress = facts.percent;
        break;
    case State::ReadyToRestart:
        model.text = facts.error.isEmpty() ? QStringLiteral("Restart to finish updating")
                                           : facts.error;
        model.action = restartCaption(facts);
        model.later = QStringLiteral("Later");
        break;
    case State::RestartPending:
        model.text = QStringLiteral("Restarting after this dictation…");
        break;
    case State::Restarting:
        model.text = QStringLiteral("Restarting…");
        break;
    case State::CheckFailed:
    case State::Error:
        model.tone = UpdateBannerModel::Tone::Error;
        model.text = failureText(facts);
        model.action = retryCaption(facts);
        // A retry or a browser window would pull focus away mid-dictation.
        model.actionEnabled = !facts.dictating;
        model.dismiss = dismiss;
        break;
    }
    return model;
}

UpdateCheckRow updateCheckRow(const UpdateBannerFacts &facts, UpdateChannel channel)
{
    const QString feed = channel == UpdateChannel::Nightly ? QStringLiteral("Nightly Build")
                                                           : QStringLiteral("Stable Release");
    switch (facts.state) {
    case State::Idle:
        return {QStringLiteral("Check now"),
                QStringLiteral("Check the %1 feed for a newer build.").arg(feed)};
    case State::Checking:
        return {QStringLiteral("Checking…"), QStringLiteral("Checking the %1 feed.").arg(feed),
                false};
    case State::UpToDate:
        return {QStringLiteral("Check again"), QStringLiteral("Speecher is up to date.")};
    case State::UpdateAvailable:
        return {facts.automaticDownloads ? QStringLiteral("Update now")
                                         : QStringLiteral("Open release page"),
                QStringLiteral("Speecher %1 is available.").arg(facts.version)};
    case State::Downloading:
        return {QStringLiteral("Downloading…"),
                QStringLiteral("Downloading Speecher %1 (%2%)").arg(facts.version).arg(facts.percent),
                false};
    case State::ReadyToRestart:
        return {restartCaption(facts),
                facts.error.isEmpty() ? QStringLiteral("Restart to finish updating.") : facts.error};
    case State::RestartPending:
        return {QStringLiteral("Restarting…"), QStringLiteral("Restarting after this dictation…"),
                false};
    case State::Restarting:
        return {QStringLiteral("Restarting…"), QStringLiteral("Restarting…"), false};
    case State::CheckFailed:
    case State::Error:
        return {retryCaption(facts), failureText(facts)};
    }
    return {};
}

WhatsNewBannerModel whatsNewBanner(const QString &currentVersion)
{
    // The bare number: a nightly's suffix says nothing to someone reading
    // what changed.
    return {QStringLiteral("Speecher %1 is installed")
                .arg(currentVersion.section(QLatin1Char('-'), 0, 0)),
            QStringLiteral("See what's new"),
            QStringLiteral("Dismiss")};
}

UpdateBanner::UpdateBanner(UpdateController *updates, DictationSession *session, QObject *parent)
    : QObject(parent)
    , m_updates(updates)
    , m_session(session)
{
    connect(updates, &UpdateController::changed, this, &UpdateBanner::changed);
    // Only the start and end of a dictation change what the banner offers.
    connect(session, &DictationSession::stateChanged, this,
            [this, wasDictating = dictating(session)]() mutable {
                if (dictating(m_session) != wasDictating) {
                    wasDictating = !wasDictating;
                    emit changed();
                }
            });
}

UpdateBannerFacts UpdateBanner::facts() const
{
    return {
        m_updates->state(),
        m_updates->availableVersionDisplay(),
        m_updates->downloadPercent(),
        m_updates->errorMessage(),
        m_updates->bannerVisible(),
        m_updates->repeatedAutomaticCheckFailure(),
        m_updates->manualInstallRequired(),
        m_updates->stableReplacementAvailable(),
        m_updates->supportsAutomaticDownloads(),
        dictating(m_session),
        m_deferredVersion && *m_deferredVersion == m_updates->availableVersion(),
    };
}

UpdateBannerModel UpdateBanner::model() const
{
    return updateBannerModel(facts());
}

UpdateCheckRow UpdateBanner::checkRow(UpdateChannel channel) const
{
    return updateCheckRow(facts(), channel);
}

void UpdateBanner::runAction()
{
    // Downloads, installs and restarts in one go; from a failed state it
    // retries, or opens the release page when that is the only way.
    m_updates->installAndRestart();
}

void UpdateBanner::later()
{
    m_deferredVersion = m_updates->availableVersion();
    emit changed();
}

void UpdateBanner::dismiss()
{
    m_updates->dismissAvailableVersion();
}

void UpdateBanner::runCheckRow(UpdateChannel channel)
{
    switch (m_updates->state()) {
    case State::UpdateAvailable:
    case State::ReadyToRestart:
    case State::CheckFailed:
    case State::Error:
        m_updates->updateNow();
        return;
    default:
        m_updates->checkForUpdates(channel);
        return;
    }
}

void bindCheckForUpdatesRow(SettingsSchema &schema, const UpdateBanner *banner)
{
    for (SettingsPage &page : schema.pages) {
        for (SettingsSection &section : page.sections) {
            for (SettingsRow &row : section.rows) {
                if (row.id != QStringLiteral("checkForUpdates")) {
                    continue;
                }
                row.actionLabelValue = [banner](const AppSettings &settings) {
                    return banner->checkRow(settings.updates.channel).caption;
                };
                row.helpValue = [banner](const AppSettings &settings) {
                    return banner->checkRow(settings.updates.channel).help;
                };
                row.enabled = [banner](const AppSettings &settings, const Capabilities &) {
                    return banner->checkRow(settings.updates.channel).enabled;
                };
                return;
            }
        }
    }
}

} // namespace speecher
