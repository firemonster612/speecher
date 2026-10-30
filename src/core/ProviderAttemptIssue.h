#pragma once

#include "core/ProviderFailure.h"
#include "core/ProviderTable.h"

#include <QList>
#include <QString>

#include <algorithm>
#include <functional>
#include <iterator>

namespace speecher {

// Why a provider in a Dictation Session's chain did not do its part.
struct ProviderAttemptIssue {
    enum class Stage {
        // Its preparation failed.
        Prepare,
        // It can't listen for the Spoken Language, so it was passed over.
        LanguageSkipped,
        // It failed before it heard any of the dictation.
        Connect,
        // It stopped after it had started hearing the dictation.
        Interrupted,
    };
    ProviderRole role = ProviderRole::Speech;
    QString providerId;
    Stage stage = Stage::Prepare;
    ProviderFailureKind kind = ProviderFailureKind::Unavailable;
    // The provider's own reason.
    QString message;
    // It needs the internet and could not connect, or was passed over,
    // while the system reported none; only then may an outcome say "No
    // internet".
    bool offline = false;
    // A speech provider that never connected still took audio the
    // microphone heard, which is never sent again (rule A7): a few words may
    // be missing. Always so for one Interrupted.
    bool wordsLost = false;
};

// The issues for which keep returns true, in their original order.
inline QList<ProviderAttemptIssue> issuesWhere(const QList<ProviderAttemptIssue> &issues,
                                               const std::function<bool(const ProviderAttemptIssue &)> &keep)
{
    QList<ProviderAttemptIssue> kept;
    std::copy_if(issues.cbegin(), issues.cend(), std::back_inserter(kept), keep);
    return kept;
}

} // namespace speecher
