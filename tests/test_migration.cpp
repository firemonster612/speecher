// An installed user's settings, keyring and files, as an older Speecher left
// them, loaded by this one: each is checked as typed values on first launch,
// then again after one unrelated edit in the Quick UI and a restart, which
// saves the whole settings snapshot.
//
// tests/migration/master-0.2.0.conf is the file the released 0.2.0 AppImage
// wrote itself, after one edit in its own Accounts page. The variant rows
// change a few of its keys to other values 0.2.0 could save. native.conf was
// written by hand, to the keys and types the native build before dc62c283
// saves: its migrations, custom Writing Profiles, tones and cleanup levels, a
// local model, and choices it left unset. No native build wrote it, and no
// native-to-Quick update ran through an updater. native-insights.jsonl is
// that build's history format, also by hand.

#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/InsightsLog.h"
#include "core/LocalModelCatalog.h"
#include "core/PasteRules.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/quick/host/QuickSettings.h"
#include "providers/LocalModelStore.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

#include <functional>
#include <map>
#include <memory>

using namespace speecher;

namespace {

const QString fixtureDir = QStringLiteral(SPEECHER_SOURCE_DIR "/tests/migration/");
const QString openAiKey = QStringLiteral("sk-fixture-openai-0001");
const QString customProfile = QStringLiteral("custom_profile_fixture1");

QString dataFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// Replaces the test store with a fixture file's keys and values, as the
// installed Speecher's own settings file holds them, with changed's keys over
// the file's. A changed key with no value is one the user never saved.
void loadSettingsFile(const QString &name, const QVariantMap &changed = {})
{
    const QSettings fixture(fixtureDir + name, QSettings::IniFormat);
    SettingsStore store;
    store.raw().clear();
    for (const QString &key : fixture.allKeys()) {
        store.raw().setValue(key, fixture.value(key));
    }
    for (auto it = changed.begin(); it != changed.end(); ++it) {
        if (it.value().isValid()) {
            store.raw().setValue(it.key(), it.value());
        } else {
            store.raw().remove(it.key());
        }
    }
    store.raw().sync();
}

// Speecher starting as it does after the update: main's migrations, then the
// controller, which moves keys out of the settings file.
std::unique_ptr<ApplicationController> launch()
{
    migrateSavedSettings();
    return std::make_unique<ApplicationController>(true);
}

// One edit unrelated to anything checked, through the Quick UI, which saves
// the whole snapshot. The color scheme is new in the Quick UI, so no older
// file holds it.
void editColorScheme(ApplicationController &controller)
{
    QuickSettings settings(&controller);
    settings.set(QStringLiteral("colorScheme"), QStringLiteral("fixture-scheme"));
}

const WritingProfileSettings &profile(const AppSettings &settings, const QString &id)
{
    for (const WritingProfileSettings &entry : settings.refinement.writingProfiles) {
        if (entry.profile == id) {
            return entry;
        }
    }
    static const WritingProfileSettings missing{QStringLiteral("missing")};
    return missing;
}

// What a variant row expects, by name, as the running app reads it.
QVariant readField(ApplicationController &controller, const QString &name)
{
    SettingsStore &store = *controller.settings();
    const AppSettings settings = store.snapshot();
    const auto delivery = [&](const Target &target) {
        return pasteMethodName(resolvePasteRule(settings.output.pasteRules, target).method);
    };
    Target terminal;
    terminal.category = AppCategory::Terminal;
    Target kate;
    kate.applicationId = QStringLiteral("org.kde.kate");
    Target unruled;
    unruled.applicationId = QStringLiteral("org.fixture.unruled");
    const std::map<QString, std::function<QVariant()>> fields{
        {QStringLiteral("openAiSpeed"), [&] { return settings.refinement.openAiSpeed; }},
        {QStringLiteral("speechProvider"), [&] { return settings.speech.providerId; }},
        {QStringLiteral("savedSpeechProvider"), [&] { return store.raw().value(SettingsKeys::SpeechProvider); }},
        {QStringLiteral("speechFallbacks"), [&] { return settings.speech.fallbackProviderIds.join(QLatin1Char(',')); }},
        {QStringLiteral("refinementProvider"), [&] { return settings.refinement.providerId; }},
        {QStringLiteral("savedRefinementProvider"), [&] { return store.raw().value(SettingsKeys::RefinementProvider); }},
        {QStringLiteral("refinementFallbacks"),
         [&] { return settings.refinement.fallbackProviderIds.join(QLatin1Char(',')); }},
        {QStringLiteral("outputMethod"), [&] { return settings.output.method; }},
        {QStringLiteral("terminalDelivery"), [&] { return delivery(terminal); }},
        {QStringLiteral("kateDelivery"), [&] { return delivery(kate); }},
        {QStringLiteral("unruledDelivery"), [&] { return delivery(unruled); }},
        {QStringLiteral("cliproxyBaseUrl"), [&] { return settings.refinement.cliproxyBaseUrl; }},
        {QStringLiteral("openAiModel"), [&] { return settings.refinement.openAiModel; }},
        {QStringLiteral("anthropicModel"), [&] { return settings.refinement.anthropicModel; }},
        {QStringLiteral("savedShortcut"), [&] { return store.raw().value(SettingsKeys::GlobalShortcut); }},
        {QStringLiteral("shortcut"), [&] { return controller.globalShortcut().toString(); }},
        {QStringLiteral("theme"), [&] { return settings.ui.theme; }},
        {QStringLiteral("workCleanup"), [&] { return profile(settings, QStringLiteral("work")).cleanupStrength; }},
        {QStringLiteral("workTone"), [&] { return profile(settings, QStringLiteral("work")).tone; }},
        {QStringLiteral("aiCodingCleanup"), [&] { return profile(settings, QStringLiteral("ai_coding")).cleanupStrength; }},
        {QStringLiteral("aiCodingTone"), [&] { return profile(settings, QStringLiteral("ai_coding")).tone; }},
    };
    const auto field = fields.find(name);
    return field == fields.end() ? QVariant(QStringLiteral("no field ") + name) : field->second();
}

void checkFields(ApplicationController &controller, const QVariantMap &expected)
{
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        QCOMPARE(readField(controller, it.key()).toString(), it.value().toString());
    }
}

