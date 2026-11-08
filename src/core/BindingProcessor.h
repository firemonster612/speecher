#pragma once

#include "core/AppSettings.h"

#include <QHash>
#include <QList>
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace speecher {

// One replacement as a dictation applies it: the spoken phrase and the text
// written in its place. Built from the Vocabulary Entries that apply.
struct BindingRule {
    QString phrase;
    QString replacement;

    bool operator==(const BindingRule &other) const = default;
};

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
    static QList<BindingRule> parseJsonImport(const QByteArray &json, QString *error = nullptr);
    static QStringList refinementVocabulary(const QList<BindingRule> &rules);
    static bool hasExplicitNoBindDirective(const QString &transcript);
    static QStringList explicitNoBindPhrases(const QString &transcript, const QList<BindingRule> &rules);
    static BindingProcessingResult process(const QString &transcript, const QList<BindingRule> &rules);
    static QString applyBindingsOutsidePlaceholders(const QString &text, const QList<BindingRule> &rules);
    // The refined text with its placeholders restored; not ok when it holds
    // placeholder-like text that sourceText, what refinement was given before
    // placeholders went in, does not.
    static BindingRestoreResult restorePlaceholders(const QString &refinedText,
                                                    const QList<BindingPlaceholder> &placeholders,
                                                    const QString &sourceText = {});
    static QHash<QString, QString> replacementsByPlaceholder(const QList<BindingPlaceholder> &placeholders);
    // restorePlaceholders for a refinement still streaming, given the
    // replacements by placeholder once for the whole stream.
    static QString restorePlaceholdersForPreview(const QString &streamedText,
                                                 const QHash<QString, QString> &replacements);
};

} // namespace speecher
