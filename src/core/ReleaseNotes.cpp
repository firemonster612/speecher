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

// The commit a nightly's version names after "+g": "0.2.1-nightly.20260921+gabc1234".
QString versionCommit(const QString &version)
{
    static const QRegularExpression sha(QStringLiteral("[+]g([0-9a-fA-F]+)$"));
    return sha.match(version).captured(1);
}

// The history's commits after the one lastVersion was built from, or none
// when that commit is not in it.
QList<HistoryCommit> commitsSince(const QList<HistoryCommit> &history, const QString &lastVersion,
                                  const QString &currentVersion)
{
    const QString lastSha = versionCommit(lastVersion);
    if (!currentVersion.contains(QStringLiteral("-nightly")) || lastSha.isEmpty()
        || versionCommit(currentVersion).isEmpty()) {
        return {};
    }
    QList<HistoryCommit> commits;
    for (const HistoryCommit &commit : history) {
        // Short SHAs grow with the repository, so either recording may be the
        // longer one.
        if (commit.sha.startsWith(lastSha, Qt::CaseInsensitive)
            || lastSha.startsWith(commit.sha, Qt::CaseInsensitive)) {
            return commits;
        }
        commits.append(commit);
    }
    return {};
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

QList<ReleaseNoteSection> releaseNoteSections(const QString &markdown)
{
    static const QRegularExpression heading(QStringLiteral("^#+\\s*"));
    static const QRegularExpression bullet(QStringLiteral("^\\s*- "));
    QList<ReleaseNoteSection> sections;
    for (const QString &line : markdown.split(u'\n')) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        if (heading.match(line).hasMatch()) {
            sections.append({QString(line).remove(heading).trimmed(), {}});
            continue;
        }
        if (sections.isEmpty()) {
            sections.append(ReleaseNoteSection{});
        }
        sections.last().items.append(QString(line).remove(bullet).trimmed());
    }
    return sections;
}

WhatsNew whatsNew(const QList<ReleaseNote> &notes,
                  const QList<HistoryCommit> &history,
                  const QString &lastSeenVersion,
                  const QString &currentVersion)
{
    WhatsNew news;
    news.commits = commitsSince(history, lastSeenVersion, currentVersion);
    if (!lastSeenVersion.isEmpty()) {
        // A nightly's base is the last stable tag patch+1, so it predates the
        // Stable Release of its own base version. An update that moves past
        // that base crossed the release; its notes are news even though the
        // base versions compare equal.
        const bool crossedOwnBase = lastSeenVersion.contains(QStringLiteral("-nightly"))
            && compareBaseVersions(currentVersion, lastSeenVersion) > 0;
        for (const ReleaseNote &note : notes) {
            const int afterLastSeen = compareBaseVersions(note.version, lastSeenVersion);
            if ((afterLastSeen > 0 || (afterLastSeen == 0 && crossedOwnBase))
                && compareBaseVersions(note.version, currentVersion) <= 0) {
                news.releases.append(note);
            }
        }
    }
    // A nightly-to-nightly update that crosses no Stable Release reports its
    // own commits; only without those does the newest stable note stand in.
    if (news.releases.isEmpty() && news.commits.isEmpty()) {
        const auto newest = std::find_if(notes.cbegin(), notes.cend(), [&currentVersion](const ReleaseNote &note) {
            return compareBaseVersions(note.version, currentVersion) <= 0;
        });
        if (newest != notes.cend()) {
            news.releases.append(*newest);
        }
    }
    return news;
}

QString commitTitle(const HistoryCommit &commit)
{
    static const QRegularExpression mergedPullRequest(QStringLiteral("^Merge pull request #\\d+ from \\S+$"));
    const QString bodyTitle = commit.body.section(u'\n', 0, 0).trimmed();
    return mergedPullRequest.match(commit.subject).hasMatch() && !bodyTitle.isEmpty() ? bodyTitle : commit.subject;
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
