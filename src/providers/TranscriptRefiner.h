#pragma once

#include "core/AppSettings.h"
#include "core/ProviderFailure.h"
#include "providers/PreparationJob.h"

#include <QObject>
#include <QStringList>

#include <optional>

namespace speecher {

// What every refinement provider implements: it turns a Raw Transcript into
// the Refined Transcript.
class TranscriptRefiner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    // As SpeechTranscriber's, these and refine() take the accounts a provider
    // that signs in reads its credentials from. A sign-in that expires renews
    // in the refresh job, whose showRefreshIndicator says it is due.
    virtual std::optional<PreparationJob> createRefreshJob(const RefinementSettings &settings,
                                                           const AccountSettings &accounts)
    {
        Q_UNUSED(settings);
        Q_UNUSED(accounts);
        return std::nullopt;
    }
    virtual std::optional<ProviderFailure> prepare(const RefinementSettings &settings,
                                                   const AccountSettings &accounts) = 0;
    virtual void refine(const QString &rawTranscript,
                        const QStringList &vocabulary,
                        const RefinementContext &context,
                        const RefinementSettings &settings,
                        const AccountSettings &accounts) = 0;
    virtual void cancel() = 0;

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const speecher::ProviderFailure &failure);
};

} // namespace speecher
