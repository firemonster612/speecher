#pragma once

#include "core/BindingProcessor.h"

namespace speecher {

struct BindingMatch {
    BindingRule rule;
    int startToken = 0;
    int tokenCount = 0;
    qsizetype start = 0;
    qsizetype end = 0;
};

class BindingMatcher {
public:
    static QStringList normalizedTokens(const QString &text);
    static QString normalizedPhrase(const QString &text);
    static QList<BindingMatch> findMatches(const QString &transcript, const QList<BindingRule> &rules);
    static BindingProcessingResult process(const QString &transcript, const QList<BindingRule> &rules);
    static QString applyOutsidePlaceholders(const QString &text, const QList<BindingRule> &rules);
    static BindingRestoreResult restorePlaceholders(const QString &refinedText,
                                                    const QList<BindingPlaceholder> &placeholders);
};

} // namespace speecher
