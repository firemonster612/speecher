#pragma once

#include "core/Target.h"

#include <QDateTime>
#include <QString>

namespace speecher {

// What one finished Dictation Session leaves behind for the stats on Home.
// Never the text or the audio.
struct DictationRecord {
    QDateTime finishedAt;
    int audioMs = 0;
    int words = 0;
    QString appName;
    WritingProfile profile = WritingProfile::Other;
};

// Words as a reader counts them: word-boundary segments that contain a letter
// or number, so punctuation is not a word and unspaced scripts still count.
int countWords(const QString &text);

} // namespace speecher
