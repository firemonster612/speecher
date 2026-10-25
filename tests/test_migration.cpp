// An installed user's settings, keyring and files, as an older Speecher left
// them, loaded by this one: each is checked as typed values on first launch,
// then again after one unrelated edit in the Quick UI and a restart, which
// saves the whole settings snapshot.
//
// tests/migration/master-0.2.0.conf is the file the released 0.2.0 AppImage
// wrote itself, after one edit in its own Accounts page. native.conf is that
// file as a later native build (dc62c283^) left it: its migrations, custom
// Writing Profiles, tones and cleanup levels, a local model, and choices it
// left unset. native-insights.jsonl is that build's history format.

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
// installed Speecher's own settings file holds them.
void loadSettingsFile(const QString &name)
{
    const QSettings fixture(fixtureDir + name, QSettings::IniFormat);
    SettingsStore store;
    store.raw().clear();
    for (const QString &key : fixture.allKeys()) {
        store.raw().setValue(key, fixture.value(key));
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
// the whole snapshot.
void editTheme(ApplicationController &controller)
{
    QuickSettings settings(&controller);
    settings.set(QStringLiteral("themeControl"), QStringLiteral("dark"));
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

bool keyringExpected()
{
    return qEnvironmentVariableIntValue("SPEECHER_TEST_EXPECT_KEYRING") == 1;
}

} // namespace

class MigrationTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        // Without the private keyring suite, a keyring that refuses writes
        // stands in for none, and leaves the person's own alone.
        if (!keyringExpected()) {
            qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        }
    }

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
        qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE");
    }

    // A 0.2.0 user's file. Its OpenAI key is in the keyring when there is
    // one, as 0.2.0 saved it, or else in the settings file.
    void masterSettingsSurviveTheUpdate()
    {
        if (keyringExpected()) {
            QVERIFY(SettingsStore().secrets()->saveSecret(SecretStore::Secret::OpenAiApiKey, openAiKey));
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
        editTheme(*controller);
        controller.reset();
        controller = launch();
        checkMasterState(*controller);
        QCOMPARE(controller->settings()->snapshot().ui.theme, QStringLiteral("dark"));
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
        editTheme(*controller);
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
        // The old window's layout is left alone, unread.
        QVERIFY(store.raw().contains(QStringLiteral("ui/appWindow/a/geometry")));

        QCOMPARE(settings.speech.providerId, QStringLiteral("codex"));
        QVERIFY(!settings.speech.codexFinalRetranscribe);
        QCOMPARE(settings.speech.claudeCredentialsPath, QStringLiteral("/tmp/speecher-fixture-claude/.credentials.json"));
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
        QCOMPARE(settings.refinement.openAiAuthMode, QStringLiteral("settings"));
        QCOMPARE(settings.refinement.openAiEffort, QStringLiteral("medium"));
        QCOMPARE(settings.refinement.openAiSpeed, QStringLiteral("standard"));
        QCOMPARE(settings.refinement.openAiCliproxyAccount, QStringLiteral("openai-fixture@example.com"));
        QCOMPARE(settings.refinement.anthropicAuthMode, QStringLiteral("cliproxy"));
        QCOMPARE(settings.refinement.anthropicEffort, QStringLiteral("high"));
        QVERIFY(!settings.refinement.anthropicFastMode);
        QCOMPARE(settings.refinement.anthropicCliproxyAccount, QStringLiteral("claude-fixture@example.com"));
        QCOMPARE(settings.refinement.cliproxyOauthDirConfigured, QStringLiteral("/tmp/speecher-fixture-oauth"));
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
        QVERIFY(settings.refinement.cliproxyOauthDirConfigured.isEmpty());
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
