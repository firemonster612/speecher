#include "core/BindingProcessor.h"

#include "core/bindings/BindingMatcher.h"
#include "core/bindings/NoBindDirectiveParser.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace speecher {
namespace {

QString validationMessage(BindingValidationIssue::Type type, int row, int duplicateOf = -1)
{
    const int displayRow = row + 1;
    switch (type) {
    case BindingValidationIssue::Type::EmptyPhrase:
        return QStringLiteral("Row %1 needs a spoken phrase containing at least one letter or number.").arg(displayRow);
    case BindingValidationIssue::Type::EmptyReplacement:
        return QStringLiteral("Row %1 needs replacement text.").arg(displayRow);
    case BindingValidationIssue::Type::DuplicatePhrase:
        return QStringLiteral("Row %1 duplicates the normalized spoken phrase from row %2.")
            .arg(displayRow)
            .arg(duplicateOf + 1);
    }
    return {};
}

} // namespace

QStringList BindingValidationResult::messages() const
{
    QStringList values;
    values.reserve(issues.size());
    for (const BindingValidationIssue &issue : issues) {
        values.append(issue.message);
    }
    return values;
}

QStringList BindingProcessor::normalizedTokens(const QString &text)
{
    return BindingMatcher::normalizedTokens(text);
}

QString BindingProcessor::normalizedPhrase(const QString &text)
{
    return BindingMatcher::normalizedPhrase(text);
}

BindingValidationResult BindingProcessor::validateRules(const QList<BindingRule> &rules)
{
    BindingValidationResult result;
    QHash<QString, int> seen;

    for (int row = 0; row < rules.size(); ++row) {
        const BindingRule &rule = rules.at(row);
        const QString phrase = rule.phrase.trimmed();
        const QStringList tokens = normalizedTokens(phrase);
        const QString normalized = tokens.join(QStringLiteral(" "));

        if (phrase.isEmpty() || tokens.isEmpty()) {
            result.issues.append({BindingValidationIssue::Type::EmptyPhrase,
                                  row,
                                  -1,
                                  validationMessage(BindingValidationIssue::Type::EmptyPhrase, row)});
            continue;
        }
        if (rule.replacement.trimmed().isEmpty()) {
            result.issues.append({BindingValidationIssue::Type::EmptyReplacement,
                                  row,
                                  -1,
                                  validationMessage(BindingValidationIssue::Type::EmptyReplacement, row)});
            continue;
        }
        if (seen.contains(normalized)) {
            const int duplicateOf = seen.value(normalized);
            result.issues.append({BindingValidationIssue::Type::DuplicatePhrase,
                                  row,
                                  duplicateOf,
                                  validationMessage(BindingValidationIssue::Type::DuplicatePhrase, row, duplicateOf)});
            continue;
        }

        seen.insert(normalized, row);
        result.rules.append({phrase, rule.replacement});
    }

    return result;
}

QList<BindingRule> BindingProcessor::parseJsonImport(const QByteArray &json, QString *error)
{
    if (error) {
        error->clear();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        }
        return {};
    }

    QList<BindingRule> rules;
    const auto appendArray = [&rules](const QJsonArray &array) {
        for (const QJsonValue &value : array) {
            const QJsonObject object = value.toObject();
            const QString phrase = object.value(QStringLiteral("phrase")).toString(
                object.value(QStringLiteral("trigger")).toString());
            const QString replacement = object.value(QStringLiteral("replacement")).toString(
                object.value(QStringLiteral("expansion")).toString(
                    object.value(QStringLiteral("text")).toString()));
            rules.append({phrase, replacement});
        }
    };

    if (document.isArray()) {
        appendArray(document.array());
    } else if (document.isObject()) {
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("snippets")).isArray()) {
            appendArray(object.value(QStringLiteral("snippets")).toArray());
        } else {
            for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
                if (iterator.value().isString()) {
                    rules.append({iterator.key(), iterator.value().toString()});
                }
            }
        }
    } else if (error) {
        *error = QStringLiteral("Snippet JSON must be an array or object.");
        return {};
    }

    const BindingValidationResult validation = validateRules(rules);
    if (!validation.ok()) {
        if (error) {
            *error = validation.messages().join(QStringLiteral("\n"));
        }
        return {};
    }
    return validation.rules;
}

QStringList BindingProcessor::refinementVocabulary(const QList<BindingRule> &rules)
{
    QStringList vocabulary;
    QSet<QString> seen;
    const QList<BindingRule> validRules = validateRules(rules).rules;

    for (const BindingRule &rule : validRules) {
        const QString phrase = rule.phrase.trimmed();
        const QString normalized = normalizedPhrase(phrase);
        for (const QString &candidate : {phrase, normalized}) {
            const QString term = candidate.simplified();
            const QString key = term.toCaseFolded();
            if (!term.isEmpty() && !seen.contains(key)) {
                seen.insert(key);
                vocabulary.append(term);
            }
        }
    }

    return vocabulary;
}

bool BindingProcessor::hasExplicitNoBindDirective(const QString &transcript)
{
    return NoBindDirectiveParser::hasDirective(transcript);
}

QStringList BindingProcessor::explicitNoBindPhrases(const QString &transcript, const QList<BindingRule> &rules)
{
    return NoBindDirectiveParser::excludedPhrases(transcript, validateRules(rules).rules);
}

BindingProcessingResult BindingProcessor::process(const QString &transcript, const QList<BindingRule> &rules)
{
    return BindingMatcher::process(transcript, rules);
}

QString BindingProcessor::applyBindingsOutsidePlaceholders(const QString &text, const QList<BindingRule> &rules)
{
    return BindingMatcher::applyOutsidePlaceholders(text, rules);
}

BindingRestoreResult BindingProcessor::restorePlaceholders(const QString &refinedText, const QList<BindingPlaceholder> &placeholders)
{
    return BindingMatcher::restorePlaceholders(refinedText, placeholders);
}

} // namespace speecher
