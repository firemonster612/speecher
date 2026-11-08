// The Apps model: the one resolver, the migration from the three matchers it
// replaced, how Apps are stored, and the edits the Apps page makes.
#include "common/test_suites.h"
#include "common/apps_fixtures.h"

#include "core/AppEdits.h"
#include "core/settings/AppsMigration.h"
#include "core/settings/AppsSettingsCodec.h"
#include "core/settings/SettingsKeys.h"

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
// "custom" settings are the ones customSettings() stores.
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
        {"defaults mac-safari", "general other steps"},
        {"defaults mac-xcode", "general other steps"},
        {"defaults mac-cursor", "ai_coding ai_coding steps"},
        {"defaults mac-notes", "general other steps"},
        {"defaults win-terminal", "terminal work terminal_paste"},
        {"defaults win-outlook", "general other steps"},
        {"defaults win-edge", "general other steps"},
        {"defaults win-word", "general other steps"},
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
        {"custom mac-safari", "general personal steps"},
        {"custom mac-xcode", "general personal steps"},
        {"custom mac-cursor", "ai_coding ai_coding steps"},
        {"custom mac-notes", "general personal steps"},
        {"custom win-terminal", "terminal work terminal_paste"},
        {"custom win-outlook", "general personal clipboard_only"},
        {"custom win-edge", "general personal steps"},
        {"custom win-word", "general personal steps"},
        {"custom win-notepad", "code_editor personal steps"},
        {"custom win-calculator", "general personal steps"},
        {"custom no-identity", "unknown personal steps"},
    };
    return answers;
}

// The old settings the "custom" answers came from, stored as older releases
// stored them: substring recognition rules (one by title only, one setting
// both), overrides (one off), single-app and app type paste rules (one off),
// and a default profile.
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
    });
    stored.setValue(SettingsKeys::DefaultWritingProfile, QStringLiteral("personal"));
}

