#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

QString transcriptRefinementInstructions(const QString &style);
QString transcriptRefinementUserMessage(const QString &rawTranscript,
                                        const QStringList &vocabulary,
                                        const QStringList &bindingVocabulary);

} // namespace speecher
