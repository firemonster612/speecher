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

#include "common/test_secrets.h"
#include "common/vocabulary_entries.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/ProviderChain.h"
#include "core/history/HistoryStore.h"
#include "core/LocalModelCatalog.h"
#include "core/PasteMethod.h"
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

// An App as migration made it, without the random part of its id.
AppEntry withoutId(AppEntry app)
{
    if (!app.id.startsWith(QStringLiteral("app:"))) {
        return app;
    }
    app.id.clear();
    return app;
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
    // The steps a delivery to target takes, in order.
    const auto delivery = [&](const Target &target) {
        return deliveryPlan(settings.output.steps,
                            resolveApp(target, settings.apps).pasting.value_or(PasteMethod::StandardPaste))
            .route()
            .join(u' ');
    };
    Target terminal;
    terminal.terminalHost = true;
    Target kate;
    kate.applicationId = QStringLiteral("org.kde.kate");
    Target unruled;
    unruled.applicationId = QStringLiteral("org.fixture.unruled");
    const std::map<QString, std::function<QVariant()>> fields{
        {QStringLiteral("openAiSpeed"), [&] { return settings.refinement.services.choice(QStringLiteral("openai")).speed; }},
        {QStringLiteral("speechProvider"), [&] { return settings.speech.services.main(); }},
        {QStringLiteral("savedSpeechProvider"), [&] { return store.raw().value(SettingsKeys::SpeechProvider); }},
        {QStringLiteral("speechFallbacks"), [&] { return settings.speech.services.order.mid(1).join(QLatin1Char(',')); }},
        // "none" once Clean up my words is off.
        {QStringLiteral("refinementProvider"),
         [&] { return serviceChain(settings.refinement).value(0, kNoRefinementProvider); }},
        {QStringLiteral("savedRefinementProvider"), [&] { return store.raw().value(SettingsKeys::RefinementProvider); }},
        {QStringLiteral("refinementFallbacks"),
         [&] { return settings.refinement.services.order.mid(1).join(QLatin1Char(',')); }},
        {QStringLiteral("deliverySteps"), [&] { return DeliveryPlan{settings.output.steps}.route().join(u' '); }},
        {QStringLiteral("terminalDelivery"), [&] { return delivery(terminal); }},
        {QStringLiteral("kateDelivery"), [&] { return delivery(kate); }},
        {QStringLiteral("unruledDelivery"), [&] { return delivery(unruled); }},
        {QStringLiteral("cliproxyBaseUrl"), [&] { return settings.accounts.cliproxyBaseUrl; }},
        {QStringLiteral("openAiModel"), [&] { return settings.refinement.services.choice(QStringLiteral("openai")).model; }},
        {QStringLiteral("anthropicModel"), [&] { return settings.refinement.services.choice(QStringLiteral("anthropic")).model; }},
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
        removedSecrets(store.secrets());
        store.raw().clear();
        // Only what these tests wrote: other test processes share the folder.
        for (const QString &file :
             {QStringLiteral("insights.jsonl"), QStringLiteral("insights.jsonl.imported"), QStringLiteral("sentinel.txt"),
              QStringLiteral("history.sqlite3"), QStringLiteral("history.sqlite3-wal"), QStringLiteral("history.sqlite3-shm")}) {
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
            QVERIFY(savedSecret(SettingsStore().secrets(), SecretStore::Secret::OpenAiApiKey, openAiKey));
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
        // The steps the old method never used are off, and no rule turns them
        // back on. The file restores the clipboard, so Keep is off where
        // something else delivers.
        QTest::newRow("direct insertion")
            << method("direct_insert")
            << QVariantMap{{QStringLiteral("deliverySteps"), QStringLiteral("insert")},
                           {QStringLiteral("terminalDelivery"), QStringLiteral("insert")},
                           {QStringLiteral("kateDelivery"), QStringLiteral("insert")},
                           {QStringLiteral("unruledDelivery"), QStringLiteral("insert")}};
        QTest::newRow("direct insertion with the default rules")
            << QVariantMap{{QStringLiteral("output/method"), QStringLiteral("direct_insert")},
                           {QStringLiteral("output/pasteRules"), QVariant()}}
            << QVariantMap{{QStringLiteral("deliverySteps"), QStringLiteral("insert")},
                           {QStringLiteral("terminalDelivery"), QStringLiteral("insert")},
                           {QStringLiteral("unruledDelivery"), QStringLiteral("insert")}};
        QTest::newRow("Qt clipboard")
            << method("qt-clipboard")
            << QVariantMap{{QStringLiteral("deliverySteps"), QStringLiteral("keep")},
                           {QStringLiteral("terminalDelivery"), QStringLiteral("keep")},
                           {QStringLiteral("kateDelivery"), QStringLiteral("keep")},
                           {QStringLiteral("unruledDelivery"), QStringLiteral("keep")}};
        QTest::newRow("automatic")
            << method("automatic")
            << QVariantMap{{QStringLiteral("deliverySteps"), QStringLiteral("insert press")},
                           {QStringLiteral("kateDelivery"), QStringLiteral("insert press")}};
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

    void whatSpeecherKeepsFollowsKeepInsights_data()
    {
        QTest::addColumn<QVariant>("insights");
        QTest::addColumn<bool>("setupCompleted");
        QTest::addColumn<HistoryKeep>("keeps");
        QTest::newRow("insights on") << QVariant(true) << true << HistoryKeep::Stats;
        QTest::newRow("insights off") << QVariant(false) << true << HistoryKeep::Nothing;
        QTest::newRow("never set, after setup") << QVariant() << true << HistoryKeep::Stats;
        QTest::newRow("new install") << QVariant() << false << HistorySettings().keeps;
    }

    // An upgrade never starts keeping text behind Keep insights' promise of
    // "never text or audio"; a new install keeps the default.
    void whatSpeecherKeepsFollowsKeepInsights()
    {
        QFETCH(QVariant, insights);
        QFETCH(bool, setupCompleted);
        QFETCH(HistoryKeep, keeps);
        {
            SettingsStore store;
            store.raw().clear();
            if (insights.isValid()) {
                store.raw().setValue(QStringLiteral("insights/enabled"), insights);
            }
            store.raw().setValue(QStringLiteral("app/setupCompleted"), setupCompleted);
            store.raw().sync();
        }
        migrateSavedSettings();
        QCOMPARE(SettingsStore().snapshot().history.keeps, keeps);
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
            const QList<DictationRecord> &records = controller.history()->records();
            QCOMPARE(records.size(), 3);
            QCOMPARE(records[0].appName, QStringLiteral("Kate"));
            QCOMPARE(records[0].profile, QStringLiteral("work"));
            QVERIFY(records[0].speechProviders.isEmpty());
            QCOMPARE(records[1].profile, customProfile);
            QCOMPARE(records[1].profileName, QStringLiteral("Lab notes"));
            QCOMPARE(records[1].speechProviders, QStringList{QStringLiteral("local")});
            QCOMPARE(records[1].refinementProviders, QStringList{QStringLiteral("anthropic")});
            QCOMPARE(records[2].audioMs, 2100);
            // Imported once into History, and kept under another name.
            QVERIFY(!QFile::exists(dataFolder() + QStringLiteral("/insights.jsonl")));
            QCOMPARE(readAll(dataFolder() + QStringLiteral("/insights.jsonl.imported")), history);
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
        // The main thread's snapshot only starts a keyring read.
        readSecret(store.secrets(), SecretStore::Secret::CliproxyApiKey);
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

        QCOMPARE(settings.speech.services.main(), QStringLiteral("codex"));
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

        // Every term is kept, past what a request carries, beside the
        // replacement and the Learned Correction.
        QCOMPARE(settings.vocabulary.size(), 264);
        QCOMPARE(test::spellingEntries(settings.vocabulary).size(), 262);
        QCOMPARE(settings.vocabulary[0].term, QStringLiteral("Speecher"));
        QVERIFY(settings.vocabulary[0].starred);
        QCOMPARE(settings.vocabulary[0].frequency, 7);
        QCOMPARE(settings.vocabulary[0].lastUsedMs, 1757200000000);
        QCOMPARE(settings.vocabulary[1].source, QStringLiteral("learned"));
        QVERIFY(settings.vocabulary[1].keyTerm);

        // The override and the paste rule for Kate are one App matched by its
        // id; the ones turned off are gone. Each recognition rule is an App
        // matched by name, and by title where it set a profile.
        QCOMPARE(settings.apps.apps.size(), 3);
        QCOMPARE(withoutId(settings.apps.apps[0]),
                 (AppEntry{{}, QStringLiteral("Kate"), {}, {{AppMatchKind::Id, QStringLiteral("org.kde.kate")}},
                           std::nullopt, QStringLiteral("personal"), PasteMethod::StandardPaste}));
        QCOMPARE(withoutId(settings.apps.apps[1]),
                 (AppEntry{{}, QStringLiteral("Konsole"), {},
                           {{AppMatchKind::Name, QStringLiteral("org.kde.konsole")},
                            {AppMatchKind::WindowTitle, QStringLiteral("org.kde.konsole")}},
                           AppCategory::Terminal, QStringLiteral("ai_coding"), std::nullopt}));
        QCOMPARE(withoutId(settings.apps.apps[2]),
                 (AppEntry{{}, QStringLiteral("Thunderbird"), {},
                           {{AppMatchKind::Name, QStringLiteral("thunderbird")},
                            {AppMatchKind::WindowTitle, QStringLiteral("thunderbird")}},
                           std::nullopt, QStringLiteral("email"), std::nullopt}));
        // The default profile is Other apps'.
        QCOMPARE(fallbackWritingProfile(settings.apps), QStringLiteral("work"));
        const QList<VocabularyEntry> replacements = test::replacementEntries(settings.vocabulary);
        QCOMPARE(replacements.size(), 1);
        QCOMPARE(replacements[0].term, QStringLiteral("smiley face"));
        QCOMPARE(replacements[0].replacement, QStringLiteral(":)"));
        QCOMPARE(settings.shortcutActivationMode, ShortcutActivationMode::Toggle);
        QVERIFY(!settings.correctionLearningEnabled);
        const QList<VocabularyEntry> learned = test::learnedEntries(settings.vocabulary);
        QCOMPARE(learned.size(), 1);
        QCOMPARE(learned[0].replacement, QStringLiteral("Speecher"));
        QCOMPARE(learned[0].evidenceCount, 3);
        QCOMPARE(learned[0].lastObservedAtMs, 1757100000000);

        QCOMPARE(settings.refinement.services.main(), QStringLiteral("anthropic"));
        QCOMPARE(profile(settings, QStringLiteral("work")).cleanupStrength, QStringLiteral("light_cleanup"));
        QCOMPARE(profile(settings, QStringLiteral("work")).tone, QStringLiteral("formal"));
        QCOMPARE(profile(settings, QStringLiteral("email")).cleanupStrength, QStringLiteral("strong_polish"));
        QCOMPARE(profile(settings, QStringLiteral("personal")).tone, QStringLiteral("gen_z"));
        QCOMPARE(profile(settings, QStringLiteral("ai_coding")).tone, QStringLiteral("formal"));
        QVERIFY(!settings.refinement.useTargetContext);
        QVERIFY(settings.refinement.includeScreenshotContext);

        // 0.2.0's default models move to the ones that replaced them.
        QCOMPARE(settings.refinement.services.choice(QStringLiteral("openai")).model, QStringLiteral("gpt-6-luna"));
        QCOMPARE(settings.refinement.services.choice(QStringLiteral("anthropic")).model, QStringLiteral("claude-opus-5-5"));
        QCOMPARE(settings.accounts.openAiAuthMode, OpenAiAuthMode::Settings);
        QCOMPARE(settings.refinement.services.choice(QStringLiteral("openai")).effort, QStringLiteral("medium"));
        QCOMPARE(settings.refinement.services.choice(QStringLiteral("openai")).speed, QStringLiteral("standard"));
        QCOMPARE(settings.accounts.openAiCliproxyAccount, QStringLiteral("openai-fixture@example.com"));
        QCOMPARE(settings.accounts.anthropicAuthMode, AnthropicAuthMode::Cliproxy);
        QCOMPARE(settings.refinement.services.choice(QStringLiteral("anthropic")).effort, QStringLiteral("high"));
        QCOMPARE(settings.refinement.services.choice(QStringLiteral("anthropic")).speed, QStringLiteral("standard"));
        QCOMPARE(settings.accounts.anthropicCliproxyAccount, QStringLiteral("claude-fixture@example.com"));
        QCOMPARE(settings.accounts.cliproxyOauthDirConfigured, QStringLiteral("/tmp/speecher-fixture-oauth"));
        QCOMPARE(settings.accounts.cliproxyBaseUrl, QStringLiteral("http://127.0.0.1:8317"));

        QCOMPARE(settings.accounts.cliproxyApiKey, QStringLiteral("fixture-proxy-key"));
        QCOMPARE(readSecret(store.secrets(), SecretStore::Secret::OpenAiApiKey), openAiKey);
        // A key moves out of the settings file only once the keyring holds it.
        QCOMPARE(store.raw().contains(SettingsKeys::CliproxyApiKey), !keyringExpected());

        // Copying only leaves Keep alone on, which no rule can widen; restoring
        // the clipboard meant nothing without a paste.
        QCOMPARE(settings.output.steps, (DeliverySteps{.insert = false, .press = false, .keep = true}));
        // Terminals' rule is their Pasting choice; macOS has none.
        QCOMPARE(appTypeSettings(settings.apps, AppCategory::Terminal).pasting,
                 terminalKeysDiffer ? std::optional(PasteMethod::TerminalPaste) : std::nullopt);
        QCOMPARE(settings.output.format, OutputFormat::Html);
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
        QCOMPARE(settings.refinement.services.choice(QStringLiteral("anthropic")).model, QStringLiteral("claude-sonnet-5-5"));
        QCOMPARE(settings.refinement.services.choice(QStringLiteral("openai")).model, QStringLiteral("gpt-6-luna"));
        // A saved speed wins over the old fast-mode switch.
        QCOMPARE(settings.refinement.services.choice(QStringLiteral("openai")).speed, QStringLiteral("standard"));

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
        QCOMPARE(lab.speechFirst, (ServiceChoice{QStringLiteral("local"), QStringLiteral("moonshine-small")}));
        QCOMPARE(lab.refinementFirst, (ServiceChoice{QStringLiteral("anthropic"), QStringLiteral("claude-haiku-5-5")}));
        QCOMPARE(profile(settings, QStringLiteral("work")).instructions, QStringLiteral("Use the team glossary."));
        QCOMPARE(settings.refinement.customTones,
                 (QList<CustomTone>{{QStringLiteral("custom_tone_fixture1"), QStringLiteral("Pirate"),
                                     QStringLiteral("Talk like a pirate.")}}));
        QCOMPARE(settings.refinement.customCleanupLevels,
                 (QList<CustomCleanupLevel>{{QStringLiteral("custom_level_fixture1"), QStringLiteral("Light plus"),
                                             QStringLiteral("light_cleanup"), QStringLiteral("Keep the filler words.")}}));
        QCOMPARE(fallbackWritingProfile(settings.apps), customProfile);
        QCOMPARE(settings.apps.apps.size(), 2);
        QCOMPARE(withoutId(settings.apps.apps[0]),
                 (AppEntry{{}, QStringLiteral("Kate"), {}, {{AppMatchKind::Id, QStringLiteral("org.kde.kate")}},
                           std::nullopt, customProfile, std::nullopt}));
        QCOMPARE(withoutId(settings.apps.apps[1]),
                 (AppEntry{{}, QStringLiteral("Obsidian"), {},
                           {{AppMatchKind::Name, QStringLiteral("obsidian")},
                            {AppMatchKind::WindowTitle, QStringLiteral("obsidian")}},
                           std::nullopt, customProfile, std::nullopt}));

        QCOMPARE(settings.speech.services.main(), QStringLiteral("local"));
        QCOMPARE(settings.speech.services.order.mid(1), QStringList{QStringLiteral("codex")});
        QCOMPARE(settings.speech.language, QStringLiteral("de"));
        QCOMPARE(localModelId(settings.speech), QStringLiteral("moonshine-small"));
        QVERIFY(settings.speech.local.modelChosen);
        // Read as native read it: the server, without the /v1 its path adds.
        QCOMPARE(settings.speech.server.baseUrl, QStringLiteral("http://127.0.0.1:9"));
        QCOMPARE(settings.speech.services.choice(QStringLiteral("endpoint")).model, QStringLiteral("whisper-fixture"));
        QCOMPARE(store.secrets()->secret(SecretStore::Secret::SpeechEndpointKey), QStringLiteral("fixture-speech-key"));

        QCOMPARE(test::spellingEntries(settings.vocabulary).size(), 262);
        QCOMPARE(settings.vocabulary[0].context, QStringLiteral("The app this fixture tests."));
        QCOMPARE(settings.vocabulary[0].profiles, QStringList{customProfile});
        QVERIFY(!settings.vocabulary[1].keyTerm);

        // Insights were on, and the upgrade keeps their promise: stats only.
        QCOMPARE(settings.history.keeps, HistoryKeep::Stats);
        // A Direct insertion default turns Press off, and restoring the
        // clipboard turns Keep off.
        QCOMPARE(settings.output.steps, (DeliverySteps{.insert = true, .press = false, .keep = false}));
        // 45 days, which the interval now caps at 30.
        QCOMPARE(store.dictationSnapshot().updates.checkIntervalMinutes, 30 * 24 * 60);
    }
};

int runMigrationTests(int argc, char **argv)
{
    MigrationTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_migration.moc"
