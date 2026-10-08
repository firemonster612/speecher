#pragma once

#include "core/ProviderChain.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationPorts.h"

#include <QHash>
#include <QVector>

#include <functional>

namespace speecher {

// One user-facing fact about a provider ("Languages" / "About 60").
struct ProviderStat {
    QString label;
    QString value;
};

// Which sign-in a speech provider reads, named the way the user would go
// looking for it: setup needs the CLI's login, not the transcription brand.
// Every frontend's welcome step names it the same way.
QString credentialSourceLabel(const QString &providerId, const QString &fallback);

struct ProviderDescriptor {
    QString id;
    QString label;
    QString setupHint;
    // Refinement providers only: this one can read a screenshot of the target
    // as context, which is what the setting offering that is gated on.
    bool supportsScreenshotContext = false;
    // One-or-two-line digest of the stats, for settings rows and tooltips.
    QString summary;
    // Shown as a label/value block on the setup assistant's provider steps.
    QVector<ProviderStat> stats;
};

class ProviderRegistry : public QObject {
    Q_OBJECT

public:
    using SpeechFactory = std::function<SpeechTranscriber *(QObject *)>;
    using RefinementFactory = std::function<TranscriptRefiner *(QObject *)>;

    explicit ProviderRegistry(QObject *parent = nullptr);

    void registerSpeechProvider(const ProviderDescriptor &descriptor, SpeechFactory factory);
    void registerRefinementProvider(const ProviderDescriptor &descriptor, RefinementFactory factory);

    QList<ProviderDescriptor> speechProviders() const;
    // The id itself for one this registry does not offer.
    QString speechProviderLabel(const QString &id) const;
    QList<ProviderDescriptor> refinementProviders() const;
    // The id itself for one this registry does not offer.
    QString refinementProviderLabel(const QString &id) const;
    // Both, by role, for as long as the registry lives.
    ProviderLabels labels() const;
    // withWritingProfileProviders, passing over a profile's speech service
    // this registry lacks, as one that can't hear the Spoken Language is.
    AppSettings withProfileProviders(const AppSettings &settings, const WritingProfileSettings &profile) const;
    // A role's providers as settings rows and fallback lists offer them: id,
    // label and summary.
    QList<RowOption> rowOptions(ProviderRole role) const;

    SpeechTranscriber *speechProvider(const QString &id);
    TranscriptRefiner *refinementProvider(const QString &id);

    // Fresh, caller-owned instances for work that must not share the cached
    // ones with live dictation (a transcriber holds one attempt at a time).
    // Null for an unknown id.
    SpeechTranscriber *createSpeechProvider(const QString &id, QObject *parent);
    TranscriptRefiner *createRefinementProvider(const QString &id, QObject *parent);

private:
    struct SpeechEntry {
        ProviderDescriptor descriptor;
        SpeechFactory factory;
        SpeechTranscriber *instance = nullptr;
    };

    struct RefinementEntry {
        ProviderDescriptor descriptor;
        RefinementFactory factory;
        TranscriptRefiner *instance = nullptr;
    };

    QHash<QString, SpeechEntry> m_speech;
    QHash<QString, RefinementEntry> m_refinement;
};

} // namespace speecher
