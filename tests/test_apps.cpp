// The Apps model: the one resolver, the migration from the three matchers it
// replaced, how Apps are stored, and the edits the Apps page makes.
#include "common/test_suites.h"
#include "common/apps_fixtures.h"
#include "common/legacy_settings.h"

#include "core/AppEdits.h"
#include "core/SettingsStore.h"
#include "core/settings/AppsMigration.h"
#include "core/settings/AppsSettingsCodec.h"
#include "core/settings/SettingsKeys.h"
#include "platform/InstalledApps.h"

#include <QLocale>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>

using namespace speecher;
using namespace speecher::test;

Q_DECLARE_METATYPE(speecher::Target)
Q_DECLARE_METATYPE(speecher::AppEntry)

namespace {

Target target(const char *id, const char *name, const char *process, const char *title = "", const char *role = "")
{
    Target target;
    target.applicationId = QString::fromUtf8(id);
    target.applicationName = QString::fromUtf8(name);
    target.processName = QString::fromUtf8(process);
    target.windowTitle = QString::fromUtf8(title);
    target.role = QString::fromUtf8(role);
    return target;
}

// Real targets as each platform's provider reports them.
QList<std::pair<QString, Target>> realTargets()
{
    Target agent = target("org.kde.konsole", "Konsole", "konsole", "✳ Claude Code", "terminal");
    agent.terminalHost = true;
    agent.aiCodingToolActive = true;
    return {
        {"linux-konsole", target("org.kde.konsole", "Konsole", "konsole", "~ : bash — Konsole", "terminal")},
        {"linux-konsole-claude", agent},
        {"linux-ghostty", target("com.mitchellh.ghostty", "ghostty", "ghostty", "~")},
        {"linux-firefox-gmail", target("firefox", "Firefox", "firefox", "Inbox (3) - me@gmail.com - Gmail — Mozilla Firefox")},
        {"linux-firefox-slack", target("firefox", "Firefox", "firefox", "general - Acme - Slack — Mozilla Firefox")},
        {"linux-thunderbird", target("org.mozilla.Thunderbird", "Thunderbird", "thunderbird", "Inbox - Mozilla Thunderbird")},
        {"linux-kate", target("org.kde.kate", "Kate", "kate", "notes.md — Kate")},
        {"linux-vscode", target("code", "Code", "code", "main.cpp - speecher - Visual Studio Code")},
        {"linux-slack", target("slack", "Slack", "slack", "general - Acme - Slack")},
        {"linux-writer", target("libreoffice-writer", "LibreOffice Writer", "soffice.bin", "Untitled 1 - LibreOffice Writer")},
        {"linux-signal", target("signal", "Signal", "signal", "Signal")},
        {"linux-dolphin", target("org.kde.dolphin", "Dolphin", "dolphin", "Home — Dolphin")},
        {"linux-kwin-fallback", target("org.kde.konsole", "", "", "~ : bash — Konsole")},
        {"mac-terminal", target("com.apple.Terminal", "Terminal", "Terminal", "~ — -zsh — 80×24")},
        {"mac-mail", target("com.apple.mail", "Mail", "Mail", "Inbox")},
        {"mac-safari", target("com.apple.Safari", "Safari", "Safari", "Apple")},
        {"mac-xcode", target("com.apple.dt.Xcode", "Xcode", "Xcode", "Speecher — main.swift")},
        {"mac-cursor", target("com.todesktop.230313mzl4w4u92", "Cursor", "Cursor", "Apps.cpp — speecher")},
        {"mac-notes", target("com.apple.Notes", "Notes", "Notes", "Notes")},
        {"win-terminal", target("windowsterminal", "Windows Terminal", "windowsterminal", "PowerShell")},
        {"win-outlook", target("outlook", "Outlook", "outlook", "Inbox - me@example.com - Outlook")},
        {"win-edge", target("msedge", "Microsoft Edge", "msedge", "New tab - Profile 1 - Microsoft Edge")},
        {"win-word", target("winword", "Word", "winword", "Document1 - Word")},
        {"win-notepad", target("notepad", "Notepad", "notepad", "Untitled - Notepad")},
        {"win-calculator", target("applicationframehost", "Calculator", "applicationframehost", "Calculator")},
        {"no-identity", target("", "", "", "")},
    };
}

// What classifyTarget, resolveWritingProfile and resolvePasteRule answered
// for realTargets() before Apps replaced them, captured from that code on
// Linux: "type profile pasting", where pasting "steps" is no rule. The
// "custom" settings are the ones customSettings() stores. Safari, Xcode,
// Outlook, Edge and Word answered General then; the catalog now knows them
// by the types the Apps page names them under.
const QHash<QString, QString> &oldAnswers()
{
    static const QHash<QString, QString> answers{
        {"defaults linux-konsole", "terminal work terminal_paste"},
        {"defaults linux-konsole-claude", "ai_coding ai_coding terminal_paste"},
        {"defaults linux-ghostty", "terminal work terminal_paste"},
        {"defaults linux-firefox-gmail", "browser other steps"},
        {"defaults linux-firefox-slack", "browser work steps"},
        {"defaults linux-thunderbird", "email email steps"},
        {"defaults linux-kate", "code_editor work steps"},
        {"defaults linux-vscode", "code_editor work steps"},
        {"defaults linux-slack", "general work steps"},
        {"defaults linux-writer", "office work steps"},
        {"defaults linux-signal", "general personal steps"},
        {"defaults linux-dolphin", "general other steps"},
        {"defaults linux-kwin-fallback", "terminal work terminal_paste"},
        {"defaults mac-terminal", "terminal work terminal_paste"},
        {"defaults mac-mail", "email email steps"},
        {"defaults mac-safari", "browser other steps"},
        {"defaults mac-xcode", "code_editor work steps"},
        {"defaults mac-cursor", "ai_coding ai_coding steps"},
        {"defaults mac-notes", "general other steps"},
        {"defaults win-terminal", "terminal work terminal_paste"},
        {"defaults win-outlook", "email email steps"},
        {"defaults win-edge", "browser other steps"},
        {"defaults win-word", "office work steps"},
        {"defaults win-notepad", "general other steps"},
        {"defaults win-calculator", "general other steps"},
        {"defaults no-identity", "unknown other steps"},
        {"custom linux-konsole", "terminal work terminal_paste"},
        {"custom linux-konsole-claude", "ai_coding ai_coding terminal_paste"},
        {"custom linux-ghostty", "terminal work terminal_paste"},
        {"custom linux-firefox-gmail", "browser email clipboard_only"},
        {"custom linux-firefox-slack", "browser work clipboard_only"},
        {"custom linux-thunderbird", "email email direct_insert"},
        {"custom linux-kate", "code_editor personal steps"},
        {"custom linux-vscode", "code_editor work steps"},
        {"custom linux-slack", "general email steps"},
        {"custom linux-writer", "office work steps"},
        {"custom linux-signal", "general work steps"},
        {"custom linux-dolphin", "office work steps"},
        {"custom linux-kwin-fallback", "terminal work terminal_paste"},
        {"custom mac-terminal", "terminal work terminal_paste"},
        {"custom mac-mail", "email email steps"},
        {"custom mac-safari", "browser personal clipboard_only"},
        {"custom mac-xcode", "code_editor work steps"},
        {"custom mac-cursor", "ai_coding ai_coding steps"},
        {"custom mac-notes", "general personal steps"},
        {"custom win-terminal", "terminal work terminal_paste"},
        {"custom win-outlook", "email email clipboard_only"},
        {"custom win-edge", "browser personal clipboard_only"},
        {"custom win-word", "office work steps"},
        {"custom win-notepad", "code_editor personal steps"},
        {"custom win-calculator", "general personal steps"},
        {"custom no-identity", "unknown personal steps"},
    };
    return answers;
}

// The old settings the "custom" answers came from, stored as older releases
// stored them: substring recognition rules (one by title only, one setting
// both), overrides (one off), single-app and app type paste rules (one off)
// beside the global rule, and a default profile.
void storeCustomSettings(QSettings &stored)
{
    stored.setValue(SettingsKeys::AppRecognitionRules,
                    QByteArray(R"([{"match":"phin","category":"office"},{"match":"gmail","writingProfile":"email"},)"
                               R"({"match":"signal","writingProfile":"work"},)"
                               R"({"match":"notepad","category":"code_editor","writingProfile":"personal"}])"));
    stored.setValue(SettingsKeys::WritingProfileOverrides,
                    QByteArray(R"([{"applicationId":"org.kde.kate","profile":"personal","enabled":true},)"
                               R"({"applicationId":"Slack","profile":"email","enabled":true},)"
                               R"({"applicationId":"winword","profile":"email","enabled":false}])"));
    storeLegacyPasteRules(stored, {
        {LegacyPasteRule::Scope::Category, "terminal", PasteMethod::TerminalPaste, true},
        {LegacyPasteRule::Scope::Category, "browser", PasteMethod::ClipboardOnly, true},
        {LegacyPasteRule::Scope::Application, "org.mozilla.Thunderbird", PasteMethod::DirectInsert, true},
        {LegacyPasteRule::Scope::Application, "outlook", PasteMethod::ClipboardOnly, true},
        {LegacyPasteRule::Scope::Application, "msedge", PasteMethod::DirectInsert, false},
        {LegacyPasteRule::Scope::Global, "", PasteMethod::StandardPaste, true},
    });
    stored.setValue(SettingsKeys::DefaultWritingProfile, QStringLiteral("personal"));
}

const DeliverySteps keepOnly{.insert = false, .press = false, .keep = true};
const DeliverySteps insertAndKeep{.insert = true, .press = false, .keep = true};

// A Pasting choice's pasting as the old answers name it, by what a delivery
// does with every step on the Pasting page: "steps" or "terminal_paste"
// where it takes them all, "direct_insert" or "clipboard_only" where it
// takes Insert and Keep, or Keep alone.
QString pastingAnswer(const PastingChoice &pasting)
{
    const DeliveryPlan plan = deliveryPlan(DeliverySteps{}, pasting);
    if (plan.steps == DeliverySteps{}) {
        return plan.keys == PasteKeyCombo::Terminal ? QStringLiteral("terminal_paste") : QStringLiteral("steps");
    }
    return plan.steps == insertAndKeep ? QStringLiteral("direct_insert")
         : plan.steps == keepOnly      ? QStringLiteral("clipboard_only")
                                       : plan.route().join(u'+');
}

QString answer(const AppResolution &resolution)
{
    return QStringList{appCategoryName(resolution.type), resolution.writingProfile, pastingAnswer(resolution.pasting)}
        .join(u' ');
}

// What a delivery to target does with these steps on the Pasting page:
// its route, and the keys where it presses them.
QString delivery(const Target &target, const AppsSettings &apps, const DeliverySteps &steps)
{
    const DeliveryPlan plan = deliveryPlan(steps, resolveApp(target, apps).pasting);
    return plan.route().join(u' ') + (plan.steps.press ? u' ' + pasteKeyComboName(plan.keys) : QString());
}

// An App as migration made it, without the random part of its id.
AppEntry withoutId(AppEntry app)
{
    app.id.clear();
    return app;
}

} // namespace

