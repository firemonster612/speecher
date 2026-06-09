#pragma once

#include "core/AppSettings.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace speecher {

struct BindingValidationIssue {
    enum class Type {
        EmptyPhrase,
        EmptyReplacement,
        DuplicatePhrase,
    };

    Type type = Type::EmptyPhrase;
    int row = -1;
    int duplicateOf = -1;
    QString message;
};

struct BindingValidationResult {
    QList<BindingValidationIssue> issues;
    QList<BindingRule> rules;

    bool ok() const { return issues.isEmpty(); }
    QStringList messages() const;
};

struct BindingPlaceholder {
    QString placeholder;
    QString replacement;
};

struct BindingProcessingResult {
    QString boundText;
    QString placeholderText;
    bool canSkipRefinement = false;
    QList<BindingPlaceholder> placeholders;

    bool hasMatches() const { return !placeholders.isEmpty(); }
};

struct BindingRestoreResult {
    bool ok = true;
    QString text;
};

class BindingProcessor {
public:
    static QStringList normalizedTokens(const QString &text);
    static QString normalizedPhrase(const QString &text);

    static BindingValidationResult validateRules(const QList<BindingRule> &rules);
    static QStringList refinementVocabulary(const QList<BindingRule> &rules);
    static bool hasExplicitNoBindDirective(const QString &transcript);
    static QStringList explicitNoBindPhrases(const QString &transcript, const QList<BindingRule> &rules);
    static BindingProcessingResult process(const QString &transcript, const QList<BindingRule> &rules);
    static QString applyBindingsOutsidePlaceholders(const QString &text, const QList<BindingRule> &rules);
    static BindingRestoreResult restorePlaceholders(const QString &refinedText,
                                                    const QList<BindingPlaceholder> &placeholders);
};

} // namespace speecher
