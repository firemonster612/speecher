#pragma once

#include "core/AppSettings.h"
#include "core/BindingProcessor.h"
#include "core/Target.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace speecher {

struct TranscriptPipelineResult {
    BindingProcessingResult bindingResult;
    QList<BindingRule> activeBindingRules;
    QStringList noBindPhrases;
    RefinementSettings refinementSettings;
    QStringList refinementVocabulary;
    RefinementContext refinementContext;
    QString deliveryFallback;
    QString refinementInput;
    bool allowPostRefinementBindings = true;
    bool editsSelection = false;
};

class TranscriptPipeline {
public:
    // The terms the speech request carries for a dictation into the target:
    // those for its Writing Profile, then the Learned Corrections.
    static QStringList speechVocabulary(const AppSettings &settings, const Target &target);
    static RefinementSettings effectiveRefinementSettings(const AppSettings &settings,
                                                          const Target &target);
    static TranscriptPipelineResult prepare(const QString &rawTranscript,
                                            const AppSettings &settings,
                                            const Target &target);
    // The user's instructions, the profile's output language and the custom
    // system prompt for a refinement run with this profile.
    static void fillUserInstructions(RefinementContext &context,
                                     const RefinementSettings &refinement,
                                     const WritingProfileSettings &profile);
    // A tone or cleanup level the user defined goes on the context, and the
    // style becomes the level's base, which is what the prompt builds on.
    static void resolveCustomChoices(TranscriptPipelineResult &pipeline);
    static void includeScreenshotContext(TranscriptPipelineResult &pipeline,
                                         bool supportsScreenshotContext,
                                         const QByteArray &screenshotData,
                                         const QString &screenshotMediaType);
    static std::optional<QString> restoreRefinedResult(const TranscriptPipelineResult &pipeline,
                                                       const QString &refinedText);
};

} // namespace speecher
