#include "core/ReleaseNotesPresentation.h"

#include <QRegularExpression>
#include <QStringList>

namespace speecher {

QString releaseNotesForPlatform(const QString &markdown, const QString &platform)
{
    static const QRegularExpression platformPrefix(QStringLiteral("^- (Linux|macOS|Windows): "));
    const QStringList lines = markdown.split(u'\n');
    // Bullets joined to one line each; every other line as it was.
    QStringList blocks;
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("  ")) && !blocks.isEmpty()
            && blocks.last().startsWith(QStringLiteral("- "))) {
            blocks.last() += u' ' + line.trimmed();
        } else {
            blocks.append(line);
        }
    }
    blocks.removeIf([&platform](const QString &block) {
        const QRegularExpressionMatch match = platformPrefix.match(block);
        return match.hasMatch() && match.captured(1) != platform;
    });
    // A heading whose section lost every bullet.
    QStringList kept;
    for (int index = 0; index < blocks.size(); ++index) {
        const QString &block = blocks.at(index);
        if (block.startsWith(u'#')) {
            int next = index + 1;
            while (next < blocks.size() && blocks.at(next).trimmed().isEmpty()) {
                ++next;
            }
            if (next == blocks.size() || blocks.at(next).startsWith(u'#')) {
                index = next - 1;
                continue;
            }
        }
        kept.append(block);
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
