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
    for (const LearnedCorrection &correction : settings.learnedCorrections) {
        const QString cleaned = correction.corrected.simplified();
        const QString key = cleaned.toCaseFolded();
        if (correction.enabled && !cleaned.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            deduplicated.append(cleaned);
        }
    }
    return deduplicated;
}

QList<BindingRule> activeBindings(const AppSettings &settings, const Target &target)
{
    QList<BindingRule> rules = settings.bindings;
    QSet<QString> claimedPhrases;
    for (const BindingRule &rule : settings.bindings) {
        claimedPhrases.insert(BindingProcessor::normalizedPhrase(rule.phrase));
    }

    const auto appendCorrections = [&](bool exactApplication) {
        for (const LearnedCorrection &correction : settings.learnedCorrections) {
            const bool exact = !correction.applicationId.isEmpty()
                && correction.applicationId.compare(target.applicationId, Qt::CaseInsensitive) == 0;
            const bool applies = exactApplication ? exact : correction.applicationId.isEmpty();
            const QString phrase = BindingProcessor::normalizedPhrase(correction.original);
            if (correction.enabled && applies && !phrase.isEmpty()
                && !claimedPhrases.contains(phrase)) {
                rules.append({correction.original, correction.corrected});
                claimedPhrases.insert(phrase);
            }
        }
    };
    appendCorrections(true);
    appendCorrections(false);
    return rules;
}

} // namespace

RefinementSettings TranscriptPipeline::effectiveRefinementSettings(const AppSettings &settings,
                                                                   const Target &target)
{
    RefinementSettings refinement = settings.refinement;
    refinement.bindingVocabulary = BindingProcessor::refinementVocabulary(
        activeBindings(settings, target));
    const WritingProfile resolved = resolveWritingProfile(
        target,
        refinement.writingProfileOverrides,
        settings.appRecognitionRules,
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
    const QList<BindingRule> bindings = activeBindings(settings, target);
    const bool hasNoBindDirective = BindingProcessor::hasExplicitNoBindDirective(rawTranscript);
    result.noBindPhrases = BindingProcessor::explicitNoBindPhrases(rawTranscript, bindings);
    result.allowPostRefinementBindings = !hasNoBindDirective || !result.noBindPhrases.isEmpty();
    result.activeBindingRules = withoutNoBindPhrases(bindings, result.noBindPhrases);
    result.bindingResult = BindingProcessor::process(rawTranscript, result.activeBindingRules);
    result.editsSelection = target.hasSelection();
    result.deliveryFallback = result.editsSelection
        ? target.selectedText
        : result.bindingResult.boundText;
    result.refinementInput = result.bindingResult.placeholderText;
    if (result.editsSelection) {
        result.allowPostRefinementBindings = false;
    }
    result.refinementSettings = effectiveRefinementSettings(settings, target);
    result.refinementVocabulary = refinementVocabulary(settings);

    result.refinementContext.target = target;
    result.refinementContext.writingProfile = resolveWritingProfile(
        target,
        result.refinementSettings.writingProfileOverrides,
        settings.appRecognitionRules,
        writingProfileFromName(result.refinementSettings.defaultWritingProfile));
    result.refinementContext.tone = result.refinementSettings.tone;
    result.refinementContext.includeNearbyText = result.refinementSettings.useTargetContext && !target.secure;
    result.refinementContext.editSelection = result.editsSelection;
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
        && !pipeline.editsSelection
        && !pipeline.refinementContext.target.secure
        && supportsScreenshotContext
        && !screenshotData.isEmpty()
        && !screenshotMediaType.isEmpty()) {
        pipeline.refinementContext.screenshotData = screenshotData;
        pipeline.refinementContext.screenshotMediaType = screenshotMediaType;
    }
}

std::optional<QString> TranscriptPipeline::restoreRefinedResult(
    const TranscriptPipelineResult &pipeline,
    const QString &refinedText)
{
    const QString refined = refinedText.trimmed();
    if (refined.isEmpty()) {
        return std::nullopt;
    }

    const QString postBound = pipeline.allowPostRefinementBindings
        ? BindingProcessor::applyBindingsOutsidePlaceholders(refined, pipeline.activeBindingRules)
        : refined;
    const BindingRestoreResult restored = BindingProcessor::restorePlaceholders(
        postBound,
        pipeline.bindingResult.placeholders);
    return restored.ok ? std::optional<QString>(restored.text) : std::nullopt;
}

} // namespace speecher