class AppsTests : public QObject {
    Q_OBJECT

private slots:
    // Every real target resolves as the three old matchers resolved it, with
    // the default settings and with old custom settings migrated.
    void realTargetsResolveAsBefore_data()
    {
        QTest::addColumn<QString>("scenario");
        QTest::addColumn<Target>("target");
        QTest::addColumn<QString>("expected");
        for (const QString scenario : {QStringLiteral("defaults"), QStringLiteral("custom")}) {
            for (const auto &[name, target] : realTargets()) {
                QString expected = oldAnswers().value(scenario + u' ' + name);
                // The answers were captured where terminals' keys differ; on
                // macOS terminals press Cmd+V like the rest, so neither the
                // default Terminals nor a migrated Terminal paste presses them.
                if (!terminalKeysDiffer) {
                    expected.replace(QStringLiteral("terminal_paste"), QStringLiteral("steps"));
                }
                QTest::addRow("%s %s", qPrintable(scenario), qPrintable(name)) << scenario << target << expected;
            }
        }
    }

    void realTargetsResolveAsBefore()
    {
        QFETCH(QString, scenario);
        QFETCH(Target, target);
        QFETCH(QString, expected);
        QVERIFY(!expected.isEmpty());
        AppsSettings apps;
        if (scenario == u"custom") {
            QTemporaryDir dir;
            QSettings stored(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
            storeCustomSettings(stored);
            migrateApps(stored);
            apps = AppsSettingsCodec::load(stored);
        }
        QCOMPARE(answer(resolveApp(target, apps)), expected);
    }

    void eachValueSaysWhereItCameFrom()
    {
        AppsSettings apps;
        apps.apps = {idApp(QStringLiteral("org.kde.kate"), WritingProfile::Personal)};
        const AppResolution kate = resolveApp(target("org.kde.kate", "Kate", "kate"), apps);
        QCOMPARE(kate.appId, QStringLiteral("app:org.kde.kate"));
        QCOMPARE(kate.writingProfileFrom, AppLayer::App);
        QCOMPARE(kate.stepsFrom, AppLayer::Fallback);
        QCOMPARE(kate.keysFrom, AppLayer::Fallback);

        const AppResolution slack = resolveApp(target("slack", "Slack", "slack"), apps);
        QVERIFY(slack.appId.isEmpty());
        QCOMPARE(slack.writingProfileFrom, AppLayer::BuiltIn);

        const AppResolution thunderbird = resolveApp(target("thunderbird", "Thunderbird", "thunderbird"), apps);
        QCOMPARE(thunderbird.writingProfile, WritingProfile::Email);
        QCOMPARE(thunderbird.writingProfileFrom, AppLayer::BuiltIn);

        Target inTerminal = target("org.kde.dolphin", "Dolphin", "dolphin");
        inTerminal.terminalHost = true;
        QCOMPARE(resolveApp(inTerminal, apps).keysFrom, terminalKeysDiffer ? AppLayer::Terminal : AppLayer::Fallback);
        QCOMPARE(resolveApp(inTerminal, apps).stepsFrom, AppLayer::Fallback);
        QCOMPARE(resolveApp(target("org.kde.dolphin", "Dolphin", "dolphin"), apps).writingProfileFrom,
                 AppLayer::Type);
        QCOMPARE(resolveApp(target("", "", ""), apps).writingProfileFrom, AppLayer::Fallback);
    }

    // Where an App set a value, the resolution names the first matched App
    // that set it, which needn't be the first that matched.
    void eachValueSaysWhichAppSetIt()
    {
        AppsSettings apps;
        apps.apps = {idApp(QStringLiteral("org.kde.kate"), std::nullopt,
                           {DeliverySteps{.insert = true, .press = false, .keep = true}, std::nullopt}),
                     textApp(QStringLiteral("Kate"), std::nullopt, WritingProfile::Personal)};
        apps.apps[1].history = HistoryKeep::Stats;

        const AppResolution kate = resolveApp(target("org.kde.kate", "Kate", "kate", "Kate"), apps);
        QCOMPARE(kate.appId, QStringLiteral("app:org.kde.kate"));
        QCOMPARE(kate.stepsFrom, AppLayer::App);
        QCOMPARE(kate.stepsApp, QStringLiteral("app:org.kde.kate"));
        QCOMPARE(kate.writingProfileFrom, AppLayer::App);
        QCOMPARE(kate.writingProfileApp, QStringLiteral("app:Kate"));
        QCOMPARE(kate.historyApp, QStringLiteral("app:Kate"));
        // Set by no App: no App to name.
        QCOMPARE(kate.keysFrom, AppLayer::Fallback);
        QVERIFY(kate.keysApp.isEmpty());
        QCOMPARE(resolveApp(target("thunderbird", "Thunderbird", "thunderbird"), apps).writingProfileApp, QString());
    }

    // What an App inherits: each of its values resolved with that value
    // alone unset, so the type it sets still picks the profile, steps and
    // History it would follow.
    void anAppInheritsEachValueWithOnlyThatOneUnset()
    {
        AppsSettings apps;
        for (AppTypeSettings &type : apps.types) {
            if (type.type == AppCategory::Email) {
                type.writingProfile = WritingProfile::Work;
                type.pasting.steps = DeliverySteps{.insert = false, .press = false, .keep = true};
                type.history = HistoryKeep::Stats;
            }
        }
        AppEntry notes = idApp(QStringLiteral("org.example.notes"), WritingProfile::Personal,
                               {DeliverySteps{.insert = true, .press = false, .keep = true}, PasteKeyCombo::Terminal});
        notes.type = AppCategory::Email;
        notes.history = HistoryKeep::Nothing;
        apps.apps = {notes};

        const AppResolution inherited = inheritedFor(notes, apps);
        QCOMPARE(inherited.type, AppCategory::General);
        QCOMPARE(inherited.writingProfile, WritingProfile::Work);
        QCOMPARE(inherited.writingProfileFrom, AppLayer::Type);
        QCOMPARE(inherited.pasting.steps, (DeliverySteps{.insert = false, .press = false, .keep = true}));
        QCOMPARE(inherited.stepsFrom, AppLayer::Type);
        QCOMPARE(inherited.pasting.keys, std::nullopt);
        QCOMPARE(inherited.keysFrom, AppLayer::Fallback);
        QCOMPARE(inherited.history, HistoryKeep::Stats);
        QCOMPARE(inherited.historyFrom, AppLayer::Type);
        QVERIFY(inherited.writingProfileApp.isEmpty() && inherited.stepsApp.isEmpty());
        // Its own values still win where it is resolved.
        const AppResolution own = resolveApp(targetFor(notes), apps);
        QCOMPARE(own.writingProfile, WritingProfile::Personal);
        QCOMPARE(own.writingProfileApp, notes.id);
    }

    // An id matches the Target's application id or process name exactly,
    // ignoring case and a ".desktop" or ".exe" ending; nothing shorter.
    void idMatchesAreExact()
    {
        const AppMatch konsole{AppMatchKind::Id, QStringLiteral(" org.kde.Konsole.desktop ")};
        QVERIFY(appMatches(konsole, target("org.kde.konsole", "", "")));
        QVERIFY(appMatches(konsole, target("", "", "org.kde.konsole")));
        QVERIFY(!appMatches(konsole, target("org.kde.konsole-nightly", "", "")));
        QVERIFY(!appMatches(konsole, target("", "Konsole", "")));
        const AppMatch terminal{AppMatchKind::Id, QStringLiteral("WindowsTerminal.exe")};
        QVERIFY(appMatches(terminal, target("windowsterminal", "", "")));
        QVERIFY(!appMatches({AppMatchKind::Id, QStringLiteral("  ")}, target("", "", "")));
        QCOMPARE(comparableAppId(QStringLiteral(" Org.Mozilla.Firefox.desktop")), QStringLiteral("org.mozilla.firefox"));
        // macOS reports the bundle id; a Windows store app its AUMID.
        QVERIFY(appMatches({AppMatchKind::Id, QStringLiteral("com.apple.Terminal")}, target("com.apple.terminal", "", "")));
        QVERIFY(appMatches({AppMatchKind::Id, QStringLiteral("Microsoft.WindowsCalculator_8wekyb3d8bbwe!App")},
                           target("microsoft.windowscalculator_8wekyb3d8bbwe!app", "", "")));
    }

    // Launchers that share a program, directly or through another, are one
    // group, in the order they came.
    void launchersSharingAnIdGroupTogether()
    {
        const QList<QStringList> ids{
            {QStringLiteral("firefox")},
            {QStringLiteral("idle")},
            {QStringLiteral("Firefox.exe"), QStringLiteral("private")},
            {QStringLiteral("notepad")},
            {QStringLiteral("PRIVATE")},
            {QStringLiteral("idle"), QStringLiteral("pythonw")},
        };
        QCOMPARE(sharedIdGroups(ids), (QList<QList<int>>{{0, 2, 4}, {1, 5}, {3}}));
        QVERIFY(sharedIdGroups({}).isEmpty());
    }

#ifdef Q_OS_LINUX
    // The fixture's .desktop files as a KDE session lists them: an earlier
    // directory's file hides the same id later, hidden or not; entries that
    // aren't shown here are left out; each app carries every id a Target may
    // report, flatpak's and snap's included.
    void desktopEntriesListTheInstalledApps()
    {
        const QString data = QStringLiteral(SPEECHER_SOURCE_DIR "/tests/data/installed-apps/");
        QStringList dirs;
        for (const char *dir : {"home", "system", "flatpak", "snap"}) {
            dirs.append(data + QLatin1String(dir) + QStringLiteral("/applications"));
        }
        const QList<InstalledApp> apps = desktopEntryApps(dirs, {QStringLiteral("KDE")});
        QStringList listed;
        for (const InstalledApp &app : apps) {
            listed.append(app.name + QStringLiteral(": ") + app.ids.join(u' '));
        }
        QCOMPARE(listed, (QStringList{
                             QStringLiteral("Dolphin: kde-dolphin dolphin"),
                             QStringLiteral("Firefox: firefox firefox-bin"),
                             QStringLiteral("Firefox (Snap): firefox_firefox firefox"),
                             QStringLiteral("GNU Image Manipulation Program: org.gimp.GIMP gimp gimp-2.10"),
                             // An interpreter runs other apps too: not an id of this one.
                             QStringLiteral("IDLE: idle"),
                             QStringLiteral("My Konsole: org.kde.konsole konsole"),
                             QStringLiteral("System Settings: kde-only systemsettings"),
                         }));
        QCOMPARE(apps.at(1).icon, QStringLiteral("/usr/share/pixmaps/firefox.png"));
        QCOMPARE(apps.at(5).location, data + QStringLiteral("home/applications/org.kde.konsole.desktop"));

        const QLocale before;
        QLocale::setDefault(QLocale(QStringLiteral("de_DE")));
        const auto restore = qScopeGuard([&before] { QLocale::setDefault(before); });
        const QList<InstalledApp> german = desktopEntryApps(dirs, {QStringLiteral("KDE")});
        const auto firefox = std::find_if(german.cbegin(), german.cend(), [](const InstalledApp &app) {
            return app.ids.first() == u"firefox";
        });
        QVERIFY(firefox != german.cend());
        QCOMPARE(firefox->name, QStringLiteral("Firefox-Webbrowser"));
    }

    // An App made from an installed app matches the Target KWin reports for
    // it, by its desktop file id or its program.
    void installedAppIdsMatchWhatTargetsReport()
    {
        const QString data = QStringLiteral(SPEECHER_SOURCE_DIR "/tests/data/installed-apps/flatpak/applications");
        const InstalledApp gimp = desktopEntryApps({data}, {}).first();
        AppEntry app{QStringLiteral("app:gimp"), gimp.name, gimp.icon, {}, std::nullopt, WritingProfile::Personal, {}};
        for (const QString &id : gimp.ids) {
            app.matches.append({AppMatchKind::Id, id});
        }
        AppsSettings apps;
        apps.apps = {app};
        QCOMPARE(resolveApp(target("org.gimp.GIMP", "gimp", "gimp-2.10"), apps).writingProfile, WritingProfile::Personal);
        QCOMPARE(resolveApp(target("", "", "gimp-2.10"), apps).writingProfile, WritingProfile::Personal);
    }
#endif

    // Name and title matches keep the old rules' any-part matching, so a
    // migrated rule matches what it did: "code" in "vscode", and the same
    // text with the punctuation left out.
    void nameAndTitleMatchesFindAnyPart()
    {
        const AppMatch code{AppMatchKind::Name, QStringLiteral("code")};
        QVERIFY(appMatches(code, target("com.visualstudio.vscode", "", "")));
        QVERIFY(appMatches({AppMatchKind::Name, QStringLiteral("acme shell")}, target("", "", "acme-shell")));
        QVERIFY(!appMatches(code, target("", "", "", "code review")));
        QVERIFY(appMatches({AppMatchKind::WindowTitle, QStringLiteral("gmail")}, target("", "", "", "Inbox - Gmail")));
        QVERIFY(!appMatches({AppMatchKind::Name, QStringLiteral("!!!")}, target("org.example.!!!", "", "")));
    }

    // The built-in catalog matches a word on its own, so a "code" inside
    // another word is not VS Code.
    void builtInAppsMatchWholeWords()
    {
        QCOMPARE(resolveApp(target("org.example.barcode", "", ""), {}).type, AppCategory::General);
        QCOMPARE(resolveApp(target("code", "", ""), {}).type, AppCategory::CodeEditor);
        QCOMPARE(resolveApp(target("", "", "qwen-code"), {}).type, AppCategory::AiCoding);
        QCOMPARE(builtInApps().size(), 82);
        QVERIFY(isAiCodingProcess(QStringLiteral("claude"), {}));
        QVERIFY(isAiCodingProcess(QStringLiteral("node"), {QStringLiteral("node"),
                                                           QStringLiteral("/usr/lib/node_modules/@openai/codex/bin/codex.js")}));
        QVERIFY(!isAiCodingProcess(QStringLiteral("zsh"), {QStringLiteral("-zsh")}));
    }

    // A window title says what a window shows: a title match sets the
    // profile and pasting, never the type.
    void aTitleMatchNeverSetsTheType()
    {
        AppsSettings apps;
        AppEntry gmail = textApp(QStringLiteral("Gmail"), AppCategory::Email, WritingProfile::Personal);
        gmail.matches = {{AppMatchKind::WindowTitle, QStringLiteral("Gmail")}};
        gmail.pasting.steps = keepOnly;
        apps.apps = {gmail};
        const AppResolution firefox = resolveApp(target("firefox", "Firefox", "firefox", "Inbox - Gmail"), apps);
        QCOMPARE(firefox.type, AppCategory::Browser);
        QCOMPARE(firefox.writingProfile, WritingProfile::Personal);
        QCOMPARE(firefox.pasting.steps, keepOnly);
    }

    // Entries matching by id come first, wherever they are in the list.
    void idEntriesComeBeforeNameEntries()
    {
        AppsSettings apps;
        apps.apps = {textApp(QStringLiteral("kate"), std::nullopt, WritingProfile::Email),
                     idApp(QStringLiteral("org.kde.kate"), WritingProfile::Personal)};
        QCOMPARE(resolveApp(target("org.kde.kate", "Kate", "kate"), apps).writingProfile, WritingProfile::Personal);
        QCOMPARE(resolveApp(target("org.kde.kate", "Kate", "kate"), apps).appId, QStringLiteral("app:org.kde.kate"));
    }

    // Changing an App Type changes every app of that type, the catalog's
    // included; an app whose type the person set takes that type's profile
    // rather than one the catalog names.
    void appTypesDecideForTheirApps()
    {
        AppsSettings apps;
        for (AppTypeSettings &type : apps.types) {
            if (type.type == AppCategory::Email) {
                type.writingProfile = WritingProfile::Personal;
                type.pasting.steps = insertAndKeep;
            }
        }
        const Target thunderbird = target("org.mozilla.Thunderbird", "Thunderbird", "thunderbird");
        QCOMPARE(resolveApp(thunderbird, apps).writingProfile, WritingProfile::Personal);
        QCOMPARE(resolveApp(thunderbird, apps).pasting.steps, insertAndKeep);

        apps.apps = {textApp(QStringLiteral("slack"), AppCategory::Email)};
        QCOMPARE(resolveApp(target("slack", "Slack", "slack"), apps).type, AppCategory::Email);
        QCOMPARE(resolveApp(target("slack", "Slack", "slack"), apps).writingProfile, WritingProfile::Personal);
    }

    // Other apps' profile is the fallback, also for a target with no
    // identity; its Pasting choice is for apps Speecher can tell apart.
    void otherAppsIsTheFallback()
    {
        AppsSettings apps;
        setOtherAppsProfile(apps, WritingProfile::Personal);
        for (AppTypeSettings &type : apps.types) {
            if (type.type == AppCategory::General || type.type == AppCategory::Browser) {
                type.pasting.steps = keepOnly;
            }
        }
        QCOMPARE(fallbackWritingProfile(apps), WritingProfile::Personal);
        QCOMPARE(resolveApp(target("org.kde.dolphin", "Dolphin", "dolphin"), apps).pasting.steps, keepOnly);
        const AppResolution unknown = resolveApp(target("", "", ""), apps);
        QCOMPARE(unknown.type, AppCategory::Unknown);
        QCOMPARE(unknown.writingProfile, WritingProfile::Personal);
        QCOMPARE(unknown.pasting.steps, std::nullopt);
        // Browsers follow Other apps' profile until they have their own.
        QCOMPARE(resolveApp(target("firefox", "Firefox", "firefox"), apps).writingProfile, WritingProfile::Personal);
    }

    // An app's own steps and keys win over Terminals' for anything in a
    // terminal, which win over the app's type; each part settles on its
    // own, so Terminals' keys don't hide the type's steps.
    void pastingPrefersTheAppThenTerminalsThenTheType()
    {
        AppsSettings apps;
        for (AppTypeSettings &type : apps.types) {
            if (type.type == AppCategory::Terminal) type.pasting = {std::nullopt, PasteKeyCombo::Terminal};
            if (type.type == AppCategory::AiCoding) type.pasting = {keepOnly, PasteKeyCombo::Standard};
        }
        Target agent = target("org.kde.konsole", "Konsole", "konsole");
        agent.terminalHost = true;
        agent.aiCodingToolActive = true;
        QCOMPARE(resolveApp(agent, apps).type, AppCategory::AiCoding);
        QCOMPARE(resolveApp(agent, apps).pasting, (PastingChoice{keepOnly, PasteKeyCombo::Terminal}));
        QCOMPARE(resolveApp(agent, apps).stepsFrom, AppLayer::Type);
        QCOMPARE(resolveApp(agent, apps).keysFrom, AppLayer::Terminal);
        QCOMPARE(resolveApp(target("", "Cursor", "cursor"), apps).pasting, (PastingChoice{keepOnly, PasteKeyCombo::Standard}));
        apps.apps = {idApp(QStringLiteral("org.kde.konsole"), std::nullopt, {DeliverySteps{}, PasteKeyCombo::Standard})};
        QCOMPARE(resolveApp(agent, apps).pasting, (PastingChoice{DeliverySteps{}, PasteKeyCombo::Standard}));
        QCOMPARE(resolveApp(agent, apps).stepsFrom, AppLayer::App);
        QCOMPARE(resolveApp(agent, apps).keysFrom, AppLayer::App);
    }

    // Each older Pasting choice delivers as it did under the older output
    // method: here a keyboard-only one, which pressed the keys and kept the
    // words, with Insert off.
    void pastingChoicesMigrateToTheSameDelivery()
    {
        QTemporaryDir dir;
        QSettings stored(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        stored.setValue(SettingsKeys::OutputMethod, QStringLiteral("ydotool"));
        using Scope = LegacyPasteRule::Scope;
        storeLegacyPasteRules(stored, {
            {Scope::Global, "", PasteMethod::StandardPaste, true},
            {Scope::Category, "terminal", PasteMethod::TerminalPaste, true},
            {Scope::Category, "browser", PasteMethod::ClipboardOnly, true},
            {Scope::Application, "org.kde.kate", PasteMethod::DirectInsert, true},
            {Scope::Application, "outlook", PasteMethod::ClipboardOnly, true},
            {Scope::Application, "firefox", PasteMethod::StandardPaste, true},
            {Scope::Application, "org.kde.dolphin", PasteMethod::TerminalPaste, true},
        });
        migrateApps(stored);
        QCOMPARE(stored.value(SettingsKeys::AppsMigrationVersion).toInt(), 2);
        const AppsSettings apps = AppsSettingsCodec::load(stored);
        const DeliverySteps steps{.insert = false, .press = true, .keep = true};
        const QString terminal = terminalKeysDiffer ? QStringLiteral("terminal") : QStringLiteral("standard");
        Target agent = target("org.kde.konsole", "Konsole", "konsole");
        agent.terminalHost = true;
        agent.aiCodingToolActive = true;
        QCOMPARE(delivery(target("org.kde.kate", "Kate", "kate"), apps, steps), QStringLiteral("insert keep"));
        QCOMPARE(delivery(target("outlook", "Outlook", "outlook"), apps, steps), QStringLiteral("keep"));
        QCOMPARE(delivery(target("chromium", "Chromium", "chromium"), apps, steps), QStringLiteral("keep"));
        // Its own Standard paste beat Browsers' Clipboard only.
        QCOMPARE(delivery(target("firefox", "Firefox", "firefox"), apps, steps), QStringLiteral("press keep standard"));
        QCOMPARE(delivery(target("org.kde.dolphin", "Dolphin", "dolphin"), apps, steps), u"press keep " + terminal);
        QCOMPARE(delivery(target("org.kde.konsole", "Konsole", "konsole"), apps, steps), u"press keep " + terminal);
        QCOMPARE(delivery(agent, apps, steps), u"press keep " + terminal);
        QCOMPARE(delivery(target("org.gnome.Nautilus", "Files", "nautilus"), apps, steps), QStringLiteral("press keep standard"));
        // Firefox's own choice followed the steps as they are, which only a
        // copy of them keeps, since Browsers now have steps of their own.
        QCOMPARE(delivery(target("firefox", "Firefox", "firefox"), apps, DeliverySteps{}),
                 QStringLiteral("press keep standard"));
        // Terminals' keys alone leave other apps the page's steps as they
        // come to be.
        QCOMPARE(delivery(target("org.gnome.Nautilus", "Files", "nautilus"), apps, DeliverySteps{}),
                 QStringLiteral("insert press keep standard"));
    }

    // A file the first Apps migration wrote has each "pasting" turned into
    // steps and keys, once.
    void storedPastingChoicesBecomeStepsAndKeys()
    {
        QTemporaryDir dir;
        QSettings stored(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        stored.setValue(SettingsKeys::AppsMigrationVersion, 1);
        stored.setValue(SettingsKeys::KeepOnClipboard, false);
        stored.setValue(SettingsKeys::AppsEntries,
                        QByteArray(R"([{"id":"app:kate","name":"Kate","matches":[{"kind":"id","value":"org.kde.kate"}],)"
                                   R"("pasting":"direct_insert"},)"
                                   R"({"id":"app:term","name":"Term","matches":[{"kind":"id","value":"term"}],)"
                                   R"("pasting":"standard_paste"}])"));
        stored.setValue(SettingsKeys::AppsTypes,
                        QByteArray(R"([{"type":"terminal","pasting":"terminal_paste","writingProfile":"work"},)"
                                   R"({"type":"general","pasting":"clipboard_only","writingProfile":"other"}])"));
        migrateApps(stored);
        const AppsSettings apps = AppsSettingsCodec::load(stored);
        // Direct insertion copied first, so a failed insertion kept the words.
        QCOMPARE(apps.apps.at(0).pasting, (PastingChoice{insertAndKeep, std::nullopt}));
        // Other apps have steps of their own, which its Standard paste hid.
        // Its Standard keys are kept only where Terminals' would differ.
        QCOMPARE(apps.apps.at(1).pasting,
                 (PastingChoice{DeliverySteps{.insert = true, .press = true, .keep = false},
                                terminalKeysDiffer ? std::optional(PasteKeyCombo::Standard) : std::nullopt}));
        QCOMPARE(appTypeSettings(apps, AppCategory::General).pasting, (PastingChoice{keepOnly, std::nullopt}));
        // On macOS Terminals' keys are the usual ones, so they aren't stored.
        QCOMPARE(appTypeSettings(apps, AppCategory::Terminal).pasting.keys,
                 terminalKeysDiffer ? std::optional(PasteKeyCombo::Terminal) : std::nullopt);
        QVERIFY(!stored.value(SettingsKeys::AppsEntries).toByteArray().contains("\"pasting\""));
        QCOMPARE(stored.value(SettingsKeys::AppsMigrationVersion).toInt(), 2);
        // Once.
        stored.setValue(SettingsKeys::AppsTypes, QByteArray(R"([{"type":"general","pasting":"clipboard_only"}])"));
        migrateApps(stored);
        QCOMPARE(appTypeSettings(AppsSettingsCodec::load(stored), AppCategory::General).pasting, PastingChoice{});
    }

    void migrationShapes_data()
    {
        QTest::addColumn<QVariantMap>("stored");
        QTest::addColumn<QList<AppEntry>>("apps");
        QTest::addColumn<QString>("otherAppsProfile");
        QTest::addColumn<QString>("browserPasting");

        const auto id = [](const char *id, std::optional<QString> profile, const PastingChoice &pasting) {
            AppEntry app = idApp(QString::fromUtf8(id), profile, pasting);
            app.id.clear();
            app.name = QString::fromUtf8(id).section(u'.', -1);
            app.name[0] = app.name[0].toUpper();
            return app;
        };
        // Beside the global Standard paste rule older releases stored.
        const auto rules = [](QList<LegacyPasteRule> rules) {
            QTemporaryDir dir;
            QSettings settings(dir.filePath(QStringLiteral("rules.ini")), QSettings::IniFormat);
            rules.append({LegacyPasteRule::Scope::Global, QString(), PasteMethod::StandardPaste, true});
            storeLegacyPasteRules(settings, rules);
            return settings.value(SettingsKeys::PasteRules);
        };
        using Scope = LegacyPasteRule::Scope;
        QTest::newRow("override only")
            << QVariantMap{{SettingsKeys::WritingProfileOverrides,
                            QByteArray(R"([{"applicationId":"org.kde.kate","profile":"email","enabled":true}])")}}
            << QList<AppEntry>{id("org.kde.kate", WritingProfile::Email, {})} << "other" << "";
        QTest::newRow("paste rule only")
            << QVariantMap{{SettingsKeys::PasteRules,
                            rules({{Scope::Application, "org.kde.kate", PasteMethod::DirectInsert, true},
                                   {Scope::Application, "firefox", PasteMethod::ClipboardOnly, false}})}}
            << QList<AppEntry>{id("org.kde.kate", std::nullopt, {insertAndKeep, std::nullopt})} << "other" << "";
        QTest::newRow("override and paste rule for one id")
            << QVariantMap{{SettingsKeys::WritingProfileOverrides,
                            QByteArray(R"([{"applicationId":"org.kde.kate","profile":"email","enabled":true}])")},
                           {SettingsKeys::PasteRules,
                            rules({{Scope::Application, "ORG.KDE.KATE", PasteMethod::ClipboardOnly, true}})}}
            << QList<AppEntry>{id("org.kde.kate", WritingProfile::Email, {keepOnly, std::nullopt})} << "other" << "";
        AppEntry both = textApp(QStringLiteral("notepad"), AppCategory::CodeEditor, WritingProfile::Personal);
        both.id.clear();
        both.name = QStringLiteral("Notepad");
        AppEntry typeOnly = textApp(QStringLiteral("phin"), AppCategory::Office);
        typeOnly.id.clear();
        typeOnly.name = QStringLiteral("Phin");
        typeOnly.catalogPicksProfile = true;
        QTest::newRow("recognition rules")
            << QVariantMap{{SettingsKeys::AppRecognitionRules,
                            QByteArray(R"([{"match":"notepad","category":"code_editor","writingProfile":"personal"},)"
                                       R"({"match":"phin","category":"office"},{"match":"  "},{"match":"x"}])")}}
            << QList<AppEntry>{both, typeOnly} << "other" << "";
        QTest::newRow("app type rule")
            << QVariantMap{{SettingsKeys::PasteRules,
                            rules({{Scope::Category, "browser", PasteMethod::ClipboardOnly, true},
                                   {Scope::Category, "email", PasteMethod::StandardPaste, true}})}}
            << QList<AppEntry>{} << "other" << "keep";
        QTest::newRow("default profile")
            << QVariantMap{{SettingsKeys::DefaultWritingProfile, QStringLiteral("personal")}}
            << QList<AppEntry>{} << "personal" << "";
    }

    void migrationShapes()
    {
        QFETCH(QVariantMap, stored);
        QFETCH(QList<AppEntry>, apps);
        QFETCH(QString, otherAppsProfile);
        QFETCH(QString, browserPasting);
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
            settings.setValue(it.key(), it.value());
        }

        migrateApps(settings);

        const AppsSettings migrated = AppsSettingsCodec::load(settings);
        QList<AppEntry> withoutIds;
        for (const AppEntry &app : migrated.apps) {
            QVERIFY(app.id.startsWith(QStringLiteral("app:")));
            withoutIds.append(withoutId(app));
        }
        QCOMPARE(withoutIds, apps);
        QCOMPARE(fallbackWritingProfile(migrated), otherAppsProfile);
        const std::optional<DeliverySteps> browser = appTypeSettings(migrated, AppCategory::Browser).pasting.steps;
        QCOMPARE(browser ? DeliveryPlan{*browser}.route().join(u' ') : QString(), browserPasting);
        // Email's Standard paste rule was no rule.
        QCOMPARE(appTypeSettings(migrated, AppCategory::Email).pasting, PastingChoice{});
        // The old keys stay for a downgrade.
        for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
            QVERIFY(settings.contains(it.key()));
        }
    }

    // A stored paste rule list says which app types had a rule; Terminals'
    // keys were a rule like any other, and one removed stays removed. A list
    // without a global rule turned on copied only wherever no rule applied;
    // an empty list read as the default rules.
    void storedPasteRulesReplaceTheDefaultTypes()
    {
        const auto migrated = [](const QList<LegacyPasteRule> &rules) {
            QTemporaryDir dir;
            QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
            storeLegacyPasteRules(settings, rules);
            migrateApps(settings);
            migrateDeliverySteps(settings);
            const DeliverySteps steps{settings.value(SettingsKeys::InsertThroughAccessibility).toBool(),
                                      settings.value(SettingsKeys::PressPasteKeys).toBool(),
                                      settings.value(SettingsKeys::KeepOnClipboard).toBool()};
            return std::pair{AppsSettingsCodec::load(settings), steps};
        };
        Target konsole = target("org.kde.konsole", "Konsole", "konsole");
        konsole.terminalHost = true;
        const Target firefox = target("firefox", "Firefox", "firefox");
        const Target dolphin = target("org.kde.dolphin", "Dolphin", "dolphin");
        const QString terminal = terminalKeysDiffer ? QStringLiteral("terminal") : QStringLiteral("standard");

        const auto [withoutGlobal, copying] = migrated(
            {{LegacyPasteRule::Scope::Category, "browser", PasteMethod::StandardPaste, true},
             {LegacyPasteRule::Scope::Global, "", PasteMethod::DirectInsert, false}});
        QCOMPARE(copying, keepOnly);
        QCOMPARE(appTypeSettings(withoutGlobal, AppCategory::Terminal).pasting, PastingChoice{});
        QCOMPARE(delivery(konsole, withoutGlobal, copying), QStringLiteral("keep"));
        QCOMPARE(delivery(dolphin, withoutGlobal, copying), QStringLiteral("keep"));
        QCOMPARE(delivery(firefox, withoutGlobal, copying), QStringLiteral("insert press keep standard"));

        const auto [empty, allSteps] = migrated({});
        QCOMPARE(allSteps, DeliverySteps{});
        QCOMPARE(delivery(konsole, empty, allSteps), u"insert press keep " + terminal);
        QCOMPARE(delivery(dolphin, empty, allSteps), QStringLiteral("insert press keep standard"));
    }

    // An older recognition rule that set only a type left the profile to the
    // catalog, as the App migrated from it still does: classifyTarget and
    // resolveWritingProfile answered "general work" for Kate under it.
    void typeOnlyRecognitionRulesLeaveTheProfileToTheCatalog()
    {
        QTemporaryDir dir;
        QSettings stored(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        stored.setValue(SettingsKeys::AppRecognitionRules, QByteArray(R"([{"match":"kate","category":"general"}])"));
        migrateApps(stored);
        AppsSettings apps = AppsSettingsCodec::load(stored);
        const Target kate = target("org.kde.kate", "Kate", "kate", "notes.md — Kate");
        QCOMPARE(answer(resolveApp(kate, apps)), QStringLiteral("general work steps"));
        // An App the person types picks its type's profile.
        apps.apps[0].catalogPicksProfile = false;
        QCOMPARE(resolveApp(kate, apps).writingProfile, WritingProfile::Other);
    }

    // A single app's or app type's rule delivered with the steps the older
    // output method used, whatever the global rule narrowed them to.
    void pasteRulesKeepTheStepsTheGlobalRuleNarrowed()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        settings.setValue(SettingsKeys::OutputMethod, QStringLiteral("automatic"));
        settings.setValue(SettingsKeys::RestoreClipboardAfterTyping, true);
        using Scope = LegacyPasteRule::Scope;
        storeLegacyPasteRules(settings, {
            {Scope::Global, "", PasteMethod::ClipboardOnly, true},
            {Scope::Application, "org.kde.kate", PasteMethod::StandardPaste, true},
            {Scope::Category, "terminal", PasteMethod::TerminalPaste, true},
            {Scope::Category, "browser", PasteMethod::DirectInsert, true},
        });
        migrateApps(settings);
        migrateDeliverySteps(settings);
        const AppsSettings apps = AppsSettingsCodec::load(settings);
        const DeliverySteps steps{settings.value(SettingsKeys::InsertThroughAccessibility).toBool(),
                                  settings.value(SettingsKeys::PressPasteKeys).toBool(),
                                  settings.value(SettingsKeys::KeepOnClipboard).toBool()};
        Target konsole = target("org.kde.konsole", "Konsole", "konsole");
        konsole.terminalHost = true;
        Target agent = konsole;
        agent.aiCodingToolActive = true;
        const QString terminal = terminalKeysDiffer ? QStringLiteral("terminal") : QStringLiteral("standard");
        // What resolvePasteRule and TextDelivery did: Kate's Standard paste
        // inserted and pasted, restoring the clipboard after.
        QCOMPARE(steps, keepOnly);
        QCOMPARE(delivery(target("org.kde.kate", "Kate", "kate"), apps, steps), QStringLiteral("insert press standard"));
        QCOMPARE(delivery(konsole, apps, steps), u"insert press " + terminal);
        QCOMPARE(delivery(agent, apps, steps), u"insert press " + terminal);
        // Direct insertion copied first and kept the words when it failed.
        QCOMPARE(delivery(target("firefox", "Firefox", "firefox"), apps, steps), QStringLiteral("insert keep"));
        QCOMPARE(delivery(target("org.kde.dolphin", "Dolphin", "dolphin"), apps, steps), QStringLiteral("keep"));
    }

    // A file with nothing to migrate gets no Apps keys, and the migration
    // runs once.
    void migrationRunsOnceAndOnlyWhereNeeded()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        migrateApps(settings);
        QVERIFY(!settings.contains(SettingsKeys::AppsEntries));
        QCOMPARE(settings.value(SettingsKeys::AppsMigrationVersion).toInt(), 2);

        settings.setValue(SettingsKeys::DefaultWritingProfile, QStringLiteral("personal"));
        migrateApps(settings);
        QVERIFY(!settings.contains(SettingsKeys::AppsTypes));
    }

