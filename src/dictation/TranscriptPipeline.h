#pragma once

#include "core/AppSettings.h"
#include "core/BindingProcessor.h"
#include "core/Target.h"
#include "dictation/DictationPlan.h"

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
    // What the user gave refinement before placeholders went in: the raw
    // transcript, then for a selection edit the selected text.
    QString refinementSource;
    bool allowPostRefinementBindings = true;
    bool editsSelection = false;
};

class TranscriptPipeline {
public:
    // spokenLanguage is the language rawTranscript was heard in, which
    // refinement is told.
    static TranscriptPipelineResult prepare(const QString &rawTranscript,
                                            const DictationPlan &plan,
                                            const Target &target,
                                            const QString &spokenLanguage);
    static void includeScreenshotContext(TranscriptPipelineResult &pipeline,
                                         bool supportsScreenshotContext,
                                         const QByteArray &screenshotData,
                                         const QString &screenshotMediaType);
    static std::optional<QString> restoreRefinedResult(const TranscriptPipelineResult &pipeline,
                                                       const QString &refinedText);
};

} // namespace speecher
