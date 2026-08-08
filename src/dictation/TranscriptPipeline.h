#pragma once

#include "core/AppSettings.h"
#include "core/BindingProcessor.h"
#include "core/Target.h"

#include <QByteArray>
#include <QString>

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
};

class TranscriptPipeline {
public:
    static RefinementSettings effectiveRefinementSettings(const AppSettings &settings,
                                                          const Target &target);
    static TranscriptPipelineResult prepare(const QString &rawTranscript,
                                            const AppSettings &settings,
                                            const Target &target);
    static void includeScreenshotContext(TranscriptPipelineResult &pipeline,
                                         bool supportsScreenshotContext,
                                         const QByteArray &screenshotData,
                                         const QString &screenshotMediaType);
    static QString restoreRefinedResult(const TranscriptPipelineResult &pipeline,
                                        const QString &refinedText);
};

} // namespace speecher
