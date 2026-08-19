#pragma once

#include "dictation/DictationPorts.h"

#include <QHash>

#include <functional>

namespace speecher {

struct ProviderDescriptor {
    QString id;
    QString label;
    QString setupHint;
    // Refinement providers only: this one can read a screenshot of the target
    // as context, which is what the setting offering that is gated on.
    bool supportsScreenshotContext = false;
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
    QList<ProviderDescriptor> refinementProviders() const;

    SpeechTranscriber *speechProvider(const QString &id);
    TranscriptRefiner *refinementProvider(const QString &id);

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