QString answer(const AppResolution &resolution)
{
    return QStringList{appCategoryName(resolution.type), resolution.writingProfile,
                       resolution.pasting ? pasteMethodName(*resolution.pasting) : QStringLiteral("steps")}
        .join(u' ');
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
                // macOS no default App Type presses them.
                if (!terminalKeysDiffer && scenario == u"defaults") {
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
        QCOMPARE(kate.pastingFrom, AppLayer::Fallback);

        const AppResolution slack = resolveApp(target("slack", "Slack", "slack"), apps);
        QVERIFY(slack.appId.isEmpty());
        QCOMPARE(slack.writingProfileFrom, AppLayer::BuiltIn);

        const AppResolution thunderbird = resolveApp(target("thunderbird", "Thunderbird", "thunderbird"), apps);
        QCOMPARE(thunderbird.writingProfile, WritingProfile::Email);
        QCOMPARE(thunderbird.writingProfileFrom, AppLayer::BuiltIn);

        Target inTerminal = target("org.kde.dolphin", "Dolphin", "dolphin");
        inTerminal.terminalHost = true;
        QCOMPARE(resolveApp(inTerminal, apps).pastingFrom,
                 terminalKeysDiffer ? AppLayer::Terminal : AppLayer::Fallback);
        QCOMPARE(resolveApp(target("org.kde.dolphin", "Dolphin", "dolphin"), apps).writingProfileFrom,
                 AppLayer::Type);
        QCOMPARE(resolveApp(target("", "", ""), apps).writingProfileFrom, AppLayer::Fallback);
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
    }

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
        QCOMPARE(builtInApps().size(), 66);
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
        gmail.pasting = PasteMethod::ClipboardOnly;
        apps.apps = {gmail};
        const AppResolution firefox = resolveApp(target("firefox", "Firefox", "firefox", "Inbox - Gmail"), apps);
        QCOMPARE(firefox.type, AppCategory::Browser);
        QCOMPARE(firefox.writingProfile, WritingProfile::Personal);
        QCOMPARE(firefox.pasting, PasteMethod::ClipboardOnly);
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
                type.pasting = PasteMethod::DirectInsert;
            }
        }
        const Target thunderbird = target("org.mozilla.Thunderbird", "Thunderbird", "thunderbird");
        QCOMPARE(resolveApp(thunderbird, apps).writingProfile, WritingProfile::Personal);
        QCOMPARE(resolveApp(thunderbird, apps).pasting, PasteMethod::DirectInsert);

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
                type.pasting = PasteMethod::ClipboardOnly;
            }
        }
        QCOMPARE(fallbackWritingProfile(apps), WritingProfile::Personal);
        QCOMPARE(resolveApp(target("org.kde.dolphin", "Dolphin", "dolphin"), apps).pasting, PasteMethod::ClipboardOnly);
        const AppResolution unknown = resolveApp(target("", "", ""), apps);
        QCOMPARE(unknown.type, AppCategory::Unknown);
        QCOMPARE(unknown.writingProfile, WritingProfile::Personal);
        QCOMPARE(unknown.pasting, std::nullopt);
        // Browsers follow Other apps' profile until they have their own.
        QCOMPARE(resolveApp(target("firefox", "Firefox", "firefox"), apps).writingProfile, WritingProfile::Personal);
    }

    // An app's own Pasting choice wins over Terminals' rule for anything in
    // a terminal, which wins over the app's type.
    void pastingPrefersTheAppThenTerminalsThenTheType()
    {
        AppsSettings apps;
        for (AppTypeSettings &type : apps.types) {
            if (type.type == AppCategory::Terminal) type.pasting = PasteMethod::TerminalPaste;
            if (type.type == AppCategory::AiCoding) type.pasting = PasteMethod::ClipboardOnly;
        }
        Target agent = target("org.kde.konsole", "Konsole", "konsole");
        agent.terminalHost = true;
        agent.aiCodingToolActive = true;
        QCOMPARE(resolveApp(agent, apps).type, AppCategory::AiCoding);
        QCOMPARE(resolveApp(agent, apps).pasting, PasteMethod::TerminalPaste);
        QCOMPARE(resolveApp(target("", "Cursor", "cursor"), apps).pasting, PasteMethod::ClipboardOnly);
        apps.apps = {idApp(QStringLiteral("org.kde.konsole"), std::nullopt, PasteMethod::StandardPaste)};
        QCOMPARE(resolveApp(agent, apps).pasting, PasteMethod::StandardPaste);
        QCOMPARE(resolveApp(agent, apps).pastingFrom, AppLayer::App);
    }

    void migrationShapes_data()
    {
        QTest::addColumn<QVariantMap>("stored");
        QTest::addColumn<QList<AppEntry>>("apps");
        QTest::addColumn<QString>("otherAppsProfile");
        QTest::addColumn<QString>("browserPasting");

        const auto id = [](const char *id, std::optional<QString> profile, std::optional<PasteMethod> pasting) {
            AppEntry app = idApp(QString::fromUtf8(id), profile, pasting);
            app.id.clear();
            app.name = QString::fromUtf8(id).section(u'.', -1);
            app.name[0] = app.name[0].toUpper();
            return app;
        };
        const auto rules = [](const QList<LegacyPasteRule> &rules) {
            QTemporaryDir dir;
            QSettings settings(dir.filePath(QStringLiteral("rules.ini")), QSettings::IniFormat);
            storeLegacyPasteRules(settings, rules);
            return settings.value(SettingsKeys::PasteRules);
        };
        using Scope = LegacyPasteRule::Scope;
        QTest::newRow("override only")
            << QVariantMap{{SettingsKeys::WritingProfileOverrides,
                            QByteArray(R"([{"applicationId":"org.kde.kate","profile":"email","enabled":true}])")}}
            << QList<AppEntry>{id("org.kde.kate", WritingProfile::Email, std::nullopt)} << "other" << "";
        QTest::newRow("paste rule only")
            << QVariantMap{{SettingsKeys::PasteRules,
                            rules({{Scope::Application, "org.kde.kate", PasteMethod::DirectInsert, true},
                                   {Scope::Application, "firefox", PasteMethod::ClipboardOnly, false}})}}
            << QList<AppEntry>{id("org.kde.kate", std::nullopt, PasteMethod::DirectInsert)} << "other" << "";
        QTest::newRow("override and paste rule for one id")
            << QVariantMap{{SettingsKeys::WritingProfileOverrides,
                            QByteArray(R"([{"applicationId":"org.kde.kate","profile":"email","enabled":true}])")},
                           {SettingsKeys::PasteRules,
                            rules({{Scope::Application, "ORG.KDE.KATE", PasteMethod::ClipboardOnly, true}})}}
            << QList<AppEntry>{id("org.kde.kate", WritingProfile::Email, PasteMethod::ClipboardOnly)} << "other" << "";
        AppEntry both = textApp(QStringLiteral("notepad"), AppCategory::CodeEditor, WritingProfile::Personal);
        both.id.clear();
        both.name = QStringLiteral("Notepad");
        AppEntry typeOnly = textApp(QStringLiteral("phin"), AppCategory::Office);
        typeOnly.id.clear();
        typeOnly.name = QStringLiteral("Phin");
        QTest::newRow("recognition rules")
            << QVariantMap{{SettingsKeys::AppRecognitionRules,
                            QByteArray(R"([{"match":"notepad","category":"code_editor","writingProfile":"personal"},)"
                                       R"({"match":"phin","category":"office"},{"match":"  "},{"match":"x"}])")}}
            << QList<AppEntry>{both, typeOnly} << "other" << "";
        QTest::newRow("app type rule")
            << QVariantMap{{SettingsKeys::PasteRules,
                            rules({{Scope::Category, "browser", PasteMethod::ClipboardOnly, true},
                                   {Scope::Category, "email", PasteMethod::StandardPaste, true}})}}
            << QList<AppEntry>{} << "other" << "clipboard_only";
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
        const std::optional<PasteMethod> browser = appTypeSettings(migrated, AppCategory::Browser).pasting;
        QCOMPARE(browser ? pasteMethodName(*browser) : QString(), browserPasting);
        // Email's Standard paste rule was no rule.
        QCOMPARE(appTypeSettings(migrated, AppCategory::Email).pasting, std::nullopt);
        // The old keys stay for a downgrade.
        for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
            QVERIFY(settings.contains(it.key()));
        }
    }

    // A stored paste rule list says which app types had a rule; Terminals'
    // keys were a rule like any other, and one removed stays removed.
    void storedPasteRulesReplaceTheDefaultTypes()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        storeLegacyPasteRules(settings, {});
        migrateApps(settings);
        QCOMPARE(appTypeSettings(AppsSettingsCodec::load(settings), AppCategory::Terminal).pasting, std::nullopt);
    }

    // A file with nothing to migrate gets no Apps keys, and the migration
    // runs once.
    void migrationRunsOnceAndOnlyWhereNeeded()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        migrateApps(settings);
        QVERIFY(!settings.contains(SettingsKeys::AppsEntries));
        QCOMPARE(settings.value(SettingsKeys::AppsMigrationVersion).toInt(), 1);

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
        apps.apps = {idApp(QStringLiteral("org.kde.kate"), WritingProfile::Email, PasteMethod::StandardPaste),
                     textApp(QStringLiteral("gmail"), AppCategory::Email, WritingProfile::Personal)};
        apps.types[0].pasting.reset();
        AppsSettingsCodec::store(settings, apps);
        QCOMPARE(AppsSettingsCodec::load(settings), apps);

        settings.setValue(SettingsKeys::AppsEntries,
                          QByteArray(R"([{"name":"no id","matches":[{"kind":"id","value":"x"}]},)"
                                     R"({"id":"app:1","name":"no matches","matches":[{"kind":"guess","value":"x"}]}])"));
        settings.setValue(SettingsKeys::AppsTypes,
                          QByteArray(R"([{"type":"browser","pasting":"standard_paste","writingProfile":"work"}])"));
        const AppsSettings read = AppsSettingsCodec::load(settings);
        QVERIFY(read.apps.isEmpty());
        QCOMPARE(appTypeSettings(read, AppCategory::Browser),
                 (AppTypeSettings{AppCategory::Browser, WritingProfile::Work, std::nullopt}));
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

        QCOMPARE(editApp(settings, kate.id, [](AppEntry &app) { app.pasting = PasteMethod::ClipboardOnly; }),
                 EditRefusal::None);
        QCOMPARE(settings.apps.apps.first().pasting, PasteMethod::ClipboardOnly);
        QCOMPARE(editApp(settings, QStringLiteral("app:gone"), [](AppEntry &) {}), EditRefusal::Missing);

        const SettingsEdit undo = removeApp(settings, kate.id);
        QVERIFY(undo);
        QCOMPARE(settings.apps.apps.size(), 1);
        undo(settings);
        QCOMPARE(settings.apps.apps.size(), 2);
        QCOMPARE(settings.apps.apps.first().id, kate.id);
        QVERIFY(!removeApp(settings, QStringLiteral("app:gone")));

        QCOMPARE(editAppType(settings, AppCategory::Browser, [](AppTypeSettings &type) {
                     type.pasting = PasteMethod::StandardPaste;
                     type.writingProfile = WritingProfile::Work;
                 }),
                 EditRefusal::None);
        QCOMPARE(appTypeSettings(settings.apps, AppCategory::Browser),
                 (AppTypeSettings{AppCategory::Browser, WritingProfile::Work, std::nullopt}));
        QCOMPARE(editAppType(settings, AppCategory::Unknown, [](AppTypeSettings &) {}), EditRefusal::Missing);
    }
};

int runAppsTests(int argc, char **argv)
{
    AppsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_apps.moc"
