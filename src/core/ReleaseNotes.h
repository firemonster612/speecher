#pragma once

#include <QList>
#include <QString>

namespace speecher {

// The release notes and commit history every build embeds, read as data. The
// Updates page decides which of them to show and how to word them.

// One Stable Release's notes, docs/releases/<version>.md, as Markdown.
struct ReleaseNote {
    QString version;
    QString markdown;
};
// Every bundled release's notes filtered to the platform this build runs on,
// newest first.
QList<ReleaseNote> bundledReleaseNotes();

// One first-parent commit: its short SHA, subject and body.
struct HistoryCommit {
    QString sha;
    QString subject;
    QString body;
};
// The recent first-parent history the build embedded, newest first, so a
// nightly can list the commits since the one before it. Empty for a build made
// outside a git checkout.
QList<HistoryCommit> embeddedHistory();
// Reads cmake/GitVersion.cmake's history format: records separated by 0x1e,
// each the SHA, subject and body separated by 0x1f.
QList<HistoryCommit> parseHistory(const QString &log);

// One release's notes as `platform` ("Linux", "macOS" or "Windows") shows
// them: a bullet that starts with another platform's name and a colon is left
// out with its nested bullets, one that starts with this platform's name loses
// that prefix, a heading left with no bullets under it goes too, and each
// bullet's hard-wrapped lines are joined into one so it wraps to the page
// rather than to the source file.
QString releaseNotesForPlatform(const QString &markdown, const QString &platform);

// Nightly metadata does not make a new Stable Release, and dotted components
// are numbers rather than text (0.10 follows 0.2). Negative, zero or positive
// as left is older than, the same release as, or newer than right.
int compareBaseVersions(const QString &left, const QString &right);

} // namespace speecher
