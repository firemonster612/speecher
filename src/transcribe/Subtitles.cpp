#include "transcribe/Subtitles.h"

#include <QStringList>

#include <algorithm>

namespace speecher {
namespace {

// Two lines of 42 characters, the usual limit for one subtitle on screen.
constexpr qsizetype kMaxCueChars = 84;

// text cut after each of marks that a space follows.
QStringList splitAfter(const QString &text, QStringView marks)
{
    QStringList pieces;
    qsizetype start = 0;
    for (qsizetype i = 0; i + 1 < text.size(); ++i) {
        if (marks.contains(text.at(i)) && text.at(i + 1).isSpace()) {
            pieces << text.mid(start, i + 1 - start).trimmed();
            start = i + 1;
        }
    }
    pieces << text.mid(start).trimmed();
    pieces.removeAll(QString());
    return pieces;
}

// One segment's cue texts: the segment whole when it fits, otherwise its
// sentences, a sentence still too long cut at its commas, and the pieces
// joined back together while they fit.
QStringList cueTexts(const QString &text)
{
    if (text.size() <= kMaxCueChars) {
        return {text};
    }
    QStringList pieces;
    for (const QString &sentence : splitAfter(text, u".?!…")) {
        pieces << (sentence.size() <= kMaxCueChars ? QStringList{sentence} : splitAfter(sentence, u",;:"));
    }
    QStringList cues;
    for (const QString &piece : std::as_const(pieces)) {
        if (!cues.isEmpty() && cues.last().size() + 1 + piece.size() <= kMaxCueChars) {
            cues.last() += QLatin1Char(' ') + piece;
        } else {
            cues << piece;
        }
    }
    return cues;
}

// Each piece of a split segment gets the share of its time that its share of
// the text is.
QList<TranscriptSegment> cues(const QList<TranscriptSegment> &segments)
{
    QList<TranscriptSegment> result;
    for (const TranscriptSegment &segment : segments) {
        if (segment.text.isEmpty()) {
            continue;
        }
        const QStringList texts = cueTexts(segment.text);
        qsizetype total = 0;
        for (const QString &text : texts) {
            total += text.size();
        }
        const qint64 duration = std::max<qint64>(0, segment.endMs - segment.startMs);
        qsizetype before = 0;
        for (const QString &text : texts) {
            const qint64 startMs = segment.startMs + duration * before / total;
            before += text.size();
            result.append({startMs, segment.startMs + duration * before / total, text});
        }
    }
    return result;
}

// "01:02:03,456" for SRT, "01:02:03.456" for WebVTT.
QString timestamp(qint64 ms, QChar decimalMark)
{
    const QChar zero = QLatin1Char('0');
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(ms / 3600000, 2, 10, zero)
        .arg(ms / 60000 % 60, 2, 10, zero)
        .arg(ms / 1000 % 60, 2, 10, zero)
        .arg(decimalMark)
        .arg(ms % 1000, 3, 10, zero);
}

// WebVTT cue text is markup, so these three would be read as tags or
// entities; "-->" cannot appear once ">" is escaped.
QString escapedForWebVtt(QString text)
{
    return text.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
        .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
        .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
}

} // namespace

QString transcriptFileExtension(TranscriptFormat format)
{
    switch (format) {
    case TranscriptFormat::Text:
        break;
    case TranscriptFormat::Srt:
        return QStringLiteral("srt");
    case TranscriptFormat::WebVtt:
        return QStringLiteral("vtt");
    }
    return QStringLiteral("txt");
}

QString subtitleFile(const QList<TranscriptSegment> &segments, TranscriptFormat format)
{
    const bool webVtt = format == TranscriptFormat::WebVtt;
    const QChar decimalMark = webVtt ? QLatin1Char('.') : QLatin1Char(',');
    QStringList blocks;
    if (webVtt) {
        blocks << QStringLiteral("WEBVTT");
    }
    const QList<TranscriptSegment> all = cues(segments);
    for (qsizetype index = 0; index < all.size(); ++index) {
        const TranscriptSegment &cue = all.at(index);
        const QString timing = QStringLiteral("%1 --> %2")
                                   .arg(timestamp(cue.startMs, decimalMark), timestamp(cue.endMs, decimalMark));
        blocks << (webVtt ? timing + QLatin1Char('\n') + escapedForWebVtt(cue.text)
                          : QStringLiteral("%1\n%2\n%3").arg(index + 1).arg(timing, cue.text));
    }
    return blocks.join(QStringLiteral("\n\n"));
}

} // namespace speecher
