#include "providers/ProviderRegistry.h"

#include <algorithm>

namespace speecher {

namespace {

void sortProviders(QList<ProviderDescriptor> &providers)
{
    std::sort(providers.begin(), providers.end(), [](const ProviderDescriptor &left, const ProviderDescriptor &right) {
        if (left.label == right.label) {
            return left.id < right.id;
        }
        return left.label < right.label;
    });
}

} // namespace

ProviderRegistry::ProviderRegistry(QObject *parent)
    : QObject(parent)
{
}

void ProviderRegistry::registerSpeechProvider(const ProviderDescriptor &descriptor, SpeechFactory factory)
{
    m_speech.insert(descriptor.id, SpeechEntry{descriptor, std::move(factory), nullptr});
}

void ProviderRegistry::registerRefinementProvider(const ProviderDescriptor &descriptor, RefinementFactory factory)
{
    m_refinement.insert(descriptor.id, RefinementEntry{descriptor, std::move(factory), nullptr});
}

QList<ProviderDescriptor> ProviderRegistry::speechProviders() const
{
    QList<ProviderDescriptor> providers;
    for (const SpeechEntry &entry : m_speech) {
        providers << entry.descriptor;
    }
    sortProviders(providers);
    return providers;
}

QList<ProviderDescriptor> ProviderRegistry::refinementProviders() const
{
    QList<ProviderDescriptor> providers;
    for (const RefinementEntry &entry : m_refinement) {
        providers << entry.descriptor;
    }
    sortProviders(providers);
    return providers;
}

SpeechTranscriber *ProviderRegistry::speechProvider(const QString &id)
{
    auto it = m_speech.find(id);
    if (it == m_speech.end()) {
        return nullptr;
    }
    if (!it->instance) {
        it->instance = it->factory(this);
    }
    return it->instance;
}

SpeechTranscriber *ProviderRegistry::createSpeechProvider(const QString &id, QObject *parent)
{
    const auto it = m_speech.constFind(id);
    return it == m_speech.constEnd() ? nullptr : it->factory(parent);
}

TranscriptRefiner *ProviderRegistry::createRefinementProvider(const QString &id, QObject *parent)
{
    const auto it = m_refinement.constFind(id);
    return it == m_refinement.constEnd() ? nullptr : it->factory(parent);
}

QString credentialSourceLabel(const QString &providerId, const QString &fallback)
{
    if (providerId == QStringLiteral("claude")) {
        return QStringLiteral("Claude Code");
    }
    if (providerId == QStringLiteral("codex")) {
        return QStringLiteral("Codex CLI or the ChatGPT app");
    }
    return fallback;
}

TranscriptRefiner *ProviderRegistry::refinementProvider(const QString &id)
{
    auto it = m_refinement.find(id);
    if (it == m_refinement.end()) {
        return nullptr;
    }
    if (!it->instance) {
        it->instance = it->factory(this);
    }
    return it->instance;
}

} // namespace speecher
