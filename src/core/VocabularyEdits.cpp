#include "core/VocabularyEdits.h"

#include "core/BindingProcessor.h"
#include "core/Vocabulary.h"

#include <algorithm>

namespace speecher {

namespace {

bool sameText(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

// The index of the first entry of list `matches` picks, or -1.
template <typename T, typename Matches>
qsizetype indexWhere(const QList<T> &list, Matches matches)
{
    const auto found = std::find_if(list.cbegin(), list.cend(), matches);
    return found == list.cend() ? -1 : found - list.cbegin();
}

// Whether an entry of list but the one at `self` is `same` as entry.
template <typename T, typename Same>
bool takenByAnother(const QList<T> &list, qsizetype self, const T &entry, Same same)
{
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (i != self && same(list.at(i), entry)) {
            return true;
        }
    }
    return false;
}

// The terms as the page lists them: tidied, one per text, in send order.
QList<VocabularyEntry> &listedTerms(AppSettings &settings)
{
    settings.vocabulary = normalizeVocabularyEntries(settings.vocabulary);
    return settings.vocabulary;
}

bool sameTerm(const VocabularyEntry &left, const VocabularyEntry &right)
{
    return sameText(left.term, right.term);
}

qsizetype indexOfTerm(const QList<VocabularyEntry> &terms, const QString &term)
{
    return indexWhere(terms, [text = term.simplified()](const VocabularyEntry &entry) { return sameText(entry.term, text); });
}

EditRefusal checkTerm(VocabularyEntry &entry, qsizetype self, const AppSettings &settings)
{
    entry.term = entry.term.simplified();
    entry.context = entry.context.trimmed();
    entry.profiles = offeredVocabularyProfiles(entry.profiles, settings.refinement.writingProfiles);
    if (entry.term.isEmpty()) {
        return EditRefusal::Blank;
    }
    return takenByAnother(settings.vocabulary, self, entry, sameTerm) ? EditRefusal::Taken : EditRefusal::None;
}

// Two corrections of the same words in the same app would compete.
bool sameCorrection(const LearnedCorrection &left, const LearnedCorrection &right)
{
    return sameText(left.original, right.original) && sameText(left.applicationId, right.applicationId);
}

EditRefusal checkCorrection(LearnedCorrection &correction, qsizetype self, const QList<LearnedCorrection> &corrections)
{
    correction.original = correction.original.trimmed();
    correction.corrected = correction.corrected.trimmed();
    if (correction.original.isEmpty()) {
        return EditRefusal::Blank;
    }
    if (correction.corrected.isEmpty()) {
        return EditRefusal::Incomplete;
    }
    return takenByAnother(corrections, self, correction, sameCorrection) ? EditRefusal::Taken : EditRefusal::None;
}

bool samePhrase(const BindingRule &left, const BindingRule &right)
{
    return BindingProcessor::normalizedPhrase(left.phrase) == BindingProcessor::normalizedPhrase(right.phrase);
}

EditRefusal checkReplacement(BindingRule &rule, qsizetype self, const QList<BindingRule> &rules)
{
    rule.phrase = rule.phrase.trimmed();
    // What the dictation's replacements check on each rule.
    const BindingValidationResult alone = BindingProcessor::validateRules({rule});
    if (!alone.ok()) {
        return alone.issues.first().type == BindingValidationIssue::Type::EmptyPhrase ? EditRefusal::Blank
                                                                                        : EditRefusal::Incomplete;
    }
    return takenByAnother(rules, self, rule, samePhrase) ? EditRefusal::Taken : EditRefusal::None;
}

qsizetype indexOfPhrase(const QList<BindingRule> &rules, const QString &phrase)
{
    return indexWhere(rules, [&phrase](const BindingRule &rule) { return samePhrase(rule, {phrase, {}}); });
}

} // namespace

EditRefusal addVocabularyEntry(AppSettings &settings, VocabularyEntry entry)
{
    return appendEntry(listedTerms(settings), entry, [&settings](VocabularyEntry &added, qsizetype self) {
        return checkTerm(added, self, settings);
    });
}

EditRefusal editVocabularyEntry(AppSettings &settings, const QString &term, const EntryChange<VocabularyEntry> &change)
{
    QList<VocabularyEntry> &terms = listedTerms(settings);
    return changeEntry(terms, indexOfTerm(terms, term), change,
                       [&settings](VocabularyEntry &entry, qsizetype self) { return checkTerm(entry, self, settings); });
}

SettingsEdit removeVocabularyEntry(AppSettings &settings, const QString &term)
{
    QList<VocabularyEntry> &terms = listedTerms(settings);
    const qsizetype index = indexOfTerm(terms, term);
    if (index < 0) {
        return {};
    }
    const auto putBack = takeEntry(terms, index, sameTerm);
    return [putBack](AppSettings &draft) { putBack(listedTerms(draft)); };
}

int importVocabularyEntries(AppSettings &settings, const QList<VocabularyEntry> &incoming)
{
    const qsizetype listed = listedTerms(settings).size();
    settings.vocabulary = normalizeVocabularyEntries(settings.vocabulary + incoming);
    return int(settings.vocabulary.size() - listed);
}

EditRefusal editLearnedCorrection(AppSettings &settings, const QString &id, const EntryChange<LearnedCorrection> &change)
{
    QList<LearnedCorrection> &corrections = settings.learnedCorrections;
    const qsizetype index = indexWhere(corrections, [&id](const LearnedCorrection &correction) { return correction.id == id; });
    return changeEntry(corrections, index, change, [&corrections](LearnedCorrection &correction, qsizetype self) {
        return checkCorrection(correction, self, corrections);
    });
}

SettingsEdit removeLearnedCorrection(AppSettings &settings, const QString &id)
{
    QList<LearnedCorrection> &corrections = settings.learnedCorrections;
    const qsizetype index = indexWhere(corrections, [&id](const LearnedCorrection &correction) { return correction.id == id; });
    if (index < 0) {
        return {};
    }
    const auto putBack = takeEntry(corrections, index, [](const LearnedCorrection &entry, const LearnedCorrection &removed) {
        return entry.id == removed.id || sameCorrection(entry, removed);
    });
    return [putBack](AppSettings &draft) { putBack(draft.learnedCorrections); };
}

EditRefusal addBindingRule(AppSettings &settings, BindingRule rule)
{
    QList<BindingRule> &rules = settings.bindings;
    return appendEntry(rules, rule, [&rules](BindingRule &added, qsizetype self) { return checkReplacement(added, self, rules); });
}

EditRefusal editBindingRule(AppSettings &settings, const QString &phrase, const EntryChange<BindingRule> &change)
{
    QList<BindingRule> &rules = settings.bindings;
    return changeEntry(rules, indexOfPhrase(rules, phrase), change,
                       [&rules](BindingRule &rule, qsizetype self) { return checkReplacement(rule, self, rules); });
}

SettingsEdit removeBindingRule(AppSettings &settings, const QString &phrase)
{
    const qsizetype index = indexOfPhrase(settings.bindings, phrase);
    if (index < 0) {
        return {};
    }
    const auto putBack = takeEntry(settings.bindings, index, samePhrase);
    return [putBack](AppSettings &draft) { putBack(draft.bindings); };
}

int importBindingRules(AppSettings &settings, const QList<BindingRule> &incoming)
{
    return int(std::count_if(incoming.cbegin(), incoming.cend(), [&settings](const BindingRule &rule) {
        return addBindingRule(settings, rule) == EditRefusal::None;
    }));
}

} // namespace speecher
