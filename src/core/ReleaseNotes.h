#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace speecher {

// The release notes and commit history every build embeds, read as data, and
// which of them tell someone what changed since the version they last ran.
// The Updates page words them.

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

// One heading of a release's notes and the bullets under it, such as "Added"
// and what was added. Notes before any heading have an empty title.
struct ReleaseNoteSection {
    QString title;
    QStringList items;
};
// A release's notes, as releaseNotesForPlatform leaves them, split at their
// headings. A nested bullet is an item of its own; each item is one line of
// the source's Markdown with its bullet marker taken off.
QList<ReleaseNoteSection> releaseNoteSections(const QString &markdown);

// What changed for someone who last ran lastSeenVersion and now runs
// currentVersion: what the What's New offer after an update shows.
struct WhatsNew {
    // A nightly's commits since the nightly it replaced, newest first. Empty
    // for a stable build, or when that nightly's commit is older than the
    // history the build embedded.
    QList<HistoryCommit> commits;
    // The Stable Releases crossed, newest first. With nothing crossed and no
    // commits, the newest release at or before currentVersion, so there is
    // always something to read when a release's notes exist.
    QList<ReleaseNote> releases;
};
// lastSeenVersion is empty when no update happened; notes come from
// bundledReleaseNotes() and history from embeddedHistory().
WhatsNew whatsNew(const QList<ReleaseNote> &notes,
                  const QList<HistoryCommit> &history,
                  const QString &lastSeenVersion,
                  const QString &currentVersion);

// What a commit is called in a list of changes: its subject, or for a merge
// commit, whose subject names only a branch, the pull request's title on the
// first line of its body.
QString commitTitle(const HistoryCommit &commit);

// Nightly metadata does not make a new Stable Release, and dotted components
// are numbers rather than text (0.10 follows 0.2). Negative, zero or positive
// as left is older than, the same release as, or newer than right.
int compareBaseVersions(const QString &left, const QString &right);

} // namespace speecher
