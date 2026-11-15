#include "dictation/TranscriptPipeline.h"

#include "core/RefinementRules.h"
#include "core/VocabularyScope.h"

#include <QDateTime>
#include <QRegularExpression>
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

// The rules on for this profile at the level refinement runs at, the
// profile's output language and the custom system prompt.
void fillRules(RefinementContext &context, const RefinementSettings &refinement,
               const WritingProfileSettings &profile)
{
    context.userRules = userRuleTexts(profile, refinement.style, refinement.rules);
    context.changedSteps = changedSteps(profile, refinement.style, refinement.rules);
    context.codingAgent = ruleOn(kCodingAgentRule, profile, refinement.style, refinement.rules);
    context.outputLanguage = profile.outputLanguage;
    context.customSystemPrompt =
        refinement.customSystemPromptEnabled ? refinement.customSystemPrompt : QString();
}

// A revised selection without the blank lines before it and the whitespace
// after it. Its first line keeps its indentation, which in code or a nested
// list is part of the text.
QString trimmedKeepingIndentation(const QString &text)
{
    static const QRegularExpression blankEdges(QStringLiteral("^\\s*\\n|\\s+$"));
    return QString(text).remove(blankEdges);
}

} // namespace

TranscriptPipelineResult TranscriptPipeline::prepare(const QString &rawTranscript,
                                                     const DictationPlan &plan,
                                                     const Target &target,
                                                     const QString &spokenLanguage)
{
    TranscriptPipelineResult result;
    const AppSettings &settings = plan.settings;
    const QString &writingProfile = plan.profile.profile;
    const VocabularyScope scope{writingProfile, target, settings.apps};
    // Read once, so the expansion before refinement, the one after it and
    // the placeholder restore all insert the same date and time.
    const QList<BindingRule> bindings = replacementRules(settings.vocabulary, scope, QDateTime::currentDateTime());
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
    result.refinementSource = result.editsSelection
        ? rawTranscript + QLatin1Char('\n') + target.selectedText
        : rawTranscript;
    if (result.editsSelection) {
        result.allowPostRefinementBindings = false;
    }
    result.refinementSettings = settings.refinement;
    result.refinementContext.target = target;
    result.refinementContext.spokenLanguage = spokenLanguage;
    result.refinementContext.writingProfile = writingProfile;
    result.refinementContext.writingProfileName =
        writingProfileLabel(writingProfile, result.refinementSettings.writingProfiles);
    result.refinementVocabulary = refinementVocabulary(settings.vocabulary, scope, plan.addedVocabulary,
                                                       result.refinementContext.vocabularyContext);
    result.refinementContext.tone = plan.profile.tone;
    result.refinementContext.bindingVocabulary = BindingProcessor::refinementVocabulary(bindings);
    result.refinementSettings.style =
        refinedCleanupLevel(result.refinementSettings.style, plan.profile.outputLanguage);
    fillRules(result.refinementContext, result.refinementSettings, plan.profile);
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
    // Set or cleared each time, for each refiner a session tries in turn.
    const bool include = pipeline.refinementSettings.includeScreenshotContext
        && !pipeline.editsSelection
        && !pipeline.refinementContext.target.secure
        && supportsScreenshotContext
        && !screenshotData.isEmpty()
        && !screenshotMediaType.isEmpty();
    pipeline.refinementContext.screenshotData = include ? screenshotData : QByteArray();
    pipeline.refinementContext.screenshotMediaType = include ? screenshotMediaType : QString();
}

std::optional<QString> TranscriptPipeline::restoreRefinedResult(
    const TranscriptPipelineResult &pipeline,
    const QString &refinedText)
{
    const QString refined = pipeline.editsSelection
        ? trimmedKeepingIndentation(refinedText)
        : refinedText.trimmed();
    if (refined.isEmpty()) {
        return std::nullopt;
    }

    const QString postBound = pipeline.allowPostRefinementBindings
        ? BindingProcessor::applyBindingsOutsidePlaceholders(refined, pipeline.activeBindingRules)
        : refined;
    const BindingRestoreResult restored = BindingProcessor::restorePlaceholders(
        postBound,
        pipeline.bindingResult.placeholders,
        pipeline.refinementSource);
    return restored.ok ? std::optional<QString>(restored.text) : std::nullopt;
}

} // namespace speecher
