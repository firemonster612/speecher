#pragma once

// Vocabulary Entries as tests write them: a replacement or a Learned
// Correction in one call, and the entries of one kind a settings list holds.

#include "core/AppSettings.h"

#include <algorithm>

namespace speecher::test {

inline VocabularyEntry replacementEntry(const QString &phrase, const QString &text)
{
    VocabularyEntry entry{phrase};
    entry.replacement = text;
    return entry;
}

// Learned in `app`, or in several apps for none.
inline VocabularyEntry learnedEntry(const QString &heard, const QString &written, const QString &app = {},
                                    const QString &id = {})
{
    VocabularyEntry entry = replacementEntry(heard, written);
    entry.source = QStringLiteral("learned");
    entry.id = id;
    entry.confidence = 0.95;
    entry.evidenceCount = 1;
    if (!app.isEmpty()) {
        entry.apps = {app};
    }
    return entry;
}

inline QList<VocabularyEntry> learnedEntries(const QList<VocabularyEntry> &vocabulary)
{
    QList<VocabularyEntry> learned;
    std::copy_if(vocabulary.cbegin(), vocabulary.cend(), std::back_inserter(learned),
                 [](const VocabularyEntry &entry) { return entry.isLearned(); });
    return learned;
}

// The person's own replacements, not the learned ones.
inline QList<VocabularyEntry> replacementEntries(const QList<VocabularyEntry> &vocabulary)
{
    QList<VocabularyEntry> replacements;
    std::copy_if(vocabulary.cbegin(), vocabulary.cend(), std::back_inserter(replacements),
                 [](const VocabularyEntry &entry) { return entry.isReplacement() && !entry.isLearned(); });
    return replacements;
}

inline QList<VocabularyEntry> spellingEntries(const QList<VocabularyEntry> &vocabulary)
{
    QList<VocabularyEntry> spellings;
    std::copy_if(vocabulary.cbegin(), vocabulary.cend(), std::back_inserter(spellings),
                 [](const VocabularyEntry &entry) { return !entry.isReplacement(); });
    return spellings;
}

} // namespace speecher::test