bool keyringExpected()
{
    return qEnvironmentVariableIntValue("SPEECHER_TEST_PRIVATE_KEYRING") == 1;
}

} // namespace

class MigrationTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        SettingsStore store;
        store.secrets()->deleteKeyringSecrets();
        store.raw().clear();
        // Only what these tests wrote: other test processes share the folder.
        for (const QString &file : {QStringLiteral("insights.jsonl"), QStringLiteral("sentinel.txt")}) {
            QFile::remove(dataFolder() + QLatin1Char('/') + file);
        }
        for (const QString &id : {QStringLiteral("moonshine-small"), QStringLiteral("moonshine-medium")}) {
            const QString model = dataFolder() + QStringLiteral("/models/") + findLocalModel(id)->fileName;
            QFile::remove(model);
            QFile::remove(model + QStringLiteral(".part"));
        }
    }

    // A 0.2.0 user's file. Its OpenAI key is in the keyring when there is
    // one, as 0.2.0 saved it, or else in the settings file.
    void masterSettingsSurviveTheUpdate()
    {
        if (keyringExpected()) {
            QCOMPARE(SettingsStore().secrets()->saveSecret(SecretStore::Secret::OpenAiApiKey, openAiKey),
                     SecretStore::SaveResult::Stored);
        }
        loadSettingsFile(QStringLiteral("master-0.2.0.conf"));
        if (!keyringExpected()) {
            SettingsStore store;
            store.raw().setValue(SettingsKeys::OpenAiApiKey, openAiKey);
            store.raw().sync();
        }

        auto controller = launch();
        checkMasterState(*controller);
        if (QTest::currentTestFailed()) return;
        editColorScheme(*controller);
        controller.reset();
        controller = launch();
        checkMasterState(*controller);
        QCOMPARE(controller->settings()->snapshot().ui.colorScheme, QStringLiteral("fixture-scheme"));
        // The edit saved the whole snapshot; the sign-in keys, which speech
        // and refinement share, went back as the file had them.
        const QSettings fixture(fixtureDir + QStringLiteral("master-0.2.0.conf"), QSettings::IniFormat);
        for (const char *key : {"openai/auth/mode", "anthropic/auth/mode", "openai/cliproxyAccount",
                                "anthropic/cliproxyAccount", "cliproxy/oauthDir", "claude/credentialsPath"}) {
            QVERIFY2(fixture.contains(key), key);
            QCOMPARE(controller->settings()->raw().value(key).toString(), fixture.value(key).toString());
        }
    }

    // Other values a 0.2.0 user can have saved, each over the same file.
    void masterVariantsSurviveTheUpdate_data()
    {
        QTest::addColumn<QVariantMap>("saved");
        QTest::addColumn<QVariantMap>("expected");
        const auto method = [](const char *name) { return QVariantMap{{QStringLiteral("output/method"), name}}; };
        QTest::newRow("fast mode without a speed")
            << QVariantMap{{QStringLiteral("openai/fastMode"), true}}
            << QVariantMap{{QStringLiteral("openAiSpeed"), QStringLiteral("fast")}};
        // Every provider a file names keeps its key. Local, endpoint and the
        // fallbacks came after 0.2.0, in builds that saved the same keys.
        for (const char *provider : {"claude", "codex", "local", "endpoint"}) {
            QTest::addRow("speech provider %s", provider)
                << QVariantMap{{QStringLiteral("stt/provider"), provider}}
                << QVariantMap{{QStringLiteral("speechProvider"), provider},
                               {QStringLiteral("savedSpeechProvider"), provider}};
        }
        for (const char *provider : {"none", "openai", "anthropic", "local", "endpoint"}) {
            QTest::addRow("refinement provider %s", provider)
                << QVariantMap{{QStringLiteral("refinement/provider"), provider}}
                << QVariantMap{{QStringLiteral("refinementProvider"), provider},
                               {QStringLiteral("savedRefinementProvider"), provider}};
        }
        QTest::newRow("fallback providers")
            << QVariantMap{{QStringLiteral("stt/fallbackProviders"), QStringList{"local", "endpoint"}},
                           {QStringLiteral("refinement/fallbackProviders"), QStringList{"endpoint", "local"}}}
            << QVariantMap{{QStringLiteral("speechFallbacks"), QStringLiteral("local,endpoint")},
                           {QStringLiteral("refinementFallbacks"), QStringLiteral("endpoint,local")}};
        // Pasting rules insert or copy only, as they did under the old method.
        QTest::newRow("direct insertion")
            << method("direct_insert")
            << QVariantMap{{QStringLiteral("outputMethod"), QStringLiteral("automatic")},
                           {QStringLiteral("terminalDelivery"), QStringLiteral("direct_insert")},
                           {QStringLiteral("kateDelivery"), QStringLiteral("direct_insert")},
                           {QStringLiteral("unruledDelivery"), QStringLiteral("clipboard_only")}};
        // With no rules saved, the default ones, which paste, insert only.
        QTest::newRow("direct insertion with the default rules")
            << QVariantMap{{QStringLiteral("output/method"), QStringLiteral("direct_insert")},
                           {QStringLiteral("output/pasteRules"), QVariant()}}
            << QVariantMap{{QStringLiteral("outputMethod"), QStringLiteral("automatic")},
                           {QStringLiteral("terminalDelivery"), QStringLiteral("direct_insert")},
                           {QStringLiteral("unruledDelivery"), QStringLiteral("direct_insert")}};
        QTest::newRow("Qt clipboard")
            << method("qt-clipboard")
            << QVariantMap{{QStringLiteral("outputMethod"), QStringLiteral("automatic")},
                           {QStringLiteral("terminalDelivery"), QStringLiteral("clipboard_only")},
                           {QStringLiteral("kateDelivery"), QStringLiteral("clipboard_only")},
                           {QStringLiteral("unruledDelivery"), QStringLiteral("clipboard_only")}};
        QTest::newRow("proxy URL ending in /v1/")
            << QVariantMap{{QStringLiteral("cliproxy/baseUrl"), QStringLiteral("http://127.0.0.1:8317/v1/")}}
            << QVariantMap{{QStringLiteral("cliproxyBaseUrl"), QStringLiteral("http://127.0.0.1:8317")}};
        QTest::newRow("models chosen over the defaults")
            << QVariantMap{{QStringLiteral("openai/model"), QStringLiteral("gpt-6-sol")},
                           {QStringLiteral("anthropic/model"), QStringLiteral("claude-fable-5")}}
            << QVariantMap{{QStringLiteral("openAiModel"), QStringLiteral("gpt-6-sol")},
                           {QStringLiteral("anthropicModel"), QStringLiteral("claude-fable-5")}};
        // On Linux the desktop keeps the combination, not this file; the
        // AppImage update script checks KGlobalAccel's.
        QVariantMap shortcut{{QStringLiteral("savedShortcut"), QStringLiteral("Meta+Alt+K")}};
#ifndef Q_OS_LINUX
        shortcut.insert(QStringLiteral("shortcut"), QStringLiteral("Meta+Alt+K"));
#endif
        QTest::newRow("saved global shortcut")
            << QVariantMap{{SettingsKeys::GlobalShortcut, QStringLiteral("Meta+Alt+K")}} << shortcut;
        QTest::newRow("light theme")
            << QVariantMap{{QStringLiteral("ui/theme"), QStringLiteral("light")}}
            << QVariantMap{{QStringLiteral("theme"), QStringLiteral("light")}};
        QTest::newRow("AI coding tuned apart from work")
            << QVariantMap{{QStringLiteral("refinement/writingProfiles"),
                            QByteArray(R"([{"cleanupStrength":"light_cleanup","profile":"work","tone":"formal"},)"
                                       R"({"cleanupStrength":"balanced","profile":"ai_coding","tone":"casual"}])")}}
            << QVariantMap{{QStringLiteral("workCleanup"), QStringLiteral("light_cleanup")},
                           {QStringLiteral("workTone"), QStringLiteral("formal")},
                           {QStringLiteral("aiCodingCleanup"), QStringLiteral("balanced")},
                           {QStringLiteral("aiCodingTone"), QStringLiteral("casual")}};
    }

    void masterVariantsSurviveTheUpdate()
    {
        QFETCH(QVariantMap, saved);
        QFETCH(QVariantMap, expected);
        loadSettingsFile(QStringLiteral("master-0.2.0.conf"), saved);

        auto controller = launch();
        checkFields(*controller, expected);
        if (QTest::currentTestFailed()) return;
        editColorScheme(*controller);
        controller.reset();
        controller = launch();
        checkFields(*controller, expected);
    }

    void nativeSettingsHistoryAndModelsSurviveTheUpdate()
    {
        loadSettingsFile(QStringLiteral("native.conf"));
        const QByteArray history = readAll(fixtureDir + QStringLiteral("native-insights.jsonl"));
        QVERIFY(writeFile(dataFolder() + QStringLiteral("/insights.jsonl"), history));
        const LocalModel *small = findLocalModel(QStringLiteral("moonshine-small"));
        const LocalModel *medium = findLocalModel(QStringLiteral("moonshine-medium"));
        QVERIFY(small && medium);
        const QString models = dataFolder() + QStringLiteral("/models/");
        QVERIFY(writeFile(models + small->fileName, {}));
        QVERIFY(QFile::resize(models + small->fileName, small->sizeBytes));
        const QByteArray partial(4096, 'x');
        QVERIFY(writeFile(models + medium->fileName + QStringLiteral(".part"), partial));
        QVERIFY(writeFile(dataFolder() + QStringLiteral("/sentinel.txt"), "kept"));

        const auto checkFiles = [&](ApplicationController &controller) {
            const QList<DictationRecord> &records = controller.insightsLog()->records();
            QCOMPARE(records.size(), 3);
            QCOMPARE(records[0].appName, QStringLiteral("Kate"));
            QCOMPARE(records[0].profile, QStringLiteral("work"));
            QVERIFY(records[0].speechProviders.isEmpty());
            QCOMPARE(records[1].profile, customProfile);
            QCOMPARE(records[1].profileName, QStringLiteral("Lab notes"));
            QCOMPARE(records[1].speechProviders, QStringList{QStringLiteral("local")});
            QCOMPARE(records[1].refinementProviders, QStringList{QStringLiteral("anthropic")});
            QCOMPARE(records[2].audioMs, 2100);
            QCOMPARE(readAll(dataFolder() + QStringLiteral("/insights.jsonl")), history);
            QVERIFY(controller.localModelStore()->isDownloaded(*small));
            QCOMPARE(readAll(models + medium->fileName + QStringLiteral(".part")), partial);
            QCOMPARE(readAll(dataFolder() + QStringLiteral("/sentinel.txt")), QByteArray("kept"));
        };

        auto controller = launch();
        checkNativeState(*controller);
        checkFiles(*controller);
        if (QTest::currentTestFailed()) return;
        editColorScheme(*controller);
        controller.reset();
        controller = launch();
        checkNativeState(*controller);
        checkFiles(*controller);
    }

