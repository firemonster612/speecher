#include "frontend/quick/host/QuickUpdates.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "app/WhatsNewTracker.h"
#include "core/ReleaseNotes.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"

#include <QDesktopServices>
#include <QUrl>

namespace speecher {

namespace {

using State = UpdateController::State;

QuickUpdates *g_instance = nullptr;

QVariantMap whatsNewMap(const WhatsNew &news)
{
    QVariantList commits;
    for (const HistoryCommit &commit : news.commits) {
        const CommitSummary summary = commitSummary(commit);
        commits.append(QVariantMap{{QStringLiteral("title"), summary.title},
                                   {QStringLiteral("pullRequest"), summary.pullRequest},
                                   {QStringLiteral("sha"), commit.sha},
                                   {QStringLiteral("url"), summary.url.toString()}});
    }
    QVariantList releases;
    for (const ReleaseNote &note : news.releases) {
        QVariantList sections;
        for (const ReleaseNoteSection &section : releaseNoteSections(note.markdown)) {
            sections.append(QVariantMap{{QStringLiteral("title"), section.title},
                                        {QStringLiteral("items"), section.items}});
        }
        releases.append(QVariantMap{{QStringLiteral("version"), note.version},
                                    {QStringLiteral("sections"), sections}});
    }
    return {{QStringLiteral("commits"), commits},
            {QStringLiteral("comparison"), news.comparison.toString()},
            {QStringLiteral("releases"), releases}};
}

} // namespace

QuickUpdates::QuickUpdates(ApplicationController *controller, UpdateController *updates, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_updates(updates)
    , m_lastSeenVersion(controller->whatsNew()->pendingVersion())
    , m_whatsNew(whatsNewMap(speecher::whatsNew(bundledReleaseNotes(), embeddedHistory(), m_lastSeenVersion,
                                                updates->currentVersion())))
{
    g_instance = this;
    connect(updates, &UpdateController::changed, this, &QuickUpdates::changed);
    // What a restart waits for changes with them.
    connect(controller->session(), &DictationSession::stateChanged, this, &QuickUpdates::changed);
    connect(controller, &ApplicationController::recordingChanged, this, &QuickUpdates::changed);
    connect(updates, &UpdateController::openReleasePageRequested, this, &QuickUpdates::openAllReleases);
    connect(controller->whatsNew(), &WhatsNewTracker::pendingChanged, this, &QuickUpdates::whatsNewPendingChanged);
}

QuickUpdates::~QuickUpdates()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickUpdates *QuickUpdates::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickUpdates::create", "the host makes Updates before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QString QuickUpdates::state() const
{
    switch (m_updates->state()) {
    case State::Idle:
        return QStringLiteral("idle");
    case State::Checking:
        return QStringLiteral("checking");
    case State::CheckFailed:
        return QStringLiteral("checkFailed");
    case State::UpToDate:
        return QStringLiteral("upToDate");
    case State::UpdateAvailable:
        return QStringLiteral("available");
    case State::Downloading:
        return QStringLiteral("downloading");
    case State::ReadyToRestart:
        return QStringLiteral("ready");
    case State::RestartPending:
        return QStringLiteral("restartPending");
    case State::Restarting:
        return QStringLiteral("restarting");
    case State::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

QString QuickUpdates::availableVersion() const
{
    return m_updates->availableVersionDisplay();
}

qreal QuickUpdates::progress() const
{
    return m_updates->downloadPercent() / 100.0;
}

QString QuickUpdates::error() const
{
    return m_updates->errorMessage();
}

bool QuickUpdates::automaticDownloads() const
{
    return m_updates->supportsAutomaticDownloads();
}

bool QuickUpdates::manualInstall() const
{
    return m_updates->manualInstallRequired();
}

bool QuickUpdates::stableReplacement() const
{
    return m_updates->stableReplacementAvailable();
}

QString QuickUpdates::restartBlocker() const
{
    switch (m_updates->restartBlocker()) {
    case UpdateController::RestartBlocker::None:
        break;
    case UpdateController::RestartBlocker::Dictation:
        return QStringLiteral("dictation");
    case UpdateController::RestartBlocker::Recording:
        return QStringLiteral("recording");
    }
    return {};
}

QDateTime QuickUpdates::lastChecked() const
{
    const qint64 ms = m_controller->settings()->updatesLastCheckTime();
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms) : QDateTime();
}

bool QuickUpdates::attention() const
{
    const State state = m_updates->state();
    // A check's progress and result answer a click on the Updates page.
    if (state == State::Idle || state == State::Checking || state == State::UpToDate) {
        return false;
    }
    const bool putOff = !m_deferredVersion.isEmpty() && m_deferredVersion == m_updates->availableVersion()
        && (state == State::ReadyToRestart || state == State::RestartPending);
    // Automatic checks fail quietly until they keep failing.
    const bool failureWorthTelling = state == State::CheckFailed && m_updates->repeatedAutomaticCheckFailure();
    return (m_updates->bannerVisible() || failureWorthTelling) && !putOff;
}

bool QuickUpdates::whatsNewPending() const
{
    return !m_controller->whatsNew()->pendingVersion().isEmpty();
}

QVariantMap QuickUpdates::whatsNew() const
{
    return m_whatsNew;
}

void QuickUpdates::check()
{
    m_updates->checkForUpdates(m_controller->settings()->updateChannel());
}

void QuickUpdates::updateNow()
{
    m_updates->updateNow();
}

void QuickUpdates::installAndRestart()
{
    m_updates->installAndRestart();
}

void QuickUpdates::later()
{
    m_deferredVersion = m_updates->availableVersion();
    emit changed();
}

void QuickUpdates::dismiss()
{
    m_updates->dismissAvailableVersion();
}

void QuickUpdates::openAllReleases()
{
    QDesktopServices::openUrl(releasesPage());
}

bool QuickUpdates::newSince(const QString &sinceVersion) const
{
    return !m_lastSeenVersion.isEmpty() && compareBaseVersions(sinceVersion, m_lastSeenVersion) > 0
        && compareBaseVersions(sinceVersion, m_updates->currentVersion()) <= 0;
}

void QuickUpdates::acknowledgeWhatsNew()
{
    m_controller->whatsNew()->clearPending();
}

void QuickUpdates::showWhatsNew()
{
    emit pageRequested(QStringLiteral("updates"));
}

} // namespace speecher
