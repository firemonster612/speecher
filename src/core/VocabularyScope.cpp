#include "core/VocabularyScope.h"

#include "core/Vocabulary.h"
#include "core/VocabularyLimit.h"

#include <QLocale>
#include <QSet>

#include <algorithm>

namespace speecher {
namespace {

bool sameText(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

bool appLimitMatches(const QString &limit, const VocabularyScope &scope)
{
    if (!limit.startsWith(QStringLiteral("app:"))) {
        return !scope.target.applicationId.isEmpty() && sameText(limit, scope.target.applicationId);
    }
    // An App removed meanwhile matches nothing (removeApp rewrites the limits
    // it can, and leaves the rest on it).
    const auto app = std::find_if(scope.apps.apps.cbegin(), scope.apps.apps.cend(),
                                  [&limit](const AppEntry &entry) { return entry.id == limit; });
    return app != scope.apps.apps.cend()
        && std::any_of(app->matches.cbegin(), app->matches.cend(),
                       [&scope](const AppMatch &match) { return appMatches(match, scope.target); });
}

bool listsSpelling(const QList<VocabularyEntry> &entries, const QString &term)
{
    return std::any_of(entries.cbegin(), entries.cend(), [text = term.simplified()](const VocabularyEntry &entry) {
        return !entry.isReplacement() && sameText(entry.term, text);
    });
}

// Whether a spelling of `term` is limited away from here, which keeps it out
// even as a Learned Correction's text.
bool spellingExcluded(const QList<VocabularyEntry> &entries, const QString &term, const VocabularyScope &scope)
{
    return std::any_of(entries.cbegin(), entries.cend(), [&](const VocabularyEntry &entry) {
        return !entry.isReplacement() && sameText(entry.term, term.simplified())
            && !vocabularyEntryApplies(entry, scope);
    });
}

// How narrowly a replacement is limited, narrowest first.
int breadth(const VocabularyEntry &entry)
{
    return !entry.apps.isEmpty() ? 0 : !entry.profiles.isEmpty() ? 1 : 2;
}

QString withVariablesFilled(QString text, const QDateTime &now)
{
    return text.replace(QStringLiteral("{date}"), QLocale().toString(now.date(), QLocale::ShortFormat))
        .replace(QStringLiteral("{time}"), QLocale().toString(now.time(), QLocale::ShortFormat));
}

} // namespace

bool vocabularyEntryApplies(const VocabularyEntry &entry, const VocabularyScope &scope)
{
    if (!entry.enabled) {
        return false;
    }
    if (scope.writingProfile.isEmpty()) {
        return true;
    }
    return (entry.profiles.isEmpty() || entry.profiles.contains(scope.writingProfile))
        && (entry.apps.isEmpty() || std::any_of(entry.apps.cbegin(), entry.apps.cend(), [&scope](const QString &limit) {
                return appLimitMatches(limit, scope);
            }));
}

QStringList speechVocabulary(const QList<VocabularyEntry> &entries,
                             const VocabularyScope &scope,
                             const QStringList &leadingTerms)
{
    const QList<VocabularyEntry> normalized = normalizeVocabularyEntries(entries);
    // Every term in send order, capped once at the end: capping the saved
    // ones first would pack them without the leading terms ahead of them.
    QStringList terms;
    QSet<QString> seen;
    const auto append = [&terms, &seen](const QString &term) {
        const QString cleaned = term.simplified();
        if (!seen.contains(cleaned.toCaseFolded())) {
            seen.insert(cleaned.toCaseFolded());
            terms.append(cleaned);
        }
    };
    for (const QString &term : leadingTerms) {
        append(term);
    }
    for (const VocabularyEntry &entry : normalized) {
        if (!entry.isReplacement() && entry.keyTerm && vocabularyEntryApplies(entry, scope)) {
            append(entry.term);
        }
    }
    // Corrections sit last, so an over-cap list drops them before any term
    // the person typed. One whose text is a listed spelling adds nothing:
    // that entry's own limits and Key term decide whether it goes.
    for (const VocabularyEntry &entry : normalized) {
        if (entry.isLearned() && vocabularyEntryApplies(entry, scope) && !listsSpelling(normalized, entry.replacement)) {
            append(entry.replacement);
        }
    }
    return VocabularyLimit::limited(terms);
}

QStringList refinementVocabulary(const QList<VocabularyEntry> &entries,
                                 const VocabularyScope &scope,
                                 const QStringList &runTerms,
                                 QHash<QString, QString> &context)
{
    // The spelling kept for each term, by its case-folded form, so a context
    // follows its term onto whichever spelling won.
    QHash<QString, QString> kept;
    QStringList deduplicated;
    const auto append = [&kept, &deduplicated](const QString &term) {
        const QString cleaned = term.simplified();
        const QString key = cleaned.toCaseFolded();
        if (cleaned.isEmpty() || kept.contains(key)) {
            return kept.value(key);
        }
        if (deduplicated.size() >= VocabularyLimit::maxRefinementTerms) {
            return QString();
        }
        kept.insert(key, cleaned);
        deduplicated.append(cleaned);
        return cleaned;
    };
    const QList<VocabularyEntry> normalized = normalizeVocabularyEntries(entries);
    for (const VocabularyEntry &entry : normalized) {
        if (entry.isLearned() && vocabularyEntryApplies(entry, scope)
            && !spellingExcluded(normalized, entry.replacement, scope)) {
            append(entry.replacement);
        }
    }
    for (const QString &term : runTerms) {
        append(term);
    }
    for (const VocabularyEntry &entry : normalized) {
        if (!entry.isReplacement() && vocabularyEntryApplies(entry, scope)) {
            const QString spelling = append(entry.term);
            if (!spelling.isEmpty() && !entry.context.isEmpty()) {
                context.insert(spelling, entry.context);
            }
        }
    }
    return deduplicated;
}

QList<BindingRule> replacementRules(const QList<VocabularyEntry> &entries,
                                    const VocabularyScope &scope,
                                    const QDateTime &now)
{
    const QList<VocabularyEntry> normalized = normalizeVocabularyEntries(entries);
    QList<VocabularyEntry> applying;
    std::copy_if(normalized.cbegin(), normalized.cend(), std::back_inserter(applying), [&](const VocabularyEntry &entry) {
        return entry.isReplacement() && vocabularyEntryApplies(entry, scope)
            && !(entry.isLearned() && spellingExcluded(normalized, entry.replacement, scope));
    });
    std::stable_sort(applying.begin(), applying.end(), [](const VocabularyEntry &left, const VocabularyEntry &right) {
        if (left.isLearned() != right.isLearned()) {
            return right.isLearned();
        }
        return breadth(left) < breadth(right);
    });
    QList<BindingRule> rules;
    QSet<QString> claimedPhrases;
    for (const VocabularyEntry &entry : applying) {
        const QString phrase = BindingProcessor::normalizedPhrase(entry.term);
        if (phrase.isEmpty() || claimedPhrases.contains(phrase)) {
            continue;
        }
        claimedPhrases.insert(phrase);
        rules.append({entry.term, entry.isLearned() ? entry.replacement : withVariablesFilled(entry.replacement, now)});
    }
    return rules;
}

} // namespace speecher