    // Stored apps read back as saved; what the codec can't use is dropped,
    // and a type the list leaves out keeps its default.
    void storedAppsReadBack()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        AppsSettings apps;
        apps.apps = {idApp(QStringLiteral("org.kde.kate"), WritingProfile::Email,
                           {DeliverySteps{.insert = false, .press = true, .keep = false}, PasteKeyCombo::Terminal}),
                     textApp(QStringLiteral("gmail"), AppCategory::Email, WritingProfile::Personal)};
        apps.types[0].pasting = {};
        apps.types[1].pasting.steps = keepOnly;
        AppsSettingsCodec::store(settings, apps);
        QCOMPARE(AppsSettingsCodec::load(settings), apps);

        settings.setValue(SettingsKeys::AppsEntries,
                          QByteArray(R"([{"name":"no id","matches":[{"kind":"id","value":"x"}]},)"
                                     R"({"id":"app:1","name":"no matches","matches":[{"kind":"guess","value":"x"}]}])"));
        settings.setValue(SettingsKeys::AppsTypes,
                          QByteArray(R"([{"type":"browser","steps":{"insert":false,"press":false,"keep":false},)"
                                     R"("writingProfile":"work"}])"));
        const AppsSettings read = AppsSettingsCodec::load(settings);
        QVERIFY(read.apps.isEmpty());
        // Steps with none on read as none.
        QCOMPARE(appTypeSettings(read, AppCategory::Browser), (AppTypeSettings{AppCategory::Browser, WritingProfile::Work}));
        QCOMPARE(appTypeSettings(read, AppCategory::Email), appTypeSettings({}, AppCategory::Email));
    }

    void appEditsKeepTheListConsistent()
    {
        AppSettings settings;
        QCOMPARE(addApp(settings, {{}, {}, {}, {{AppMatchKind::Id, QStringLiteral("  ")}}, {}, {}, {}}),
                 EditRefusal::Blank);
        QCOMPARE(addApp(settings, {{}, {}, {}, {{AppMatchKind::Id, QStringLiteral("org.kde.kate")}}, {},
                                   QStringLiteral("custom_gone"), {}}),
                 EditRefusal::Missing);
        QCOMPARE(addApp(settings, {{}, {}, {}, {{AppMatchKind::Id, QStringLiteral("org.kde.kate")}}, {}, {}, {}}),
                 EditRefusal::None);
        const AppEntry kate = settings.apps.apps.first();
        QVERIFY(kate.id.startsWith(QStringLiteral("app:")));
        QCOMPARE(kate.name, QStringLiteral("org.kde.kate"));
        QCOMPARE(addApp(settings, {{}, {}, {}, {{AppMatchKind::Id, QStringLiteral("ORG.KDE.KATE.desktop")}}, {}, {}, {}}),
                 EditRefusal::Taken);
        // The same text by another kind is another match.
        QCOMPARE(addApp(settings, {kate.id, QStringLiteral("Kate"), {}, {{AppMatchKind::Name, QStringLiteral("org.kde.kate")}},
                                   {}, {}, {}}),
                 EditRefusal::None);
        QVERIFY(settings.apps.apps.last().id != kate.id);

        QCOMPARE(editApp(settings, kate.id, [](AppEntry &app) { app.pasting.steps = keepOnly; }), EditRefusal::None);
        QCOMPARE(settings.apps.apps.first().pasting.steps, keepOnly);
        // Steps of its own keep one on.
        QCOMPARE(editApp(settings, kate.id, [](AppEntry &app) { app.pasting.steps = DeliverySteps{false, false, false}; }),
                 EditRefusal::Incomplete);
        QCOMPARE(settings.apps.apps.first().pasting.steps, keepOnly);
        QCOMPARE(editApp(settings, QStringLiteral("app:gone"), [](AppEntry &) {}), EditRefusal::Missing);

        const SettingsEdit undo = removeApp(settings, kate.id);
        QVERIFY(undo);
        QCOMPARE(settings.apps.apps.size(), 1);
        undo(settings);
        QCOMPARE(settings.apps.apps.size(), 2);
        QCOMPARE(settings.apps.apps.first().id, kate.id);
        QVERIFY(!removeApp(settings, QStringLiteral("app:gone")));

        QCOMPARE(editAppType(settings, AppCategory::Browser, [](AppTypeSettings &type) {
                     type.pasting.keys = PasteKeyCombo::Terminal;
                     type.writingProfile = WritingProfile::Work;
                 }),
                 EditRefusal::None);
        QCOMPARE(appTypeSettings(settings.apps, AppCategory::Browser),
                 (AppTypeSettings{AppCategory::Browser, WritingProfile::Work, {std::nullopt, PasteKeyCombo::Terminal}}));
        QCOMPARE(editAppType(settings, AppCategory::Browser,
                             [](AppTypeSettings &type) { type.pasting.steps = DeliverySteps{false, false, false}; }),
                 EditRefusal::Incomplete);
        QCOMPARE(editAppType(settings, AppCategory::Unknown, [](AppTypeSettings &) {}), EditRefusal::Missing);
    }
};

int runAppsTests(int argc, char **argv)
{
    AppsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_apps.moc"