private:
    void checkMasterState(ApplicationController &controller)
    {
        SettingsStore &store = *controller.settings();
        const AppSettings settings = store.snapshot();
        QVERIFY(settings.setupCompleted);
        QVERIFY(settings.launchAtLogin);

        QVERIFY(!settings.ui.transcriptionPreviewEnabled);
        QVERIFY(!settings.ui.refinementPreviewEnabled);
        QCOMPARE(settings.ui.previewWords, 12);
        QVERIFY(settings.ui.pauseMediaDuringTranscription);
        QVERIFY(!settings.ui.soundsEnabled);
        QCOMPARE(settings.ui.theme, QStringLiteral("system"));
        // The old window's layout is left alone, unread.
        QVERIFY(store.raw().contains(QStringLiteral("ui/appWindow/a/geometry")));

        QCOMPARE(settings.speech.providerId, QStringLiteral("codex"));
        QVERIFY(!settings.speech.codexFinalRetranscribe);
        QCOMPARE(settings.accounts.claudeCredentialsPath, QStringLiteral("/tmp/speecher-fixture-claude/.credentials.json"));
        QCOMPARE(settings.speech.claudeEndpointBase, QStringLiteral("https://claude.fixture.test"));
        QCOMPARE(settings.speech.claudeVoicePath, QStringLiteral("/v1/voice-fixture"));

        QCOMPARE(settings.audio.deviceId, QStringLiteral("alsa_input.fixture-mic"));
        QCOMPARE(settings.audio.mode, QStringLiteral("warm"));
        QVERIFY(settings.audio.vadEnabled);
        QCOMPARE(settings.audio.preRollMs, 400);
        QCOMPARE(settings.audio.postRollMs, 300);
        QCOMPARE(settings.audio.readinessTimeoutMs, 1200);
        QCOMPARE(settings.audio.vadThresholdPercent, 5);

        // Every term is kept, past what a request carries.
        QCOMPARE(settings.vocabulary.size(), 262);
        QCOMPARE(settings.vocabulary[0].term, QStringLiteral("Speecher"));
        QVERIFY(settings.vocabulary[0].starred);
        QCOMPARE(settings.vocabulary[0].frequency, 7);
        QCOMPARE(settings.vocabulary[0].lastUsedMs, 1757200000000);
        QCOMPARE(settings.vocabulary[1].source, QStringLiteral("learned"));
        QVERIFY(settings.vocabulary[1].keyTerm);

        QCOMPARE(settings.appRecognitionRules.size(), 2);
        QCOMPARE(settings.appRecognitionRules[0].match, QStringLiteral("org.kde.konsole"));
        QCOMPARE(settings.appRecognitionRules[0].category, std::optional(AppCategory::Terminal));
        QCOMPARE(settings.appRecognitionRules[0].writingProfile, std::optional(QStringLiteral("ai_coding")));
        QCOMPARE(settings.appRecognitionRules[1].writingProfile, std::optional(QStringLiteral("email")));
        QCOMPARE(settings.bindings.size(), 1);
        QCOMPARE(settings.bindings[0].phrase, QStringLiteral("smiley face"));
        QCOMPARE(settings.bindings[0].replacement, QStringLiteral(":)"));
        QCOMPARE(settings.shortcutActivationMode, ShortcutActivationMode::Toggle);
        QVERIFY(!settings.correctionLearningEnabled);
        QCOMPARE(settings.learnedCorrections.size(), 1);
        QCOMPARE(settings.learnedCorrections[0].corrected, QStringLiteral("Speecher"));
        QCOMPARE(settings.learnedCorrections[0].evidenceCount, 3);
        QCOMPARE(settings.learnedCorrections[0].lastObservedAtMs, 1757100000000);

        QCOMPARE(settings.refinement.providerId, QStringLiteral("anthropic"));
        QCOMPARE(settings.refinement.defaultWritingProfile, QStringLiteral("work"));
        QCOMPARE(profile(settings, QStringLiteral("work")).cleanupStrength, QStringLiteral("light_cleanup"));
        QCOMPARE(profile(settings, QStringLiteral("work")).tone, QStringLiteral("formal"));
        QCOMPARE(profile(settings, QStringLiteral("email")).cleanupStrength, QStringLiteral("strong_polish"));
        QCOMPARE(profile(settings, QStringLiteral("personal")).tone, QStringLiteral("gen_z"));
        QCOMPARE(profile(settings, QStringLiteral("ai_coding")).tone, QStringLiteral("formal"));
        QCOMPARE(settings.refinement.writingProfileOverrides.size(), 2);
        QCOMPARE(settings.refinement.writingProfileOverrides[0].profile, QStringLiteral("personal"));
        QVERIFY(!settings.refinement.writingProfileOverrides[1].enabled);
        QVERIFY(!settings.refinement.useTargetContext);
        QVERIFY(settings.refinement.includeScreenshotContext);

        // 0.2.0's default models move to the ones that replaced them.
        QCOMPARE(settings.refinement.openAiModel, QStringLiteral("gpt-6-luna"));
        QCOMPARE(settings.refinement.anthropicModel, QStringLiteral("claude-opus-5-5"));
        QCOMPARE(settings.accounts.openAiAuthMode, OpenAiAuthMode::Settings);
        QCOMPARE(settings.refinement.openAiEffort, QStringLiteral("medium"));
        QCOMPARE(settings.refinement.openAiSpeed, QStringLiteral("standard"));
        QCOMPARE(settings.accounts.openAiCliproxyAccount, QStringLiteral("openai-fixture@example.com"));
        QCOMPARE(settings.accounts.anthropicAuthMode, AnthropicAuthMode::Cliproxy);
        QCOMPARE(settings.refinement.anthropicEffort, QStringLiteral("high"));
        QVERIFY(!settings.refinement.anthropicFastMode);
        QCOMPARE(settings.accounts.anthropicCliproxyAccount, QStringLiteral("claude-fixture@example.com"));
        QCOMPARE(settings.accounts.cliproxyOauthDirConfigured, QStringLiteral("/tmp/speecher-fixture-oauth"));
        QCOMPARE(settings.refinement.cliproxyBaseUrl, QStringLiteral("http://127.0.0.1:8317"));

        QCOMPARE(settings.refinement.cliproxyApiKey, QStringLiteral("fixture-proxy-key"));
        QCOMPARE(store.secrets()->apiKey(), openAiKey);
        // A key moves out of the settings file only once the keyring holds it.
        QCOMPARE(store.raw().contains(SettingsKeys::CliproxyApiKey), !keyringExpected());

        // Copying only becomes Automatic with every pasting rule copying only,
        // and an app with no rule still gets the clipboard.
        QCOMPARE(settings.output.method, QStringLiteral("automatic"));
        const QList<PasteRule> rules{
            {PasteRuleScope::Category, QStringLiteral("terminal"), PasteMethod::ClipboardOnly, true},
            {PasteRuleScope::Application, QStringLiteral("org.kde.kate"), PasteMethod::ClipboardOnly, true},
            {PasteRuleScope::Application, QStringLiteral("firefox"), PasteMethod::ClipboardOnly, false},
        };
        QCOMPARE(settings.output.pasteRules, rules);
        QCOMPARE(resolvePasteRule(settings.output.pasteRules, Target{}).method, PasteMethod::ClipboardOnly);
        QCOMPARE(settings.output.format, OutputFormat::Html);
        QVERIFY(settings.output.restoreClipboardAfterTyping);
        QCOMPARE(settings.output.completionStatusDurationMs, 1500);

        QCOMPARE(settings.updates.channel, UpdateChannel::Stable);
        QVERIFY(settings.updates.autoCheck);
        QVERIFY(settings.updates.autoInstall);
        QCOMPARE(settings.updates.checkIntervalMinutes, 120);
    }

    void checkNativeState(ApplicationController &controller)
    {
        SettingsStore &store = *controller.settings();
        const AppSettings settings = store.snapshot();
        QVERIFY(settings.setupCompleted);

        // Chosen again after the first model migration, Sonnet 5 moves to 5.5.
        QCOMPARE(settings.refinement.anthropicModel, QStringLiteral("claude-sonnet-5-5"));
        QCOMPARE(settings.refinement.openAiModel, QStringLiteral("gpt-6-luna"));
        // A saved speed wins over the old fast-mode switch.
        QCOMPARE(settings.refinement.openAiSpeed, QStringLiteral("standard"));

        // What the person never chose stays unchosen.
        QVERIFY(!store.raw().contains(SettingsKeys::RefinementProvider));
        QVERIFY(store.raw().value(SettingsKeys::CliproxyOauthDir).toString().isEmpty());
        QVERIFY(settings.accounts.cliproxyOauthDirConfigured.isEmpty());
        QVERIFY(settings.speech.codexFinalRetranscribe);

        const WritingProfileSettings &lab = profile(settings, customProfile);
        QCOMPARE(lab.name, QStringLiteral("Lab notes"));
        QCOMPARE(lab.instructions, QStringLiteral("Write lab notes."));
        QCOMPARE(lab.outputLanguage, QStringLiteral("Spanish"));
        QCOMPARE(lab.cleanupStrength, QStringLiteral("custom_level_fixture1"));
        QCOMPARE(lab.tone, QStringLiteral("custom_tone_fixture1"));
        QCOMPARE(lab.speechProvider, QStringLiteral("local"));
        QCOMPARE(lab.speechModel, QStringLiteral("moonshine-small"));
        QCOMPARE(lab.refinementProvider, QStringLiteral("anthropic"));
        QCOMPARE(lab.refinementModel, QStringLiteral("claude-haiku-5-5"));
        QCOMPARE(profile(settings, QStringLiteral("work")).instructions, QStringLiteral("Use the team glossary."));
        QCOMPARE(settings.refinement.customTones,
                 (QList<CustomTone>{{QStringLiteral("custom_tone_fixture1"), QStringLiteral("Pirate"),
                                     QStringLiteral("Talk like a pirate.")}}));
        QCOMPARE(settings.refinement.customCleanupLevels,
                 (QList<CustomCleanupLevel>{{QStringLiteral("custom_level_fixture1"), QStringLiteral("Light plus"),
                                             QStringLiteral("light_cleanup"), QStringLiteral("Keep the filler words.")}}));
        QCOMPARE(settings.refinement.defaultWritingProfile, customProfile);
        QCOMPARE(settings.refinement.writingProfileOverrides,
                 (QList<WritingProfileOverride>{{QStringLiteral("org.kde.kate"), customProfile, true}}));
        QCOMPARE(settings.appRecognitionRules.size(), 1);
        QCOMPARE(settings.appRecognitionRules[0].writingProfile, std::optional(customProfile));

        QCOMPARE(settings.speech.providerId, QStringLiteral("local"));
        QCOMPARE(settings.speech.fallbackProviderIds, QStringList{QStringLiteral("codex")});
        QCOMPARE(settings.speech.language, QStringLiteral("de"));
        QCOMPARE(settings.speech.local.modelId, QStringLiteral("moonshine-small"));
        QVERIFY(settings.speech.local.modelChosen);
        // Read as native read it: the server, without the /v1 its path adds.
        QCOMPARE(settings.speech.endpoint.baseUrl, QStringLiteral("http://127.0.0.1:9"));
        QCOMPARE(settings.speech.endpoint.model, QStringLiteral("whisper-fixture"));
        QCOMPARE(store.secrets()->secret(SecretStore::Secret::SpeechEndpointKey), QStringLiteral("fixture-speech-key"));

        QCOMPARE(settings.vocabulary.size(), 262);
        QCOMPARE(settings.vocabulary[0].context, QStringLiteral("The app this fixture tests."));
        QCOMPARE(settings.vocabulary[0].profiles, QStringList{customProfile});
        QVERIFY(!settings.vocabulary[1].keyTerm);

        QVERIFY(settings.insightsEnabled);
        QCOMPARE(settings.output.method, QStringLiteral("automatic"));
        QCOMPARE(resolvePasteRule(settings.output.pasteRules, Target{}).method, PasteMethod::DirectInsert);
        // 45 days, which the interval now caps at 30.
        QCOMPARE(store.updateCheckIntervalMinutes(), 30 * 24 * 60);
    }
};

int runMigrationTests(int argc, char **argv)
{
    MigrationTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_migration.moc"
