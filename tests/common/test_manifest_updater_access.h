#pragma once

#include "app/ManifestUpdater.h"

#include <QNetworkReply>
#include <QTimer>

#include <optional>

namespace speecher {

class ManifestUpdaterTestAccess {
public:
    static void setState(ManifestUpdater &updater,
                         UpdateController::State state,
                         const QString &error = {})
    {
        updater.setState(state, error);
    }

    static void setAvailableVersion(ManifestUpdater &updater,
                                    const QString &version,
                                    UpdateChannel channel = UpdateChannel::Stable,
                                    qint64 buildNumber = 0)
    {
        updater.m_manifest.version = version;
        updater.m_manifest.channel = channel;
        updater.m_manifest.buildNumber = buildNumber;
    }

    static std::optional<UpdateManifest> bestCandidate(std::optional<UpdateManifest> primary,
                                                       std::optional<UpdateManifest> stable)
    {
        return ManifestUpdater::bestCandidate(std::move(primary), std::move(stable));
    }

    static void restartNow(ManifestUpdater &updater)
    {
        updater.restartNow();
    }

    static bool shouldOfferManifest(const UpdateManifest &manifest,
                                    qint64 currentBuildNumber,
                                    const QString &currentVersion,
                                    UpdateChannel channel,
                                    bool automaticCheck)
    {
        return ManifestUpdater::shouldOfferManifest(
            manifest, currentBuildNumber, currentVersion, channel, automaticCheck);
    }

    static void finishCheck(ManifestUpdater &updater,
                            QNetworkReply *reply,
                            bool automatic)
    {
        updater.m_automaticCheck = automatic;
        updater.m_reply = reply;
        updater.finishCheck(reply);
    }

    static int nextCheckDelay(const ManifestUpdater &updater)
    {
        return updater.m_checkTimer->interval();
    }

    static bool checkTimerActive(const ManifestUpdater &updater)
    {
        return updater.m_checkTimer->isActive();
    }

    static void setAutomaticCheckFailures(ManifestUpdater &updater, int failures)
    {
        updater.m_automaticCheckFailures = failures;
    }

    static void setManualInstallRequired(ManifestUpdater &updater, bool required)
    {
        updater.m_manualInstallRequired = required;
    }
};

} // namespace speecher
