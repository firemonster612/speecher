#include "app/WhatsNewTracker.h"

#include "core/ReleaseNotes.h"
#include "core/SettingsStore.h"

namespace speecher {

WhatsNewTracker::WhatsNewTracker(SettingsStore &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    const QString currentVersion = QStringLiteral(SPEECHER_VERSION);
    const qint64 currentBuildNumber = SPEECHER_BUILD_NUMBER;
    const QString previousVersion = m_settings.updatesLastRunVersion();
    const qint64 previousBuildNumber = m_settings.updatesLastRunBuildNumber();
    if (previousVersion != currentVersion || previousBuildNumber != currentBuildNumber) {
        const bool migratedNightlyUpgrade = previousBuildNumber < 0
            && !previousVersion.isEmpty()
            && previousVersion.contains(QStringLiteral("-nightly"))
            && currentVersion.contains(QStringLiteral("-nightly"))
            && previousVersion != currentVersion;
        const bool upgraded = previousBuildNumber >= 0
            ? currentBuildNumber > previousBuildNumber
            : compareBaseVersions(currentVersion, previousVersion) > 0 || migratedNightlyUpgrade;
        if (upgraded
            && m_settings.updatesPendingWhatsNewVersion().isEmpty()) {
            m_settings.setUpdatesPendingWhatsNewVersion(previousVersion);
        }
        m_settings.setUpdatesLastRunVersion(currentVersion);
        m_settings.setUpdatesLastRunBuildNumber(currentBuildNumber);
    }
    m_pendingVersion = m_settings.updatesPendingWhatsNewVersion();
}

QString WhatsNewTracker::pendingVersion() const
{
    return m_pendingVersion;
}

void WhatsNewTracker::clearPending()
{
    if (m_pendingVersion.isEmpty()) {
        return;
    }
    m_pendingVersion.clear();
    m_settings.setUpdatesPendingWhatsNewVersion({});
    emit pendingChanged();
}

} // namespace speecher
