#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

// The option values that come with this build rather than from the settings,
// so a completion script carries them.
struct CompletionChoices {
    QStringList speechProviders;
    QStringList refinementProviders;
    QStringList spokenLanguages;
};

// The lists a completion script asks `speecher completions --list <list>` for
// each time it completes, because the settings change them.
inline const QString kCompletionProfiles = QStringLiteral("profiles");
inline const QString kCompletionTones = QStringLiteral("tones");
inline const QString kCompletionCleanupLevels = QStringLiteral("cleanup");

inline const QStringList kCompletionShells{QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("fish")};

// The script that completes speecher's commands and options in shell, one of
// kCompletionShells, or empty for any other.
QString completionScript(const QString &shell, const CompletionChoices &choices);

} // namespace speecher
