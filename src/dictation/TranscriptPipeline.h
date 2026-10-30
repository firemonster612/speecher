#pragma once

#include "core/AppSettings.h"
#include "core/BindingProcessor.h"
#include "core/Target.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace speecher {

// What one Dictation Session, Transcribe batch or Recording chooses for
// itself, ahead of the saved settings. Never saved.
struct RunChoices {
    // The Writing Profile it runs under, ahead of every rule: the one
    // `toggle --profile` or `start --profile` forces, or Transcribe's page's.
    // Empty lets the target decide.
    QString writingProfile;
    // Terms it adds, such as --vocab-file's, in order. They lead the speech
    // request and refinement whatever the saved list says.
    QStringList vocabulary;
};

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
    // the session's, those for its Writing Profile, then the Learned
    // Corrections.
    static QStringList speechVocabulary(const AppSettings &settings, const RunChoices &choices,
                                        const Target &target);
    // The Writing Profile a dictation into the target runs under.
    static WritingProfileSettings writingProfile(const AppSettings &settings, const RunChoices &choices,
                                                 const Target &target);
    static RefinementSettings effectiveRefinementSettings(const AppSettings &settings,
                                                          const RunChoices &choices,
                                                          const Target &target);
    // spokenLanguage is the language rawTranscript was heard in, which
    // refinement is told.
    static TranscriptPipelineResult prepare(const QString &rawTranscript,
                                            const AppSettings &settings,
                                            const RunChoices &choices,
                                            const Target &target,
                                            const QString &spokenLanguage);
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
