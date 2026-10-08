#pragma once

#include "core/settings/FallbackPresentation.h"

#include <QByteArray>

#include <iosfwd>
#include <memory>
#include <optional>

namespace speecher {

class LocalSetup;
class ProviderRegistry;
class SettingsStore;
class SingleInstancePlatform;

// Every provider this build offers, judged from what the settings pages read:
// the saved settings and the setup's LiveFacts.
QList<ProviderReport> providerReports(const SettingsStore &settings, const LocalSetup &local,
                                      const ProviderRegistry &providers);

// One line holding a JSON array, an object per provider whose unknowns are
// null: what `speecher providers --json` prints and how the running app
// answers the providers command.
QByteArray providerReportsJson(const QList<ProviderReport> &reports);

// `speecher providers`' output: a table with a header, or the JSON line.
void printProviderReports(const QList<ProviderReport> &reports, bool json, std::ostream &out);

// The running app's reports, from the sign-ins it has seen and the runners it
// has looked for. Nothing when no app answers the command: none runs, or an
// older one doesn't know it. error says why an app's answer couldn't be read.
std::optional<QList<ProviderReport>> runningAppProviderReports(
    const std::shared_ptr<const SingleInstancePlatform> &platform, QString *error);

// Prints the running app's reports, or without one judges them in this
// process: from the saved settings, the downloaded Local Models and the
// operating system's network state, without asking a server, a keyring or a
// sign-in refresh. Sign-ins and whether a Local Runner is running stay unknown
// then. Returns the exit code.
int runProvidersCommand(bool json, const std::shared_ptr<const SingleInstancePlatform> &platform,
                        std::ostream &out, std::ostream &err);

} // namespace speecher
