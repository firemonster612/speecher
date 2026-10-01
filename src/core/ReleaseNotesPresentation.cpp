#include "core/ReleaseNotesPresentation.h"

#include <QRegularExpression>
#include <QStringList>

namespace speecher {

QString releaseNotesForPlatform(const QString &markdown, const QString &platform)
{
    static const QRegularExpression platformPrefix(QStringLiteral("^- (Linux|macOS|Windows): "));
    // Each bullet with its nested bullets, which go with it. A bullet's
    // hard-wrapped lines join onto it; every other line stays as it was.
    QList<QStringList> blocks;
    for (const QString &line : markdown.split(u'\n')) {
        if (!line.startsWith(QStringLiteral("  ")) || blocks.isEmpty()
            || !blocks.last().first().startsWith(QStringLiteral("- "))) {
            blocks.append({line});
        } else if (line.trimmed().startsWith(QStringLiteral("- "))) {
            blocks.last().append(line);
        } else {
            blocks.last().last() += u' ' + line.trimmed();
        }
    }
    QStringList lines;
    for (QStringList &block : blocks) {
        const QRegularExpressionMatch match = platformPrefix.match(block.first());
        if (match.hasMatch()) {
            if (match.captured(1) != platform) {
                continue;
            }
            const QString text = block.first().mid(match.capturedLength());
            block.first() = QStringLiteral("- ") + text.left(1).toUpper() + text.mid(1);
        }
        lines.append(block);
    }
    // A heading whose section lost every bullet.
    QStringList kept;
    for (int index = 0; index < lines.size(); ++index) {
        const QString &line = lines.at(index);
        if (line.startsWith(u'#')) {
            int next = index + 1;
            while (next < lines.size() && lines.at(next).trimmed().isEmpty()) {
                ++next;
            }
            if (next == lines.size() || lines.at(next).startsWith(u'#')) {
                index = next - 1;
                continue;
            }
        }
        kept.append(line);
    }
    return kept.join(u'\n').trimmed();
}

QString releaseNotesForThisPlatform(const QString &markdown)
{
#if defined(Q_OS_MACOS)
    return releaseNotesForPlatform(markdown, QStringLiteral("macOS"));
#elif defined(Q_OS_WIN)
    return releaseNotesForPlatform(markdown, QStringLiteral("Windows"));
#else
    return releaseNotesForPlatform(markdown, QStringLiteral("Linux"));
#endif
}

} // namespace speecher
