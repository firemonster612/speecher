#pragma once

#include "core/DeliverySteps.h"
#include "core/Target.h"
#include "core/history/HistoryEntry.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Apps and App Types: what the person set for an app or a kind of app, and the
// one resolver that says which App Type, Writing Profile and Pasting choice a
// Target gets. The built-in catalog of common apps and the terminal and AI
// coding detection that reads it live here too.

namespace speecher {

enum class AppMatchKind {
    // The app's id, exactly: compared case-folded with a ".desktop" or ".exe"
    // ending dropped, against the Target's application id and process name.
    // The installed-app picker and Switch to it make these.
    Id,
    // Contained in the Target's application id, name, process name or role,
    // ignoring case, or once only letters and digits are kept.
    Name,
    // Contained in the window title, the same way. For web apps, such as
    // Gmail in a browser. A window title says what a window shows, not which
    // program it is, so a title match never sets the App Type.
    WindowTitle,
};

struct AppMatch {
    AppMatchKind kind = AppMatchKind::Id;
    QString value;

    bool operator==(const AppMatch &other) const = default;
};

// One app the person set something for. Each value left unset follows its
// App Type ("Same as …").
struct AppEntry {
    // "app:" and a random part; see newAppId.
    QString id;
    // As the picker showed it.
    QString name;
    // An icon key from InstalledApps; empty draws the name's initial.
    QString icon;
    // Any one matching is enough.
    QList<AppMatch> matches;
    // Unset: the built-in catalog and the terminal and AI coding detection
    // decide (Automatic).
    std::optional<AppCategory> type;
    std::optional<QString> writingProfile;
    // Its own steps and paste keys; each part unset follows Terminals' for
    // anything running in a terminal, then its App Type's.
    PastingChoice pasting;
    // What History keeps of its dictations: Stats or Nothing, which can only
    // lower What Speecher Keeps. Unset follows its App Type.
    std::optional<HistoryKeep> history;
    // Its type doesn't stop the built-in catalog choosing its profile. Only
    // migration sets it, for an older recognition rule that set only a type,
    // which left the profile to the catalog; an App the person types picks
    // its type's profile.
    bool catalogPicksProfile = false;

    bool operator==(const AppEntry &other) const = default;
};

// What one App Type sets. General is "Other apps", whose profile is the one
// any app Speecher doesn't recognize gets.
struct AppTypeSettings {
    AppCategory type = AppCategory::General;
    // Unset: Other apps' profile (for Other apps itself, the Other profile).
    std::optional<QString> writingProfile;
    // Each part unset: the Pasting page's steps, and the usual paste keys.
    PastingChoice pasting;
    // As AppEntry::history. Unset: Other apps' (for Other apps itself, What
    // Speecher Keeps).
    std::optional<HistoryKeep> history;

    bool operator==(const AppTypeSettings &other) const = default;
};

// The App Types an app can have, in the order the UI lists them, Other apps
// last.
QList<AppCategory> appTypes();
// Each of appTypes() with what the code always gave it: Terminals, Code
// editors and Office apps write with Work, Email apps with Email, AI coding
// apps with AI coding; Terminals press their own paste keys where those
// differ.
QList<AppTypeSettings> defaultAppTypeSettings();

struct AppsSettings {
    QList<AppEntry> apps;
    // One for each of appTypes(), in that order.
    QList<AppTypeSettings> types = defaultAppTypeSettings();

