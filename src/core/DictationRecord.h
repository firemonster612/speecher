#pragma once

#include "core/Target.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace speecher {

// What one finished Dictation Session leaves behind for the stats on Home.
// Never the text or the audio.
struct DictationRecord {
    QDateTime finishedAt;
    int audioMs = 0;
    int words = 0;
    QString appName;
    QString profile = WritingProfile::Other;
    // A custom profile's name when the record was made, so it still reads
    // after the profile is deleted. Empty for a built-in.
    QString profileName;
    // The providers that ran, by id, in the order they ran: more than one
    // when a fallback stood in. Empty in records from before they were kept.
    QStringList speechProviders;
    QStringList refinementProviders;
};

// Words as a reader counts them: word-boundary segments that contain a letter
// or number, so punctuation is not a word and unspaced scripts still count.
int countWords(const QString &text);

} // namespace speecher
