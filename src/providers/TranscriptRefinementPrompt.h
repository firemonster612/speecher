#pragma once

#include "core/Target.h"

#include <QString>
#include <QStringList>

namespace speecher {

// The dictation prompt: the built-in rules for the style, or the context's
// custom system prompt followed by the tone rule, then the user's instructions
// and the target context.
QString dictationRefinementSystemPrompt(const QString &style,
                                        const RefinementContext &context = {});
// The built-in dictation rules at Medium cleanup with no tone, which a custom
// system prompt starts from and resets to.
QString builtInDictationSystemPrompt();
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
