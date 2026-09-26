#pragma once

#include "core/Target.h"

#include <QString>
#include <QStringList>

namespace speecher {

QString dictationRefinementSystemPrompt(const QString &style,
                                        const RefinementContext &context = {});
QString selectedDocumentEditingSystemPrompt(const QString &style,
                                            const RefinementContext &context = {});
// The system prompt for this request: selection editing or dictation cleanup.
QString refinementSystemPrompt(const QString &style, const RefinementContext &context);
// About a hundred words of the same cleanup rules, for the small models a
// Local Runner serves: under the full prompt a 1B model loses the transcript
// and answers with one of the prompt's own examples. Selection editing keeps
// its full prompt.
QString compactRefinementSystemPrompt(const QString &style, const RefinementContext &context);
QString transcriptRefinementUserMessage(const QString &rawTranscript,
                                        const QStringList &vocabulary,
                                        const QStringList &bindingVocabulary,
                                        const RefinementContext &context = {});

} // namespace speecher
