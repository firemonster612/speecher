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
QString transcriptRefinementUserMessage(const QString &rawTranscript,
                                        const QStringList &vocabulary,
                                        const QStringList &bindingVocabulary,
                                        const RefinementContext &context = {});

} // namespace speecher
