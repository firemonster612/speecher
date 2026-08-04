#include "dictation/TranscriptPipeline.h"

#include <QSet>

namespace speecher {
namespace {

QList<BindingRule> withoutNoBindPhrases(const QList<BindingRule> &rules,
                                        const QStringList &normalizedNoBindPhrases)
{
    if (normalizedNoBindPhrases.isEmpty()) {
        return rules;
    }

    QSet<QString> excluded;
    for (const QString &phrase : normalizedNoBindPhrases) {
        excluded.insert(phrase);
    }
    QList<BindingRule> filtered;
    for (const BindingRule &rule : rules) {
        if (!excluded.contains(BindingProcessor::normalizedPhrase(rule.phrase))) {
            filtered.append(rule);
        }
    }
    return filtered;
}

QStringList refinementVocabulary(const AppSettings &settings)
{
    QSet<QString> seen;
    QStringList deduplicated;
    for (const QString &term : settings.speech.vocabulary) {
        const QString cleaned = term.simplified();
        const QString key = cleaned.toCaseFolded();
        if (!cleaned.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            deduplicated.append(cleaned);
        }
    }
    return deduplicated;
}

} // namespace

RefinementSettings TranscriptPipeline::effectiveRefinementSettings(const AppSettings &settings,
                                                                   const Target &target)
{
    RefinementSettings refinement = settings.refinement;
    refinement.bindingVocabulary = BindingProcessor::refinementVocabulary(settings.bindings);
    const WritingProfile resolved = resolveWritingProfile(
        target,
        refinement.writingProfileOverrides,
        writingProfileFromName(refinement.defaultWritingProfile));
    const WritingProfileSettings profileSettings = writingProfileSettingsFor(
        refinement.writingProfiles,
        resolved);
    refinement.style = profileSettings.cleanupStrength;
    refinement.tone = profileSettings.tone;
    return refinement;
}

TranscriptPipelineResult TranscriptPipeline::prepare(const QString &rawTranscript,
                                                     const AppSettings &settings,
                                                     const Target &target)
{
    TranscriptPipelineResult result;
    const bool hasNoBindDirective = BindingProcessor::hasExplicitNoBindDirective(rawTranscript);
    result.noBindPhrases = BindingProcessor::explicitNoBindPhrases(rawTranscript, settings.bindings);
    result.allowPostRefinementBindings = !hasNoBindDirective || !result.noBindPhrases.isEmpty();
    result.activeBindingRules = withoutNoBindPhrases(settings.bindings, result.noBindPhrases);
    result.bindingResult = BindingProcessor::process(rawTranscript, result.activeBindingRules);
    result.deliveryFallback = result.bindingResult.boundText;
    result.refinementInput = result.bindingResult.placeholderText;
    result.refinementSettings = effectiveRefinementSettings(settings, target);
    result.refinementVocabulary = refinementVocabulary(settings);

    result.refinementContext.target = target;
    result.refinementContext.writingProfile = resolveWritingProfile(
        target,
        result.refinementSettings.writingProfileOverrides,
        writingProfileFromName(result.refinementSettings.defaultWritingProfile));
    result.refinementContext.tone = result.refinementSettings.tone;
    result.refinementContext.includeNearbyText = result.refinementSettings.useTargetContext && !target.secure;
    if (!result.refinementSettings.useTargetContext) {
        result.refinementContext.target.nearbyTextBefore.clear();
        result.refinementContext.target.nearbyTextAfter.clear();
    }
    return result;
}

void TranscriptPipeline::includeScreenshotContext(TranscriptPipelineResult &pipeline,
                                                  bool supportsScreenshotContext,
                                                  const QByteArray &screenshotData,
                                                  const QString &screenshotMediaType)
{
    if (pipeline.refinementSettings.includeScreenshotContext
        && !pipeline.refinementContext.target.secure
        && supportsScreenshotContext
        && !screenshotData.isEmpty()
        && !screenshotMediaType.isEmpty()) {
        pipeline.refinementContext.screenshotData = screenshotData;
        pipeline.refinementContext.screenshotMediaType = screenshotMediaType;
    }
}

QString TranscriptPipeline::restoreRefinedResult(const TranscriptPipelineResult &pipeline,
                                                 const QString &refinedText)
{
    const QString refined = refinedText.trimmed();
    if (refined.isEmpty()) {
        return pipeline.deliveryFallback;
    }

    const QString postBound = pipeline.allowPostRefinementBindings
        ? BindingProcessor::applyBindingsOutsidePlaceholders(refined, pipeline.activeBindingRules)
        : refined;
    const BindingRestoreResult restored = BindingProcessor::restorePlaceholders(
        postBound,
        pipeline.bindingResult.placeholders);
    return restored.ok ? restored.text : pipeline.deliveryFallback;
}

} // namespace speecher
