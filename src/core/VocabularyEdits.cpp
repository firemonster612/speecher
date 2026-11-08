#include "core/VocabularyEdits.h"

#include "core/BindingProcessor.h"
#include "core/Vocabulary.h"

#include <QSet>

#include <algorithm>

namespace speecher {

namespace {

bool sameText(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

QSet<QString> foldedSet(const QStringList &list)
{
    QSet<QString> folded;
    for (const QString &item : list) {
        folded.insert(item.toCaseFolded());
    }
    return folded;
}

// Two spellings of one term, or two replacements of one phrase under the
// same limits, which would compete.
bool clash(const VocabularyEntry &left, const VocabularyEntry &right)
{
    if (left.isReplacement() != right.isReplacement()) {
        return false;
    }
    if (!left.isReplacement()) {
        return sameText(left.term, right.term);
    }
    return BindingProcessor::normalizedPhrase(left.term) == BindingProcessor::normalizedPhrase(right.term)
        && foldedSet(left.profiles) == foldedSet(right.profiles) && foldedSet(left.apps) == foldedSet(right.apps);
}

// The list as the page shows it: tidied, one spelling per term.
QList<VocabularyEntry> &listed(AppSettings &settings)
{
    settings.vocabulary = normalizeVocabularyEntries(settings.vocabulary);
    return settings.vocabulary;
}

qsizetype indexOfId(const QList<VocabularyEntry> &entries, const QString &id)
{
    const auto found = std::find_if(entries.cbegin(), entries.cend(),
                                    [&id](const VocabularyEntry &entry) { return entry.id == id; });
    return found == entries.cend() ? -1 : found - entries.cbegin();
}

bool knownApp(const AppSettings &settings, const QString &limit)
{
    return !limit.startsWith(QStringLiteral("app:"))
        || std::any_of(settings.apps.apps.cbegin(), settings.apps.apps.cend(),
                       [&limit](const AppEntry &app) { return app.id == limit; });
}

EditRefusal check(VocabularyEntry &entry, qsizetype self, const AppSettings &settings)
{
    // Tidied as the list stores it, so the checks see what will be saved.
    entry = normalizeVocabularyEntries({entry}).value(0);
    entry.profiles = offeredVocabularyProfiles(entry.profiles, settings.refinement.writingProfiles);
    if (entry.term.isEmpty()) {
        return EditRefusal::Blank;
    }
    // A Learned Correction keeps text to write; emptied, it would turn into
    // a spelling of what was misheard.
    if (self >= 0 && settings.vocabulary.at(self).isLearned() && !entry.isReplacement()) {
        return EditRefusal::Incomplete;
    }
    if (entry.isReplacement() && !BindingProcessor::validateRules({{entry.term, entry.replacement}}).ok()) {
        return EditRefusal::Blank;
    }
    if (!std::all_of(entry.apps.cbegin(), entry.apps.cend(),
                     [&settings](const QString &limit) { return knownApp(settings, limit); })) {
        return EditRefusal::Missing;
    }
    for (qsizetype i = 0; i < settings.vocabulary.size(); ++i) {
        if (i != self && clash(settings.vocabulary.at(i), entry)) {
            return EditRefusal::Taken;
        }
    }
    return EditRefusal::None;
}

} // namespace

EditRefusal addVocabularyEntry(AppSettings &settings, VocabularyEntry entry)
{
    entry.id.clear();
    entry = withVocabularyIds({entry}).first();
    return appendEntry(listed(settings), entry,
                       [&settings](VocabularyEntry &added, qsizetype self) { return check(added, self, settings); });
}

EditRefusal editVocabularyEntry(AppSettings &settings, const QString &id, const EntryChange<VocabularyEntry> &change)
{
    QList<VocabularyEntry> &entries = listed(settings);
    return changeEntry(entries, indexOfId(entries, id), change,
                       [&settings](VocabularyEntry &entry, qsizetype self) { return check(entry, self, settings); });
}

SettingsEdit removeVocabularyEntry(AppSettings &settings, const QString &id)
{
    QList<VocabularyEntry> &entries = listed(settings);
    const qsizetype index = indexOfId(entries, id);
    if (index < 0) {
        return {};
    }
    const auto putBack = takeEntry(entries, index, [](const VocabularyEntry &entry, const VocabularyEntry &removed) {
        return entry.id == removed.id || clash(entry, removed);
    });
    return [putBack](AppSettings &draft) { putBack(listed(draft)); };
}

int importVocabularyEntries(AppSettings &settings, const QList<VocabularyEntry> &incoming)
{
    QList<VocabularyEntry> spellings;
    int added = 0;
    for (const VocabularyEntry &entry : incoming) {
        if (entry.isReplacement()) {
            added += addVocabularyEntry(settings, entry) == EditRefusal::None;
        } else {
            spellings.append(entry);
        }
    }
    const qsizetype before = listed(settings).size();
    settings.vocabulary = withVocabularyIds(normalizeVocabularyEntries(settings.vocabulary + spellings));
    return added + int(settings.vocabulary.size() - before);
}

void keepVocabularyLimitsOf(AppSettings &settings, const AppEntry &removed)
{
    QStringList ids;
    for (const AppMatch &match : removed.matches) {
        if (match.kind == AppMatchKind::Id) {
            ids.append(match.value);
        }
    }
    for (VocabularyEntry &entry : settings.vocabulary) {
        if (entry.apps.removeAll(removed.id) > 0) {
            entry.apps += ids;
            entry.apps.removeDuplicates();
        }
    }
}

} // namespace speecher
