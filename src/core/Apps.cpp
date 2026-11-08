#include "core/Apps.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <functional>
#include <numeric>

namespace speecher {

namespace {

// A catalog entry with what matches its word: the word on its own, or a
// part that is the word once only letters and digits are kept.
struct CatalogApp {
    BuiltInApp app;
    QRegularExpression pattern;
};

const QList<CatalogApp> &catalog()
{
    static const QList<CatalogApp> apps = [] {
        QList<CatalogApp> all;
        for (const BuiltInApp &app : builtInApps()) {
            all.append({app, QRegularExpression(QStringLiteral("(^|[^\\p{L}\\p{N}])%1([^\\p{L}\\p{N}]|$)")
                                                    .arg(QRegularExpression::escape(app.word)),
                                                QRegularExpression::CaseInsensitiveOption
                                                    | QRegularExpression::UseUnicodePropertiesOption)});
        }
        return all;
    }();
    return apps;
}

QString compact(const QString &value)
{
    QString result;
    result.reserve(value.size());
    for (const QChar character : value) {
        if (character.isLetterOrNumber()) {
            result.append(character.toCaseFolded());
        }
    }
    return result;
}

QStringList identityParts(const Target &target)
{
    return {target.applicationId, target.applicationName, target.processName, target.role};
}

bool catalogMatches(const CatalogApp &entry, const QStringList &parts)
{
    const QString word = compact(entry.app.word);
    return std::any_of(parts.cbegin(), parts.cend(), [&](const QString &part) {
        return entry.pattern.match(part).hasMatch() || compact(part) == word;
    });
}

// The type the catalog gives the Target, from its identity alone.
std::optional<AppCategory> builtInType(const Target &target)
{
    const QStringList parts = identityParts(target);
    for (const CatalogApp &entry : catalog()) {
        if (entry.app.type && catalogMatches(entry, parts)) {
            return entry.app.type;
        }
    }
    return std::nullopt;
}

// Whether the joined parts contain value, as Name and WindowTitle matches
// compare: ignoring case, or once only letters and digits are kept.
bool containsText(const QStringList &parts, const QString &value)
{
    const QString wanted = compact(value);
    if (wanted.isEmpty()) {
        return false;
    }
    const QString joined = parts.join(QLatin1Char(' '));
    return joined.contains(value.trimmed(), Qt::CaseInsensitive) || compact(joined).contains(wanted);
}

struct MatchedEntry {
    const AppEntry *entry;
    // Matched by id or name, which may set the type; a title match may not.
    bool byIdentity;
};

// The entries the Target matches: those matching by id first, then those
// matching by name or title, each group in the person's order.
QList<MatchedEntry> matchedEntries(const Target &target, const QList<AppEntry> &apps)
{
    QList<MatchedEntry> byId;
    QList<MatchedEntry> byText;
    for (const AppEntry &entry : apps) {
        const auto matchesKind = [&](AppMatchKind kind) {
            return std::any_of(entry.matches.cbegin(), entry.matches.cend(),
                               [&](const AppMatch &match) { return match.kind == kind && appMatches(match, target); });
        };
        if (matchesKind(AppMatchKind::Id)) {
            byId.append({&entry, true});
        } else if (matchesKind(AppMatchKind::Name)) {
            byText.append({&entry, true});
        } else if (matchesKind(AppMatchKind::WindowTitle)) {
            byText.append({&entry, false});
        }
    }
    return byId + byText;
}

} // namespace

QList<BuiltInApp> builtInApps()
{
    const auto typed = [](AppCategory type, std::initializer_list<const char *> words) {
        QList<BuiltInApp> apps;
        for (const char *word : words) {
            apps.append({QString::fromUtf8(word), type, {}});
        }
        return apps;
    };
    const auto profiled = [](const QString &profile, std::initializer_list<const char *> words) {
        QList<BuiltInApp> apps;
        for (const char *word : words) {
            apps.append({QString::fromUtf8(word), std::nullopt, profile});
        }
        return apps;
    };
    return typed(AppCategory::AiCoding,
                 {"t3code", "chatgpt", "codex", "cursor", "windsurf", "kiro", "zed", "opencode", "aider",
                  "claude code", "gemini cli", "github copilot", "replit agent", "amazon q developer", "qwen code",
                  "mistral vibe", "goose", "ampcode", "augment code", "sourcegraph cody", "cline", "roo code",
                  "kilo code", "factory droid", "auggie", "kimi code", "pearai", "trae", "google antigravity",
                  "tabnine"})
        + typed(AppCategory::Terminal,
                {"terminal", "konsole", "ghostty", "alacritty", "kitty", "wezterm", "foot", "rio", "xterm", "urxvt",
                 "iterm2"})
        + typed(AppCategory::Email, {"thunderbird", "kmail", "mail", "outlook"})
        + typed(AppCategory::Browser, {"firefox", "chrome", "chromium", "helium", "browser", "safari", "msedge",
                                       "microsoft edge"})
        + typed(AppCategory::Office, {"libreoffice", "soffice", "writer", "office", "winword", "microsoft word",
                                      "excel", "powerpnt", "powerpoint", "com.apple.iwork.pages",
                                      "com.apple.iwork.numbers", "com.apple.iwork.keynote"})
        + typed(AppCategory::CodeEditor, {"kate", "code", "editor", "jetbrains", "xcode", "devenv", "visual studio"})
        + profiled(WritingProfile::Personal, {"signal", "discord", "telegram", "whatsapp", "messenger", "element"})
        + profiled(WritingProfile::Work, {"slack", "microsoft teams", "linear", "notion"});
}

QList<AppCategory> appTypes()
{
    return {AppCategory::Terminal, AppCategory::Browser,  AppCategory::Email,  AppCategory::Office,
            AppCategory::CodeEditor, AppCategory::AiCoding, AppCategory::General};
}

QList<AppTypeSettings> defaultAppTypeSettings()
{
    QList<AppTypeSettings> types;
    for (const AppCategory type : appTypes()) {
        AppTypeSettings settings{type};
        switch (type) {
        case AppCategory::Terminal:
            settings.writingProfile = WritingProfile::Work;
            if (terminalKeysDiffer) {
                settings.pasting.keys = PasteKeyCombo::Terminal;
            }
            break;
        case AppCategory::Office:
        case AppCategory::CodeEditor:
            settings.writingProfile = WritingProfile::Work;
            break;
        case AppCategory::Email:
            settings.writingProfile = WritingProfile::Email;
            break;
        case AppCategory::AiCoding:
            settings.writingProfile = WritingProfile::AiCoding;
            break;
        case AppCategory::General:
            settings.writingProfile = WritingProfile::Other;
            break;
        case AppCategory::Browser:
        case AppCategory::Unknown:
            break;
        }
        types.append(settings);
    }
    return types;
}

AppTypeSettings appTypeSettings(const AppsSettings &apps, AppCategory type)
{
    for (const AppTypeSettings &settings : apps.types) {
        if (settings.type == type) {
            return settings;
        }
    }
    return {type};
}

QString fallbackWritingProfile(const AppsSettings &apps)
{
    return appTypeSettings(apps, AppCategory::General).writingProfile.value_or(WritingProfile::Other);
}

QString comparableAppId(const QString &id)
{
    QString comparable = id.trimmed().toCaseFolded();
    for (const QLatin1String ending : {QLatin1String(".desktop"), QLatin1String(".exe")}) {
        if (comparable.endsWith(ending)) {
            comparable.chop(ending.size());
        }
    }
    return comparable;
}

QList<QList<int>> sharedIdGroups(const QList<QStringList> &ids)
{
    // Each list's group, by the first list in it, merged as shared ids turn up.
    QList<int> group(ids.size());
    std::iota(group.begin(), group.end(), 0);
    const auto root = [&group](int index) {
        while (group[index] != index) {
            index = group[index];
        }
        return index;
    };
    QHash<QString, int> firstWithId;
    for (int i = 0; i < ids.size(); ++i) {
        for (const QString &id : ids.at(i)) {
            const QString comparable = comparableAppId(id);
            if (comparable.isEmpty()) {
                continue;
            }
            const auto seen = firstWithId.constFind(comparable);
            if (seen == firstWithId.cend()) {
                firstWithId.insert(comparable, i);
                continue;
            }
            const int left = root(*seen);
            const int right = root(i);
            group[std::max(left, right)] = std::min(left, right);
        }
    }
    QList<QList<int>> groups;
    QHash<int, qsizetype> groupAt;
    for (int i = 0; i < ids.size(); ++i) {
        const int first = root(i);
        if (!groupAt.contains(first)) {
            groupAt.insert(first, groups.size());
            groups.append(QList<int>());
        }
        groups[groupAt.value(first)].append(i);
    }
    return groups;
}

QString newAppId(const QStringList &taken)
{
    QString id;
    do {
        id = QStringLiteral("app:") + QUuid::createUuid().toString(QUuid::Id128).left(12);
    } while (taken.contains(id));
    return id;
}

bool appMatches(const AppMatch &match, const Target &target)
{
    switch (match.kind) {
    case AppMatchKind::Id: {
        const QString wanted = comparableAppId(match.value);
        return !wanted.isEmpty()
            && (comparableAppId(target.applicationId) == wanted || comparableAppId(target.processName) == wanted);
    }
    case AppMatchKind::Name:
        return containsText(identityParts(target), match.value);
    case AppMatchKind::WindowTitle:
        return containsText({target.windowTitle}, match.value);
    }
    return false;
}

QStringList matchingAppIds(const Target &target, const AppsSettings &apps)
{
    QStringList ids;
    for (const MatchedEntry &match : matchedEntries(target, apps.apps)) {
        ids.append(match.entry->id);
    }
    return ids;
}

AppResolution resolveApp(const Target &target, const AppsSettings &apps)
{
    const QList<MatchedEntry> matched = matchedEntries(target, apps.apps);
    AppResolution resolution;
    if (!matched.isEmpty()) {
        resolution.appId = matched.first().entry->id;
    }
    // The first matched entry that sets field, a member or a test of the
    // entry.
    const auto first = [&matched](auto field, bool identityOnly = false) -> const AppEntry * {
        for (const MatchedEntry &match : matched) {
            if (std::invoke(field, *match.entry) && (match.byIdentity || !identityOnly)) {
                return match.entry;
            }
        }
        return nullptr;
    };

    const AppEntry *typed = first(&AppEntry::type, true);
    if (typed) {
        resolution.type = *typed->type;
    } else if (target.aiCodingToolActive) {
        resolution.type = AppCategory::AiCoding;
    } else if (const std::optional<AppCategory> builtIn = builtInType(target)) {
        resolution.type = *builtIn;
    } else {
        resolution.type = target.hasIdentity() ? AppCategory::General : AppCategory::Unknown;
    }

    const auto typeProfile = [&apps](AppCategory type) { return appTypeSettings(apps, type).writingProfile; };
    const auto setProfile = [&resolution](const QString &profile, AppLayer from, const QString &app = {}) {
        resolution.writingProfile = profile;
        resolution.writingProfileFrom = from;
        resolution.writingProfileApp = app;
    };
    // A coding agent inside a named terminal must not lose to the terminal's
    // catalog entry below, or the AI coding profile never reaches the Target
    // its detection exists for.
    const auto builtInProfile = [&]() -> std::optional<QString> {
        if (typed && !typed->catalogPicksProfile) {
            return std::nullopt;
        }
        const QStringList withTitle = identityParts(target) << target.windowTitle;
        const QStringList withoutTitle = identityParts(target);
        for (const CatalogApp &entry : catalog()) {
            const BuiltInApp &app = entry.app;
            const bool aiCoding = app.type == AppCategory::AiCoding;
            if (!catalogMatches(entry, aiCoding ? withoutTitle : withTitle)) {
                continue;
            }
            if (!app.type) {
                return app.writingProfile;
            }
            if (const std::optional<QString> profile = typeProfile(*app.type)) {
                return profile;
            }
        }
        return std::nullopt;
    };
    std::optional<QString> profile;
    if (const AppEntry *entry = first(&AppEntry::writingProfile)) {
        setProfile(*entry->writingProfile, AppLayer::App, entry->id);
    } else if (resolution.type == AppCategory::AiCoding && (profile = typeProfile(AppCategory::AiCoding))) {
        setProfile(*profile, AppLayer::Type);
    } else if ((profile = builtInProfile())) {
        setProfile(*profile, AppLayer::BuiltIn);
    } else if ((profile = typeProfile(resolution.type))) {
        setProfile(*profile, AppLayer::Type);
    } else {
        setProfile(fallbackWritingProfile(apps), AppLayer::Fallback);
    }

    // Steps and keys settle on their own: Terminals may set only their keys
    // and still follow the type's steps, or none.
    Target classified = target;
    classified.category = resolution.type;
    const bool inTerminal = isTerminalTarget(classified);
    const PastingChoice terminals = appTypeSettings(apps, AppCategory::Terminal).pasting;
    const PastingChoice own = appTypeSettings(apps, resolution.type).pasting;
    const auto settle = [&](auto part, auto &value, AppLayer &from, QString &app) {
        if (const AppEntry *entry = first([part](const AppEntry &each) { return bool(each.pasting.*part); })) {
            value = entry->pasting.*part;
            from = AppLayer::App;
            app = entry->id;
        } else if (inTerminal && terminals.*part) {
            value = terminals.*part;
            from = AppLayer::Terminal;
        } else if (own.*part) {
            value = own.*part;
            from = AppLayer::Type;
        }
    };
    settle(&PastingChoice::steps, resolution.pasting.steps, resolution.stepsFrom, resolution.stepsApp);
    settle(&PastingChoice::keys, resolution.pasting.keys, resolution.keysFrom, resolution.keysApp);

    std::optional<HistoryKeep> history;
    if (const AppEntry *entry = first(&AppEntry::history)) {
        resolution.history = entry->history;
        resolution.historyFrom = AppLayer::App;
        resolution.historyApp = entry->id;
    } else if ((history = appTypeSettings(apps, resolution.type).history)) {
        resolution.history = history;
        resolution.historyFrom = AppLayer::Type;
    } else if ((history = appTypeSettings(apps, AppCategory::General).history)) {
        resolution.history = history;
        resolution.historyFrom = AppLayer::Fallback;
    }
    return resolution;
}

Target targetFor(const AppEntry &app)
{
    Target target;
    for (const AppMatch &match : app.matches) {
        QString &field = match.kind == AppMatchKind::Id     ? target.applicationId
                       : match.kind == AppMatchKind::Name   ? target.applicationName
                                                            : target.windowTitle;
        if (field.isEmpty()) {
            field = match.value;
        }
    }
    return target;
}

AppResolution inheritedFor(const AppEntry &app, const AppsSettings &apps)
{
    const Target target = targetFor(app);
    const auto without = [&](auto clear) {
        AppsSettings changed = apps;
        for (AppEntry &entry : changed.apps) {
            if (entry.id == app.id) {
                clear(entry);
            }
        }
        return resolveApp(target, changed);
    };
    AppResolution inherited = without([](AppEntry &entry) { entry.type.reset(); });
    const AppResolution profile = without([](AppEntry &entry) { entry.writingProfile.reset(); });
    inherited.writingProfile = profile.writingProfile;
    inherited.writingProfileFrom = profile.writingProfileFrom;
    inherited.writingProfileApp = profile.writingProfileApp;
    const AppResolution pasting = without([](AppEntry &entry) { entry.pasting = {}; });
    inherited.pasting = pasting.pasting;
    inherited.stepsFrom = pasting.stepsFrom;
    inherited.stepsApp = pasting.stepsApp;
    inherited.keysFrom = pasting.keysFrom;
    inherited.keysApp = pasting.keysApp;
    const AppResolution history = without([](AppEntry &entry) { entry.history.reset(); });
    inherited.history = history.history;
    inherited.historyFrom = history.historyFrom;
    inherited.historyApp = history.historyApp;
    return inherited;
}

bool isTerminalTarget(const Target &target)
{
    return target.terminalHost || target.aiCodingToolActive || target.category == AppCategory::Terminal
        || target.role.contains(QStringLiteral("terminal"), Qt::CaseInsensitive)
        || builtInType(target) == AppCategory::Terminal;
}

// The commands AI coding tools run as in a terminal. The catalog holds their
// product names, which short commands such as "q" or "pi" can't safely be
// matched as.
static const QSet<QString> &aiCodingCommands()
{
    static const QSet<QString> commands{
        QStringLiteral("aider"),    QStringLiteral("amp"),      QStringLiteral("auggie"),
        QStringLiteral("claude"),   QStringLiteral("cline"),    QStringLiteral("codex"),
        QStringLiteral("copilot"),  QStringLiteral("crush"),    QStringLiteral("cursor-agent"),
        QStringLiteral("droid"),    QStringLiteral("gemini"),   QStringLiteral("goose"),
        QStringLiteral("kiro-cli"), QStringLiteral("kilocode"), QStringLiteral("kimi"),
        QStringLiteral("opencode"), QStringLiteral("openhands"), QStringLiteral("pi"),
        QStringLiteral("q"),        QStringLiteral("qwen"),     QStringLiteral("replit"),
        QStringLiteral("roo"),      QStringLiteral("trae"),     QStringLiteral("vibe"),
        QStringLiteral("windsurf"),
    };
    return commands;
}

static QString executableName(const QString &path)
{
    return path.sliced(path.lastIndexOf(QLatin1Char('/')) + 1).trimmed().toLower();
}

bool isAiCodingProcess(const QString &processName, const QStringList &commandLine)
{
    if (aiCodingCommands().contains(processName.toLower())) {
        return true;
    }
    QStringList executableParts;
    for (const QString &argument : commandLine.mid(0, 2)) {
        const QString trimmed = argument.trimmed();
        if (!trimmed.isEmpty()) {
            executableParts.append(trimmed);
            executableParts.append(executableName(trimmed));
        }
    }
    if (executableParts.isEmpty()) {
        return false;
    }
    if (aiCodingCommands().contains(executableName(executableParts.first()))) {
        return true;
    }
    Target probe;
    probe.processName = executableParts.join(QLatin1Char(' '));
    return builtInType(probe) == AppCategory::AiCoding;
}

bool terminalTitleNamesAiCodingTool(const Target &target)
{
    Target titleProbe;
    titleProbe.applicationName = target.windowTitle;
    return builtInType(titleProbe) == AppCategory::AiCoding;
}

} // namespace speecher
