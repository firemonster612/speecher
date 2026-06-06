#pragma once

#include "dictation/DictationInterfaces.h"

#include <QHash>

#include <functional>

namespace speecher {

struct ProviderDescriptor {
    QString id;
    QString label;
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
