#include "core/ReleaseNotes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

// Outside the namespace: Q_INIT_RESOURCE declares the resource's initializer
// at the scope it is used in, and the resource defines it globally.
static void initializeReleaseNotesResources()
{
    Q_INIT_RESOURCE(release_notes);
    Q_INIT_RESOURCE(release_history);
}

namespace speecher {

namespace {

void ensureResourcesInitialized()
{
    static const bool initialized = [] {
        initializeReleaseNotesResources();
        return true;
    }();
    Q_UNUSED(initialized);
}

QString thisPlatform()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#else
    return QStringLiteral("Linux");
#endif
}

} // namespace

QList<ReleaseNote> bundledReleaseNotes()
{
    ensureResourcesInitialized();
    QList<ReleaseNote> notes;
    const QDir directory(QStringLiteral(":/releases"));
    for (const QString &fileName : directory.entryList({QStringLiteral("*.md")}, QDir::Files)) {
        QFile file(directory.filePath(fileName));
        // Text mode: a Windows checkout gives the bundled notes CRLF line ends.
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            notes.append({QFileInfo(fileName).completeBaseName(),
                          releaseNotesForPlatform(QString::fromUtf8(file.readAll()), thisPlatform())});
        }
    }
    std::sort(notes.begin(), notes.end(), [](const ReleaseNote &left, const ReleaseNote &right) {
        return compareBaseVersions(left.version, right.version) > 0;
    });
    return notes;
}

QList<HistoryCommit> embeddedHistory()
{
    ensureResourcesInitialized();
    QFile file(QStringLiteral(":/releases/history.log"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return parseHistory(QString::fromUtf8(file.readAll()));
}

QList<HistoryCommit> parseHistory(const QString &log)
{
    QList<HistoryCommit> commits;
    for (const QString &record : log.split(QChar(0x1e), Qt::SkipEmptyParts)) {
        const QStringList fields = record.split(QChar(0x1f));
        if (fields.size() < 2) {
            continue;
        }
        commits.append({fields.at(0).trimmed(), fields.at(1).trimmed(), fields.value(2).trimmed()});
    }
    return commits;
}

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

int compareBaseVersions(const QString &left, const QString &right)
{
    static const QRegularExpression leadingDigits(QStringLiteral("^\\d+"));
    const auto component = [](const QStringList &parts, int index) {
        if (index >= parts.size()) {
            return 0;
        }
        return leadingDigits.match(parts.at(index)).captured().toInt();
    };
    const QStringList leftParts = left.section(QLatin1Char('-'), 0, 0).split(QLatin1Char('.'));
    const QStringList rightParts = right.section(QLatin1Char('-'), 0, 0).split(QLatin1Char('.'));
    for (int index = 0; index < qMax(leftParts.size(), rightParts.size()); ++index) {
        const int leftPart = component(leftParts, index);
        const int rightPart = component(rightParts, index);
        if (leftPart != rightPart) {
            return leftPart < rightPart ? -1 : 1;
        }
    }
    return 0;
}

} // namespace speecher