    bool operator==(const AppsSettings &other) const = default;
};

// Where a resolved value came from, so the UI can say "Same as Email apps".
enum class AppLayer {
    // An AppEntry of the person's.
    App,
    // The built-in catalog: a profile it gives an app of no type of its own,
    // such as Slack's Work.
    BuiltIn,
    // Terminals' Pasting choice, which anything running in a terminal takes.
    Terminal,
    // The Target's App Type.
    Type,
    // Other apps' profile, or the steps as they are.
    Fallback,
};

// Each value's ...From says which layer set it, and where that is an App,
// ...App the id of the one that did: the first matched entry setting it,
// which need not be appId. ...App is empty for any other layer.
struct AppResolution {
    // The first of the person's entries that matched, or empty.
    QString appId;
    AppCategory type = AppCategory::Unknown;
    QString writingProfile = WritingProfile::Other;
    AppLayer writingProfileFrom = AppLayer::Fallback;
    QString writingProfileApp;
    // Each part unset: nothing set it, so the Pasting page's steps and the
    // usual paste keys hold (deliveryPlan).
    PastingChoice pasting;
    AppLayer stepsFrom = AppLayer::Fallback;
    QString stepsApp;
    AppLayer keysFrom = AppLayer::Fallback;
    QString keysApp;
    // Unset: nothing lowers What Speecher Keeps for this app.
    std::optional<HistoryKeep> history;
    AppLayer historyFrom = AppLayer::Fallback;
    QString historyApp;

    bool operator==(const AppResolution &other) const = default;
};

// The App Type, Writing Profile and Pasting choice the Target gets. The
// person's entries come first, those matching by id before those matching by
// name or title, and for each value the first that sets it wins.
//   Type: an entry's (never from a title match), then a coding agent detected
//   in a terminal, then the built-in catalog, then General (Unknown for a
//   Target with no identity).
//   Profile: an entry's, then AI coding apps' for an AI coding Target, then
//   the catalog's when no entry set the type, then the type's, then Other
//   apps'.
//   Pasting, its steps and its keys each on their own: an entry's, then
//   Terminals' for anything running in a terminal, then the type's.
//   History: an entry's, then the type's, then Other apps'.
// The Target's own category is ignored; this decides it.
AppResolution resolveApp(const Target &target, const AppsSettings &apps);
// A Target as the app would report itself, from what its App matches: the
// first id, name and window title it matches by.
Target targetFor(const AppEntry &app);
// What app's own values give way to: each value resolved for app's Target
// with that one value of app's unset and the others kept, since the type it
// sets still decides the profile and pasting it would inherit. The type,
// the profile, the pasting parts and History each come from their own
// resolution.
AppResolution inheritedFor(const AppEntry &app, const AppsSettings &apps);
// The ids of the person's entries the Target matches, in the order
// resolveApp consults them: those matching by id, then by name or title.
QStringList matchingAppIds(const Target &target, const AppsSettings &apps);
bool appMatches(const AppMatch &match, const Target &target);
// The settings of App Type `type`; an Unknown type has none of its own.
AppTypeSettings appTypeSettings(const AppsSettings &apps, AppCategory type);
// Other apps' profile: what an app Speecher doesn't recognize writes with.
QString fallbackWritingProfile(const AppsSettings &apps);
// An app id as Id matches compare it: trimmed, case-folded, without a
// ".desktop" or ".exe" ending.
QString comparableAppId(const QString &id);
// The lists of ids, by index, grouped where they share an id as Id matches
// compare them, directly or through others: launchers of one program, which
// one App matches together. Groups and their members keep the lists' order.
QList<QList<int>> sharedIdGroups(const QList<QStringList> &ids);
// A fresh AppEntry id, never one of `taken`.
QString newAppId(const QStringList &taken = {});

// One app the built-in catalog knows, by a word of its id, name, process name
// or role, matched on its own ("code" in "Code - OSS", not in "vscode"). One
// with a type writes with that type's profile; one without names its own.
struct BuiltInApp {
    QString word;
    std::optional<AppCategory> type;
    QString writingProfile;

    bool operator==(const BuiltInApp &other) const = default;
};
// The catalog, in the order it is consulted.
QList<BuiltInApp> builtInApps();

// Whether the Target runs in a terminal: the platform said so, its role or
// App Type says so, or the catalog knows it as a terminal.
bool isTerminalTarget(const Target &target);
// Whether a running process is an AI coding tool, from its name and its
// command line. Only the first two arguments count: the executable, or an
// interpreter and the script it runs.
bool isAiCodingProcess(const QString &processName, const QStringList &commandLine);
// Whether a terminal's window title names an AI coding tool, such as "Claude
// Code". Only for terminals: the AI coding rules otherwise ignore titles,
// which a browser tab or a document can carry.
bool terminalTitleNamesAiCodingTool(const Target &target);

} // namespace speecher
