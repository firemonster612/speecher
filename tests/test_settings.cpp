#include "common/test_prelude.h"
#include "common/apps_fixtures.h"
#include "core/EndpointSettings.h"
#include "core/ProviderChain.h"
#include "core/ProviderReadiness.h"
#include "core/WritingProfileEdits.h"
#include "core/WritingProfileProviders.h"
#include "core/settings/SpokenLanguages.h"
#include "core/settings/SettingsKeys.h"
#include <QProcess>
#ifdef SPEECHER_WITH_QKEYCHAIN
#include "core/ShortcutBinding.h"

#include <QSet>
#endif

using namespace speecher;


class SettingsTests : public QObject {
    Q_OBJECT

private slots:
    void legacySpeechModelIsAnExplicitChoice()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue("local/model", "parakeet");
        QVERIFY(settings.dictationSnapshot().speech.local.modelChosen);
        auto saved = settings.dictationSnapshot().speech.local;
        saved.idleUnloadMinutes = 60;
        settings.edit([&](AppSettings &draft) { draft.speech.local = saved; return true; });
        QVERIFY(SettingsStore().dictationSnapshot().speech.local.modelChosen);
    }

    void localRunsOnDefaultsToAutomaticAndKeepsAnOldCard()
    {
        SettingsStore settings;
        settings.raw().clear();
        QCOMPARE(settings.dictationSnapshot().speech.local.runsOn, LocalRunsOn{});
        QCOMPARE(settings.dictationSnapshot().speech.local.runsOn.backend, QStringLiteral("auto"));

        // A graphics card saved before backends could be chosen is kept, on
        // whichever backend reaches it, and survives the next write.
        settings.raw().setValue(SettingsKeys::LocalDevice, QStringLiteral("0000:c1:00.0"));
        const LocalRunsOn legacy{QStringLiteral("auto"), QStringLiteral("0000:c1:00.0")};
        QCOMPARE(settings.dictationSnapshot().speech.local.runsOn, legacy);
        settings.edit([&](AppSettings &draft) {
            draft.speech.local = settings.dictationSnapshot().speech.local;
            return true;
        });
        QCOMPARE(SettingsStore().dictationSnapshot().speech.local.runsOn, legacy);
    }

    void settingsRespectConfiguredStorageFormat()
    {
        const auto previous = QSettings::defaultFormat();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        SettingsStore settings;
        const auto actual = settings.raw().format();
        QSettings::setDefaultFormat(previous);
        QCOMPARE(actual, QSettings::IniFormat);
    }

    void serviceOrdersAreStoredInOrderWithinTheCap()
    {
        const QString claude = QStringLiteral("claude");
        const QString codex = QStringLiteral("codex");
        const QString local = QStringLiteral("local");
        const QString endpoint = QStringLiteral("endpoint");
        QCOMPARE(normalizedServiceOrder(ProviderRole::Speech,
                                        {{endpoint, QString(), QStringLiteral("whisper"), claude, endpoint, local, codex}})
                     .order,
                 (QStringList{endpoint, claude, local}));
        // A profile's service may lead the whole order, so a chain has no cap.
        QCOMPARE(serviceChain(ProviderRole::Speech, {codex, local, claude, local, endpoint}),
                 (QStringList{codex, local, claude, endpoint}));
        QCOMPARE(normalizedServiceOrder(ProviderRole::Refinement, {{QStringLiteral("none"), claude, QStringLiteral("anthropic")}})
                     .order,
                 QStringList{QStringLiteral("anthropic")});

        SettingsStore settings;
        settings.raw().clear();
        // Nothing written reads as the default, with nothing behind it.
        QCOMPARE(settings.dictationSnapshot().speech.services.order, QStringList{claude});

        settings.edit([&](AppSettings &draft) {
            draft.speech.services.order = {codex, local, codex, endpoint, claude};
            return true;
        });
        QCOMPARE(SettingsStore().dictationSnapshot().speech.services.order, (QStringList{codex, local, endpoint}));
        // A service moved first leaves its old place.
        settings.edit([&](AppSettings &draft) { draft.speech.services = draft.speech.services.withMain(local); return true; });
        QCOMPARE(SettingsStore().dictationSnapshot().speech.services.order, (QStringList{local, codex, endpoint}));
    }

    // Each entry keeps its own choice, and keeps it while it is out of the
    // order, for when it comes back.
    void serviceChoicesStayWithTheirService()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.speech.services.order = {QStringLiteral("claude"), QStringLiteral("codex")};
            draft.speech.services.choice(QStringLiteral("codex")).language = QStringLiteral("ja");
            return true;
        });
        settings.edit([](AppSettings &draft) {
            draft.speech.services.order = {QStringLiteral("claude")};
            return true;
        });
        const ServiceOrder saved = SettingsStore().dictationSnapshot().speech.services;
        QCOMPARE(saved.order, QStringList{QStringLiteral("claude")});
        QCOMPARE(saved.choice(QStringLiteral("codex")).language, QStringLiteral("ja"));
        // An entry with nothing chosen names only its service.
        QCOMPARE(saved.choice(QStringLiteral("endpoint")), ServiceChoice{QStringLiteral("endpoint")});
    }

    // Clean up my words off runs no refinement service and keeps the order
    // for when it comes back on.
    void cleanUpMyWordsOffKeepsTheOrder()
    {
        SettingsStore settings;
        settings.raw().clear();
        const QStringList order{QStringLiteral("openai"), QStringLiteral("local")};
        settings.edit([&](AppSettings &draft) {
            draft.refinement.services.order = order;
            draft.refinement.enabled = false;
            return true;
        });
        AppSettings saved = SettingsStore().dictationSnapshot();
        QVERIFY(!saved.refinement.enabled);
        QCOMPARE(saved.refinement.services.order, order);
        QVERIFY(serviceChain(saved.refinement).isEmpty());

        settings.edit([](AppSettings &draft) { draft.refinement.enabled = true; return true; });
        QCOMPARE(serviceChain(SettingsStore().dictationSnapshot().refinement), order);
    }

    // Every shape an older release saved becomes one order per job, once,
    // and the older keys stay for a downgrade.
    void serviceOrdersMigrateFromEveryOldShape()
    {
        SettingsStore settings;
        settings.raw().clear();
        const auto set = [&settings](const QString &key, const QVariant &value) { settings.raw().setValue(key, value); };
        set(SettingsKeys::SpeechProvider, QStringLiteral("codex"));
        set(SettingsKeys::SpeechFallbackProviders, QStringList{QStringLiteral("local"), QStringLiteral("endpoint")});
        set(SettingsKeys::SpeechFallbackLanguages + QStringLiteral("/local"), QStringLiteral("de"));
        set(SettingsKeys::LocalModel, QStringLiteral("whisper-small"));
        set(SettingsKeys::SpeechEndpointModel, QStringLiteral("whisper-1"));
        set(SettingsKeys::RefinementProvider, QStringLiteral("none"));
        set(SettingsKeys::OpenAiModel, QStringLiteral("gpt-6-astra"));
        set(SettingsKeys::OpenAiFastMode, false);
        set(SettingsKeys::AnthropicModel, QStringLiteral("claude-sonnet-5-5"));
        set(SettingsKeys::AnthropicEffort, QStringLiteral("max"));
        set(SettingsKeys::AnthropicFastMode, false);
        set(SettingsKeys::LocalRunnerModel, QStringLiteral("gemma4:e4b"));
        set(SettingsKeys::CliproxyBaseUrl, QStringLiteral("http://proxy.example:8317"));
        set(SettingsKeys::RefinementEndpointPreset, QStringLiteral("cliproxy"));
        set(SettingsKeys::RefinementEndpointBaseUrl, QStringLiteral("http://custom.example/v1"));
        set(SettingsKeys::RefinementEndpointModel, QStringLiteral("qwen3.5:4b"));
        set(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("refinement-endpoint-key")});
        settings.raw().sync();

        SettingsCodecs().migrateServiceOrder();

        const AppSettings migrated = SettingsStore().snapshot();
        const ServiceOrder &speech = migrated.speech.services;
        QCOMPARE(speech.order, (QStringList{QStringLiteral("codex"), QStringLiteral("local"), QStringLiteral("endpoint")}));
        QCOMPARE(speech.choice(QStringLiteral("local")).language, QStringLiteral("de"));
        QCOMPARE(speech.choice(QStringLiteral("endpoint")).language, QString());
        QCOMPARE(localModelId(migrated.speech), QStringLiteral("whisper-small"));
        QCOMPARE(speech.choice(QStringLiteral("endpoint")).model, QStringLiteral("whisper-1"));
        // None is Clean up my words off, with an order waiting for it.
        const RefinementSettings &refinement = migrated.refinement;
        QVERIFY(!refinement.enabled);
        QVERIFY(!refinement.services.order.isEmpty());
        QCOMPARE(refinement.services.choice(QStringLiteral("openai")),
                 (ServiceChoice{QStringLiteral("openai"), QStringLiteral("gpt-6-astra"), {}, QStringLiteral("none"),
                                QStringLiteral("standard")}));
        QCOMPARE(refinement.services.choice(QStringLiteral("anthropic")),
                 (ServiceChoice{QStringLiteral("anthropic"), QStringLiteral("claude-sonnet-5-5"), {}, QStringLiteral("max"),
                                QStringLiteral("standard")}));
        QCOMPARE(refinement.services.choice(QStringLiteral("local")).model, QStringLiteral("gemma4:e4b"));
        QCOMPARE(refinement.services.choice(QStringLiteral("endpoint")).model, QStringLiteral("qwen3.5:4b"));
        // The CLI Proxy preset is the server at the proxy's address, which
        // sends the proxy's key: the key kept for the Custom server is let go.
        QCOMPARE(refinement.server.baseUrl, QStringLiteral("http://proxy.example:8317/v1"));
        QVERIFY(usesProxyKey(refinement, migrated.accounts));
        QVERIFY(!settings.raw().value(SettingsKeys::SecretsInKeyring).toStringList().contains(
            QStringLiteral("refinement-endpoint-key")));

        // An older release still finds what it saved.
        QCOMPARE(settings.raw().value(SettingsKeys::SpeechProvider).toString(), QStringLiteral("codex"));
        QCOMPARE(settings.raw().value(SettingsKeys::RefinementProvider).toString(), QStringLiteral("none"));
        QCOMPARE(settings.raw().value(SettingsKeys::RefinementEndpointPreset).toString(), QStringLiteral("cliproxy"));

        // Once: an order chosen since stays.
        settings.edit([](AppSettings &draft) {
            draft.speech.services.order = {QStringLiteral("claude")};
            return true;
        });
        SettingsCodecs().migrateServiceOrder();
        QCOMPARE(SettingsStore().snapshot().speech.services.order, QStringList{QStringLiteral("claude")});
        settings.raw().clear();
    }

    // Settings with nothing to migrate keep their defaults, Refinement's
    // still free to follow the coding apps installed.
    void aFreshInstallMigratesNothing()
    {
        SettingsStore settings;
        settings.raw().clear();
        SettingsCodecs().migrateServiceOrder();
        QVERIFY(!settings.raw().contains(SettingsKeys::SpeechServices));
        QVERIFY(!settings.raw().contains(SettingsKeys::RefinementServices));
        settings.raw().clear();
    }

    // Each entry is judged by its own choice: the Local Model it names, the
    // cleanup model it asks the Local Runner for.
    void aServiceIsJudgedByItsOwnEntry()
    {
        AppSettings settings;
        settings.speech.services.order = {QStringLiteral("claude"), QStringLiteral("local")};
        localModelId(settings.speech) = QStringLiteral("whisper-small");
        LiveFacts facts;
        facts.downloadedModels = {QStringLiteral("parakeet")};
        QCOMPARE(fallbackProblem(ProviderRole::Speech, QStringLiteral("local"), settings, facts), FallbackProblem::NoModel);
        localModelId(settings.speech) = QStringLiteral("parakeet");
        QCOMPARE(fallbackProblem(ProviderRole::Speech, QStringLiteral("local"), settings, facts), FallbackProblem::None);

        settings.refinement.localRunner.runner = QStringLiteral("ollama");
        facts.runners = {{QStringLiteral("ollama"), QStringLiteral("Ollama 0.12.3")}};
        facts.runnersChecked = true;
        QCOMPARE(fallbackProblem(ProviderRole::Refinement, QStringLiteral("local"), settings, facts),
                 FallbackProblem::NoModel);
        settings.refinement.services.choice(QStringLiteral("local")).model = QStringLiteral("gemma4:e4b");
        QCOMPARE(fallbackProblem(ProviderRole::Refinement, QStringLiteral("local"), settings, facts), FallbackProblem::None);

        // The server's model is its entry's, and its address its own.
        QCOMPARE(fallbackProblem(ProviderRole::Refinement, QStringLiteral("endpoint"), settings, facts),
                 FallbackProblem::NoServer);
        settings.refinement.server.baseUrl = QStringLiteral("http://localhost:11434/v1");
        QCOMPARE(fallbackProblem(ProviderRole::Refinement, QStringLiteral("endpoint"), settings, facts),
                 FallbackProblem::None);
    }

    // A profile's or a run's choice runs over the order's entry for its
    // service, field by field.
    void aChoiceRunsOverTheEntryForItsService()
    {
        const ServiceChoice entry{QStringLiteral("openai"), QStringLiteral("gpt-6-luna"), {}, QStringLiteral("none"),
                                  QStringLiteral("fast")};
        QCOMPARE(mergedServiceChoice(entry, {QStringLiteral("openai"), QStringLiteral("gpt-6-astra")}),
                 (ServiceChoice{QStringLiteral("openai"), QStringLiteral("gpt-6-astra"), {}, QStringLiteral("none"),
                                QStringLiteral("fast")}));
        QCOMPARE(mergedServiceChoice(entry, {}), entry);
    }

    void fallbackProvidersRoundTripThroughASnapshot()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.speech.services.order = {QStringLiteral("codex"), QStringLiteral("endpoint"), QStringLiteral("local")};
        draft.refinement.services.order = {QStringLiteral("anthropic"), QStringLiteral("local")};
        settings.applySnapshot(draft);

        const AppSettings saved = SettingsStore().snapshot();
        QCOMPARE(saved.speech.services.order.mid(1), draft.speech.services.order.mid(1));
        QCOMPARE(saved.refinement.services.order.mid(1), draft.refinement.services.order.mid(1));
    }

    // A draft taken before another window swapped the primary and a fallback
    // still saves the chain it shows.
    void aDraftThatSwapsThePrimaryAndAFallbackSavesBoth()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.speech.services.order = {QStringLiteral("claude"), QStringLiteral("codex")};
            return true;
        });
        AppSettings draft = settings.snapshot();
        draft.speech.services.order = {QStringLiteral("codex"), QStringLiteral("claude")};
        settings.applySnapshot(draft);

        const AppSettings saved = SettingsStore().snapshot();
        QCOMPARE(saved.speech.services.main(), QStringLiteral("codex"));
        QCOMPARE(saved.speech.services.order.mid(1), QStringList{QStringLiteral("claude")});
    }

    void otherTestProcessesCannotClearSettingsFallbacks()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(SettingsKeys::SpeechEndpointApiKey, QStringLiteral("speech-secret"));
        settings.raw().sync();

        // This peer clears its settings, as many suites do at the start of a test.
        QProcess peer;
        peer.setProcessChannelMode(QProcess::MergedChannels);
        peer.start(QCoreApplication::applicationFilePath(),
                   {QStringLiteral("--suite"), QStringLiteral("settings"),
                    QStringLiteral("legacySpeechModelIsAnExplicitChoice")});
        QVERIFY(peer.waitForFinished());
        QVERIFY2(peer.exitStatus() == QProcess::NormalExit && peer.exitCode() == 0,
                 peer.readAll().constData());
        QCOMPARE(SettingsStore().snapshot().speech.server.apiKey, QStringLiteral("speech-secret"));
    }

    void identityMigrationMergesOnlyMissingSettingsOnce()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings oldSettings(dir.filePath(QStringLiteral("old.ini")), QSettings::IniFormat);
        QSettings newSettings(dir.filePath(QStringLiteral("new.ini")), QSettings::IniFormat);
        oldSettings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("old-theme"));
        oldSettings.setValue(QStringLiteral("output/method"), QStringLiteral("ydotool"));
        newSettings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("new-theme"));

        QString error;
        QVERIFY2(migrateSettingsIdentity(newSettings, oldSettings, &error), qPrintable(error));
        QCOMPARE(newSettings.value(QStringLiteral("appearance/theme")).toString(),
                 QStringLiteral("new-theme"));
        QCOMPARE(newSettings.value(QStringLiteral("output/method")).toString(),
                 QStringLiteral("ydotool"));

        newSettings.remove(QStringLiteral("output/method"));
        QVERIFY2(migrateSettingsIdentity(newSettings, oldSettings, &error), qPrintable(error));
        QVERIFY(!newSettings.contains(QStringLiteral("output/method")));
    }

    void refinementModelMigrationMovesReplacedDefaultsOnce()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("openai/model"), QStringLiteral("gpt-5.6-luna"));
        settings.setValue(QStringLiteral("anthropic/model"), QStringLiteral("claude-opus-5"));

        migrateRefinementModels(settings);
        QCOMPARE(settings.value(QStringLiteral("openai/model")).toString(),
                 QStringLiteral("gpt-6-luna"));
        QCOMPARE(settings.value(QStringLiteral("anthropic/model")).toString(),
                 QStringLiteral("claude-opus-5"));

        // Sonnet 5 chosen after the first migration moves to Sonnet 5.5, once.
        settings.setValue(QStringLiteral("migration/refinementModelVersion"), 1);
        settings.setValue(QStringLiteral("anthropic/model"), QStringLiteral("claude-sonnet-5"));
        migrateRefinementModels(settings);
        QCOMPARE(settings.value(QStringLiteral("anthropic/model")).toString(),
                 QStringLiteral("claude-sonnet-5-5"));

        settings.setValue(QStringLiteral("anthropic/model"), QStringLiteral("claude-sonnet-5"));
        migrateRefinementModels(settings);
        QCOMPARE(settings.value(QStringLiteral("anthropic/model")).toString(),
                 QStringLiteral("claude-sonnet-5"));
    }

    // Old installs hold QKeySequence text under shortcuts/toggleDictation, so
    // that form must keep reading as a combination while a single key gets its
    // own prefix.
    void shortcutBindingRoundTripsBothFormsAndTheLegacyValue()
    {
        const ShortcutBinding combo(QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
        QCOMPARE(combo.toString(), QStringLiteral("Meta+Alt+D"));
        QCOMPARE(ShortcutBinding::fromString(QStringLiteral("Meta+Alt+D")), combo);
        QVERIFY(!combo.isSingleKey());

        const ShortcutBinding rightAlt = ShortcutBinding::singleKey(QStringLiteral("AltRight"));
        QVERIFY(rightAlt.isSingleKey());
        QCOMPARE(rightAlt.toString(), QStringLiteral("key:AltRight"));
        QCOMPARE(ShortcutBinding::fromString(QStringLiteral("key:AltRight")), rightAlt);
        QCOMPARE(rightAlt.keyCode(), QStringLiteral("AltRight"));
#ifdef Q_OS_MACOS
        QCOMPARE(rightAlt.displayText(), QStringLiteral("Right Option"));
#else
        QCOMPARE(rightAlt.displayText(), QStringLiteral("Right Alt"));
#endif
        QVERIFY(rightAlt.combination().isEmpty());

        QVERIFY(ShortcutBinding().isEmpty());
        QVERIFY(ShortcutBinding::singleKey(QStringLiteral("NotAKey")).isEmpty());
        QVERIFY(ShortcutBinding::fromString(QString()).isEmpty());
    }

    // Each platform column maps a code to that platform's keycode. Expected
    // values are from the platforms' own tables (input-event-codes.h,
    // HIToolbox/Events.h kVK_*, Chromium's dom_code_data.inc win column),
    // never recomputed the way the table is; -1 marks keys a platform does
    // not have, which the reverse lookups must never match.
    void vocabularyCarriesPlatformKeycodes()
    {
        // evdev (X11 = evdev + 8).
        QCOMPARE(physicalKey(QStringLiteral("AltRight"))->evdev, 100);
        QCOMPARE(physicalKey(QStringLiteral("KeyE"))->evdev, 18);
        QCOMPARE(physicalKeyForEvdev(100)->code, "AltRight");
        QCOMPARE(physicalKeyForEvdev(58)->code, "CapsLock");
        QVERIFY(physicalKeyForEvdev(9999) == nullptr);

        // mac. Note evdev 58 is Caps Lock while mac 58 is Left Option — the
        // columns are independent.
        QCOMPARE(physicalKey(QStringLiteral("AltRight"))->mac, 61);
        QCOMPARE(physicalKey(QStringLiteral("AltLeft"))->mac, 58);
        QCOMPARE(physicalKey(QStringLiteral("Fn"))->mac, 63);
        QCOMPARE(physicalKey(QStringLiteral("PrintScreen"))->mac, -1);
        QCOMPARE(physicalKeyForMac(0)->code, "KeyA");
        QVERIFY(physicalKeyForMac(-1) == nullptr);

        // win: the set-1 make code with 0xE0 in the high byte for extended
        // keys, as WM_KEYDOWN's lParam spells it. Pause is 0x45 while NumLock
        // is 0xE045 (the spelling the raw-input backend normalizes its E1/E0
        // quirks to); Fn never reaches Windows.
        QCOMPARE(physicalKey(QStringLiteral("AltRight"))->win, 0xE038);
        QCOMPARE(physicalKey(QStringLiteral("Pause"))->win, 0x45);
        QCOMPARE(physicalKey(QStringLiteral("NumLock"))->win, 0xE045);
        QCOMPARE(physicalKey(QStringLiteral("Fn"))->win, -1);
        QCOMPARE(physicalKeyForWin(0xE038)->code, "AltRight");
        QVERIFY(physicalKeyForWin(-1) == nullptr);

        // No watcher may read one event as two bindings: the assigned mac
        // modifier keycodes are unique, and the win keys sharing a make code
        // differ exactly in the E0 byte.
        QSet<int> seen;
        for (const char *code : {"ShiftLeft", "ShiftRight", "ControlLeft", "ControlRight",
                                 "AltLeft", "AltRight", "MetaLeft", "MetaRight", "CapsLock", "Fn"}) {
            const int mac = physicalKey(QLatin1String(code))->mac;
            QVERIFY(!seen.contains(mac));
            seen.insert(mac);
        }
        for (const auto &[plain, extended] :
             QList<QPair<QString, QString>>{{QStringLiteral("ControlLeft"), QStringLiteral("ControlRight")},
                                            {QStringLiteral("AltLeft"), QStringLiteral("AltRight")},
                                            {QStringLiteral("Slash"), QStringLiteral("NumpadDivide")},
                                            {QStringLiteral("Enter"), QStringLiteral("NumpadEnter")},
                                            {QStringLiteral("NumpadMultiply"), QStringLiteral("PrintScreen")},
                                            {QStringLiteral("Pause"), QStringLiteral("NumLock")}}) {
            QCOMPARE(physicalKey(extended)->win, physicalKey(plain)->win | 0xE000);
        }
    }

    // The warning is the same sentence on every platform; the keys that carry
    // no text stay silent.
    void singleKeyWarningNamesTypingKeysOnly()
    {
        QVERIFY(singleKeyTypingWarning(ShortcutBinding::singleKey(QStringLiteral("AltRight")))
                    .isEmpty());
        QVERIFY(singleKeyTypingWarning(ShortcutBinding::singleKey(QStringLiteral("F13")))
                    .isEmpty());
        QVERIFY(singleKeyTypingWarning(ShortcutBinding::singleKey(QStringLiteral("KeyE")))
                    .contains(QStringLiteral("E")));
    }

    // A watched single key fires whatever modifiers are held, so it overlaps
    // every combination on the same key; two shortcuts must not share one.
    void aWatchedKeyOverlapsEveryCombinationOnIt()
    {
        const ShortcutBinding f13 = ShortcutBinding::singleKey(QStringLiteral("F13"));
        QVERIFY(f13.overlaps(ShortcutBinding(QKeySequence(Qt::Key_F13))));
        QVERIFY(f13.overlaps(ShortcutBinding(QKeySequence(Qt::CTRL | Qt::Key_F13))));
        QVERIFY(ShortcutBinding(QKeySequence(Qt::Key_C))
                    .overlaps(ShortcutBinding::singleKey(QStringLiteral("KeyC"))));
        QVERIFY(ShortcutBinding::singleKey(QStringLiteral("Escape"))
                    .overlaps(ShortcutBinding(QKeySequence(Qt::Key_Escape))));
        QVERIFY(ShortcutBinding::singleKey(QStringLiteral("Enter"))
                    .overlaps(ShortcutBinding(QKeySequence(Qt::CTRL | Qt::Key_Return))));
        QVERIFY(ShortcutBinding::singleKey(QStringLiteral("PrintScreen"))
                    .overlaps(ShortcutBinding(QKeySequence(Qt::Key_Print))));
        QVERIFY(!f13.overlaps(ShortcutBinding(QKeySequence(Qt::Key_F14))));
        QVERIFY(!f13.overlaps(ShortcutBinding()));
        // Desktop shortcut services match combinations exactly.
        QVERIFY(!ShortcutBinding(QKeySequence(Qt::CTRL | Qt::Key_C))
                     .overlaps(ShortcutBinding(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C))));
    }

    void settingsDefaults()
    {
        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "1");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "1");
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CODEX_INSTALLED");
            qunsetenv("SPEECHER_TEST_CLAUDE_INSTALLED");
        });

        SettingsStore settings;
        settings.raw().clear();
        QCOMPARE(settings.dictationSnapshot().setupCompleted, false);
        QCOMPARE(settings.dictationSnapshot().ui.previewWords, 7);
        QCOMPARE(settings.dictationSnapshot().ui.theme, QStringLiteral("system"));
        QCOMPARE(settings.dictationSnapshot().ui.pauseMediaDuringTranscription, true);
        QCOMPARE(settings.dictationSnapshot().ui.soundsEnabled, false);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        QCOMPARE(settings.dictationSnapshot().launchAtLogin, true);
#else
        QCOMPARE(settings.dictationSnapshot().launchAtLogin, false);
#endif
        QVERIFY(settings.dictationSnapshot().vocabulary.isEmpty());
        QCOMPARE(settings.dictationSnapshot().bindings.size(), 0);
        QCOMPARE(settings.dictationSnapshot().refinement.services.main(), QStringLiteral("openai"));
        QCOMPARE(settings.dictationSnapshot().refinement.style, QStringLiteral("balanced"));
        QCOMPARE(fallbackWritingProfile(settings.dictationSnapshot().apps), QStringLiteral("other"));
        QCOMPARE(settings.dictationSnapshot().refinement.writingProfiles, defaultWritingProfileSettings());
        QCOMPARE(settings.dictationSnapshot().refinement.writingProfiles.size(), 5);
        QCOMPARE(settings.dictationSnapshot().apps, AppsSettings{});
        QCOMPARE(settings.dictationSnapshot().refinement.useTargetContext, true);
        QCOMPARE(settings.dictationSnapshot().refinement.includeScreenshotContext, false);
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).model, QStringLiteral("gpt-6-luna"));
        QCOMPARE(settings.dictationSnapshot().accounts.openAiAuthMode, OpenAiAuthMode::Auto);
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).effort, QStringLiteral("none"));
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).speed, QStringLiteral("fast"));
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).model, QStringLiteral("claude-opus-5-5"));
        QCOMPARE(settings.dictationSnapshot().accounts.anthropicAuthMode, AnthropicAuthMode::Oauth);
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).effort, QStringLiteral("low"));
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).speed,
                 QStringLiteral("fast"));
        // The Codex accuracy pass is on until someone turns it off.
        QCOMPARE(settings.dictationSnapshot().speech.codexFinalRetranscribe, true);
        settings.edit([](AppSettings &draft) { draft.speech.codexFinalRetranscribe = false; return true; });
        QCOMPARE(settings.dictationSnapshot().speech.codexFinalRetranscribe, false);
        QCOMPARE(settings.snapshot().speech.language, QStringLiteral("en"));
        QCOMPARE(settings.dictationSnapshot().output.steps, (DeliverySteps{.insert = true, .press = true, .keep = true}));
        QCOMPARE(settings.dictationSnapshot().output.format, OutputFormat::PlainText);
        QCOMPARE(settings.dictationSnapshot().output.ydotoolEnabled, false);
        QCOMPARE(settings.snapshot().output.completionStatusDurationMs, 500);
        const AudioCaptureSettings audio = settings.dictationSnapshot().audio;
        QCOMPARE(audio.deviceId, QString());
        QCOMPARE(audio.mode, QStringLiteral("on_demand"));
        QCOMPARE(audio.vadEnabled, false);
        QCOMPARE(audio.preRollMs, 250);
        QCOMPARE(audio.postRollMs, 200);
        QCOMPARE(audio.readinessTimeoutMs, 900);
        QCOMPARE(audio.vadThresholdPercent, 2);
        QCOMPARE(settings.dictationSnapshot().shortcutActivationMode, ShortcutActivationMode::Hybrid);

        settings.edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
        QCOMPARE(settings.dictationSnapshot().setupCompleted, true);
        QCOMPARE(settings.snapshot().setupCompleted, true);

        settings.raw().setValue(SettingsKeys::RefinementStyle, QStringLiteral("strong_polish"));
        QCOMPARE(settings.dictationSnapshot().refinement.style, QStringLiteral("strong_polish"));
        settings.raw().setValue(SettingsKeys::RefinementStyle, QStringLiteral("balanced"));
        QCOMPARE(settings.dictationSnapshot().refinement.style, QStringLiteral("balanced"));
        settings.raw().setValue(SettingsKeys::RefinementStyle, QStringLiteral("light_cleanup"));
        QCOMPARE(settings.dictationSnapshot().refinement.style, QStringLiteral("light_cleanup"));
        settings.raw().setValue(SettingsKeys::RefinementStyle, QStringLiteral("unknown"));
        QCOMPARE(settings.dictationSnapshot().refinement.style, QStringLiteral("balanced"));
        settings.raw().setValue(QStringLiteral("refinement/writingProfiles"),
                                QByteArray(R"([{"profile":"work","cleanupStrength":"strong_polish","tone":"formal"},)"
                                           R"({"profile":"email","cleanupStrength":"balanced","tone":"casual"},)"
                                           R"({"profile":"personal","cleanupStrength":"light_cleanup","tone":"very_casual"},)"
                                           R"({"profile":"other","cleanupStrength":"balanced","tone":"none"}])"));
        const WritingProfileSettings migratedAiCoding =
            writingProfileSettingsFor(settings.dictationSnapshot().refinement.writingProfiles, WritingProfile::AiCoding);
        QCOMPARE(migratedAiCoding.cleanupStrength, QStringLiteral("strong_polish"));
        QCOMPARE(migratedAiCoding.tone, QStringLiteral("formal"));
        settings.edit([](AppSettings &draft) {
            draft.refinement.writingProfiles = {
                {WritingProfile::Work, QStringLiteral("strong_polish"), QStringLiteral("formal")},
                {WritingProfile::Email, QStringLiteral("balanced"), QStringLiteral("casual")},
                {WritingProfile::Personal, QStringLiteral("light_cleanup"), QStringLiteral("very_casual")},
                {WritingProfile::AiCoding, QStringLiteral("none"), QStringLiteral("casual")},
                {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
            };
            return true;
        });
        QCOMPARE(writingProfileSettingsFor(settings.dictationSnapshot().refinement.writingProfiles, WritingProfile::Work).cleanupStrength,
                 QStringLiteral("strong_polish"));
        QCOMPARE(writingProfileSettingsFor(settings.dictationSnapshot().refinement.writingProfiles, WritingProfile::Personal).tone,
                 QStringLiteral("very_casual"));
        QCOMPARE(writingProfileSettingsFor(settings.dictationSnapshot().refinement.writingProfiles, WritingProfile::AiCoding).cleanupStrength,
                 QStringLiteral("none"));
        settings.edit([](AppSettings &draft) { draft.refinement.useTargetContext = false; return true; });
        QCOMPARE(settings.dictationSnapshot().refinement.useTargetContext, false);
        settings.edit([](AppSettings &draft) { draft.refinement.includeScreenshotContext = true; return true; });
        QCOMPARE(settings.dictationSnapshot().refinement.includeScreenshotContext, true);
        settings.edit([](AppSettings &draft) { draft.ui.soundsEnabled = true; return true; });
        QCOMPARE(settings.dictationSnapshot().ui.soundsEnabled, true);
        settings.edit([](AppSettings &draft) { draft.ui.soundsEnabled = false; return true; });
        QCOMPARE(settings.dictationSnapshot().ui.soundsEnabled, false);

        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("openai")).model = QStringLiteral(" gpt-5.4-nano ");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).model, QStringLiteral("gpt-5.4-nano"));
        settings.edit([](AppSettings &draft) { draft.refinement.services.choice(QStringLiteral("openai")).model = QString(); return true; });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).model, QStringLiteral("gpt-6-luna"));

        // Each auth mode is stored by the name earlier releases stored, and
        // the names they stored before these read as the mode they meant.
        const std::pair<const char *, OpenAiAuthMode> openAiModes[] = {
            {"auto", OpenAiAuthMode::Auto},
            {"codex_oauth", OpenAiAuthMode::CodexOauth},
            {"codex_api_key", OpenAiAuthMode::CodexApiKey},
            {"env", OpenAiAuthMode::Env},
            {"settings", OpenAiAuthMode::Settings},
            {"cliproxy", OpenAiAuthMode::Cliproxy},
        };
        for (const auto &[name, mode] : openAiModes) {
            settings.edit([&](AppSettings &draft) { draft.accounts.openAiAuthMode = mode; return true; });
            QCOMPARE(settings.raw().value(QStringLiteral("openai/auth/mode")).toString(), QString::fromLatin1(name));
            QCOMPARE(settings.dictationSnapshot().accounts.openAiAuthMode, mode);
        }
        const std::pair<const char *, OpenAiAuthMode> legacyOpenAiModes[] = {
            {"api_key_env", OpenAiAuthMode::Env},
            {"api_key_settings", OpenAiAuthMode::Settings},
            {"codex_then_api_key", OpenAiAuthMode::Auto},
            {"unknown", OpenAiAuthMode::Auto},
        };
        for (const auto &[name, mode] : legacyOpenAiModes) {
            settings.raw().setValue(QStringLiteral("openai/auth/mode"), QString::fromLatin1(name));
            QCOMPARE(settings.dictationSnapshot().accounts.openAiAuthMode, mode);
        }
        settings.edit([](AppSettings &draft) { draft.refinement.services.choice(QStringLiteral("openai")).effort = QStringLiteral("xhigh"); return true; });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).effort, QStringLiteral("xhigh"));
        settings.edit([](AppSettings &draft) { draft.refinement.services.choice(QStringLiteral("openai")).effort = QStringLiteral("none"); return true; });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).effort, QStringLiteral("none"));
        settings.raw().setValue(QStringLiteral("openai/effort"), QStringLiteral("minimal"));
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).effort, QStringLiteral("none"));
        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("openai")).effort = QStringLiteral("unsupported");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).effort, QStringLiteral("none"));
        // Fast mode turned off before Speed existed migrates as Standard.
        settings.raw().clear();
        settings.raw().setValue(QStringLiteral("openai/fastMode"), false);
        SettingsCodecs().migrateServiceOrder();
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).speed, QStringLiteral("standard"));
        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("openai")).speed = QStringLiteral("ultrafast");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).speed, QStringLiteral("ultrafast"));
        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("openai")).speed = QStringLiteral("unsupported");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).speed, QStringLiteral("fast"));

        settings.edit([](AppSettings &draft) {
            draft.refinement.services.order = {QStringLiteral("anthropic")};
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.main(), QStringLiteral("anthropic"));
        settings.edit([](AppSettings &draft) { draft.refinement.services.order = {QStringLiteral("unknown")}; return true; });
        QCOMPARE(settings.dictationSnapshot().refinement.services.main(), QStringLiteral("openai"));

        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("anthropic")).model = QStringLiteral(" claude-opus-5 ");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).model, QStringLiteral("claude-opus-5"));
        settings.edit([](AppSettings &draft) { draft.refinement.services.choice(QStringLiteral("anthropic")).model = QString(); return true; });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).model, QStringLiteral("claude-opus-5-5"));
        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("anthropic")).model = QStringLiteral("claude-haiku-4-5-20251001");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).model, QStringLiteral("claude-haiku-5-5"));
        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("anthropic")).model = QStringLiteral("claude-haiku-4-5");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).model, QStringLiteral("claude-haiku-5-5"));
        settings.edit([](AppSettings &draft) {
            draft.accounts.anthropicAuthMode = AnthropicAuthMode::Cliproxy;
            return true;
        });
        QCOMPARE(settings.raw().value(QStringLiteral("anthropic/auth/mode")).toString(), QStringLiteral("cliproxy"));
        QCOMPARE(settings.dictationSnapshot().accounts.anthropicAuthMode, AnthropicAuthMode::Cliproxy);
        settings.edit([](AppSettings &draft) {
            draft.accounts.anthropicAuthMode = AnthropicAuthMode::Oauth;
            return true;
        });
        QCOMPARE(settings.raw().value(QStringLiteral("anthropic/auth/mode")).toString(), QStringLiteral("oauth"));
        QCOMPARE(settings.dictationSnapshot().accounts.anthropicAuthMode, AnthropicAuthMode::Oauth);
        settings.raw().setValue(QStringLiteral("anthropic/auth/mode"), QStringLiteral("unknown"));
        QCOMPARE(settings.dictationSnapshot().accounts.anthropicAuthMode, AnthropicAuthMode::Oauth);
        settings.edit([](AppSettings &draft) {
            draft.accounts.openAiCliproxyAccount = QStringLiteral(" codex-user@example.com.json ");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().accounts.openAiCliproxyAccount, QStringLiteral("codex-user@example.com.json"));
        settings.edit([](AppSettings &draft) {
            draft.accounts.anthropicCliproxyAccount = QStringLiteral("claude-user@example.com.json");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().accounts.anthropicCliproxyAccount, QStringLiteral("claude-user@example.com.json"));
        QCOMPARE(settings.snapshot().accounts.openAiCliproxyAccount, QStringLiteral("codex-user@example.com.json"));
        QCOMPARE(settings.snapshot().accounts.anthropicCliproxyAccount, QStringLiteral("claude-user@example.com.json"));
        // The default is autodetected from the machine: whichever CLI Proxy API
        // auth dir holds accounts, or the stock ~/.cli-proxy-api on a machine
        // with neither.
        const QString oauthDir = settings.snapshot().accounts.cliproxyOauthDir;
        QVERIFY(oauthDir.endsWith(QStringLiteral("cliproxy-api/oauth"))
                || oauthDir.endsWith(QStringLiteral(".cli-proxy-api")));
        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("anthropic")).effort = QStringLiteral("high");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).effort, QStringLiteral("high"));
        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("anthropic")).effort = QStringLiteral("max");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).effort, QStringLiteral("max"));
        settings.edit([](AppSettings &draft) {
            draft.refinement.services.choice(QStringLiteral("anthropic")).effort = QStringLiteral("none");
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).effort, QStringLiteral("low"));
        settings.edit([](AppSettings &draft) { draft.refinement.services.choice(QStringLiteral("anthropic")).speed = QStringLiteral("standard"); return true; });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).speed,
                 QStringLiteral("standard"));
        settings.edit([](AppSettings &draft) { draft.refinement.services.choice(QStringLiteral("anthropic")).speed = QStringLiteral("fast"); return true; });
        QCOMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("anthropic")).speed,
                 QStringLiteral("fast"));

        settings.edit([](AppSettings &draft) { draft.ui.pauseMediaDuringTranscription = true; return true; });
        QCOMPARE(settings.dictationSnapshot().ui.pauseMediaDuringTranscription, true);
        settings.edit([](AppSettings &draft) { draft.ui.pauseMediaDuringTranscription = false; return true; });
        QCOMPARE(settings.dictationSnapshot().ui.pauseMediaDuringTranscription, false);

        settings.edit([](AppSettings &draft) {
            draft.output.steps = {.insert = false, .press = true, .keep = false};
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().output.steps, (DeliverySteps{.insert = false, .press = true, .keep = false}));
        // A file that turns every step off still keeps the words.
        settings.raw().setValue(QStringLiteral("output/pressPasteKeys"), false);
        QCOMPARE(settings.dictationSnapshot().output.steps, (DeliverySteps{.insert = false, .press = false, .keep = true}));
        settings.setYdotoolEnabled(true);
        QCOMPARE(settings.dictationSnapshot().output.ydotoolEnabled, true);
        settings.setYdotoolEnabled(false);
        settings.edit([](AppSettings &draft) { draft.output.format = OutputFormat::Html; return true; });
        QCOMPARE(settings.dictationSnapshot().output.format, OutputFormat::Html);
        settings.raw().setValue(QStringLiteral("output/format"), QStringLiteral("unsupported"));
        QCOMPARE(settings.dictationSnapshot().output.format, OutputFormat::PlainText);
        settings.edit([](AppSettings &draft) {
            draft.audio = {
                QStringLiteral(" mic-id "),
                QStringLiteral("always_open"),
                true,
                5000,
                -20,
                50,
                99,
            };
            return true;
        });
        const AudioCaptureSettings clamped = settings.dictationSnapshot().audio;
        QCOMPARE(clamped.deviceId, QStringLiteral("mic-id"));
        QCOMPARE(clamped.mode, QStringLiteral("warm"));
        QCOMPARE(clamped.vadEnabled, true);
        QCOMPARE(clamped.preRollMs, 1500);
        QCOMPARE(clamped.postRollMs, 0);
        QCOMPARE(clamped.readinessTimeoutMs, 500);
        QCOMPARE(clamped.vadThresholdPercent, 20);

        settings.raw().setValue(SettingsKeys::ShortcutActivationMode, QStringLiteral("hold"));
        QCOMPARE(settings.dictationSnapshot().shortcutActivationMode, ShortcutActivationMode::Hybrid);
        settings.edit([](AppSettings &draft) {
            draft.shortcutActivationMode = ShortcutActivationMode::PushToTalk;
            return true;
        });
        QCOMPARE(settings.dictationSnapshot().shortcutActivationMode, ShortcutActivationMode::PushToTalk);
        QCOMPARE(settings.snapshot().shortcutActivationMode, ShortcutActivationMode::PushToTalk);
    }

    void spokenLanguageRoundTrips()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.speech.language = QStringLiteral("de");
        settings.applySnapshot(draft);
        QCOMPARE(SettingsStore().snapshot().speech.language, QStringLiteral("de"));
        settings.raw().clear();
    }

    // An entry's own Spoken Language round-trips; one with none behind
    // Automatic, English-only, then listens for English.
    void entrySpokenLanguagesRoundTrip()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.speech.services.order = {QStringLiteral("codex"), QStringLiteral("local")};
            draft.speech.language = QStringLiteral("auto");
            return true;
        });
        AppSettings draft = SettingsStore().snapshot();
        QVERIFY(draft.speech.services.choice(QStringLiteral("local")).language.isEmpty());
        QCOMPARE(chainSpeechSettings(draft.speech, QStringLiteral("local")).language, QStringLiteral("en"));

        draft.speech.services.choice(QStringLiteral("local")).language = QStringLiteral("de");
        settings.applySnapshot(draft);
        const AppSettings saved = SettingsStore().snapshot();
        QCOMPARE(saved.speech.services.choice(QStringLiteral("local")).language, QStringLiteral("de"));
        QCOMPARE(saved.speech.language, QStringLiteral("auto"));
        settings.raw().clear();
    }

    void aFallbackListensForItsOwnSpokenLanguage_data()
    {
        QTest::addColumn<QString>("spoken");
        QTest::addColumn<QString>("fallback");
        QTest::addColumn<QString>("chosen");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("listens");
        QTest::newRow("English-only, behind Automatic") << QStringLiteral("auto") << QStringLiteral("local") << QString()
                                                        << QStringLiteral("en") << true;
        QTest::newRow("takes the Spoken Language") << QStringLiteral("de") << QStringLiteral("claude") << QString()
                                                   << QStringLiteral("de") << true;
        QTest::newRow("lacks the Spoken Language") << QStringLiteral("af") << QStringLiteral("claude") << QString()
                                                   << QStringLiteral("af") << false;
        QTest::newRow("chosen") << QStringLiteral("auto") << QStringLiteral("claude") << QStringLiteral("fr")
                                << QStringLiteral("fr") << true;
        QTest::newRow("chosen, can't listen") << QStringLiteral("auto") << QStringLiteral("local")
                                              << QStringLiteral("fr") << QStringLiteral("fr") << false;
    }

    // A fallback listens for the language chosen for it; with none, for the
    // Spoken Language where it can, otherwise English behind Automatic. The
    // primary keeps the Spoken Language.
    void aFallbackListensForItsOwnSpokenLanguage()
    {
        QFETCH(QString, spoken);
        QFETCH(QString, fallback);
        QFETCH(QString, chosen);
        QFETCH(QString, expected);
        QFETCH(bool, listens);
        SpeechSettings speech;
        speech.services.order = {QStringLiteral("codex"), fallback};
        speech.language = spoken;
        localModelId(speech) = QStringLiteral("parakeet");
        speech.services.choice(fallback).language = chosen;
        // The first entry's own language waits for it to move down.
        speech.services.choice(QStringLiteral("codex")).language = QStringLiteral("ja");

        const SpeechSettings resolved = chainSpeechSettings(speech, fallback);
        QCOMPARE(resolved.services.main(), fallback);
        QCOMPARE(resolved.language, expected);
        QCOMPARE(listensForSpokenLanguage(resolved), listens);
        QCOMPARE(chainSpeechSettings(speech, QStringLiteral("codex")).language, spoken);
    }

    void localSpeechSettingsRoundTrip()
    {
        SettingsStore settings;
        settings.raw().clear();
        QCOMPARE(settings.snapshot().speech.local, LocalSpeechSettings{});
        QCOMPARE(localModelId(settings.snapshot().speech), QStringLiteral("parakeet"));
        QCOMPARE(settings.snapshot().speech.local.idleUnloadMinutes, 10);

        AppSettings draft = settings.snapshot();
        localModelId(draft.speech) = QStringLiteral("cohere");
        draft.speech.local.modelChosen = true;
        draft.speech.local.runsOn = {QStringLiteral("cuda"), QStringLiteral("0000:c1:00.0")};
        draft.speech.local.idleUnloadMinutes = 0;
        const QString parakeetOnCuda = localSpeedTestKey(QStringLiteral("parakeet"), draft.speech.local.runsOn);
        const QString cohereOnCpu = localSpeedTestKey(QStringLiteral("cohere"), {QStringLiteral("cpu"), {}});
        draft.speech.local.speedTestSeconds = {{parakeetOnCuda, 0.42}, {cohereOnCpu, 1.7}};
        settings.applySnapshot(draft);
        QCOMPARE(SettingsStore().snapshot().speech.local, draft.speech.local);

        draft.speech.local.speedTestSeconds.remove(cohereOnCpu);
        settings.applySnapshot(draft);
        QCOMPARE(SettingsStore().snapshot().speech.local.speedTestSeconds.keys(), QStringList{parakeetOnCuda});

        // A result saved before results were kept per Runs on choice ran on
        // Automatic.
        settings.raw().setValue(SettingsKeys::LocalSpeedTest + QStringLiteral("/moonshine-small"), 0.3);
        QCOMPARE(settings.dictationSnapshot().speech.local.speedTestSeconds.value(
                     localSpeedTestKey(QStringLiteral("moonshine-small"), {})),
                 0.3);
        settings.raw().clear();
    }

    void settingsDefaultRefinementProviderUsesInstalledCli()
    {
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CODEX_INSTALLED");
            qunsetenv("SPEECHER_TEST_CLAUDE_INSTALLED");
        });

        SettingsStore settings;
        settings.raw().clear();

        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "0");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "1");
        QCOMPARE(settings.dictationSnapshot().refinement.services.main(), QStringLiteral("anthropic"));

        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "1");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "1");
        QCOMPARE(settings.dictationSnapshot().refinement.services.main(), QStringLiteral("openai"));

        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "1");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "0");
        QCOMPARE(settings.dictationSnapshot().refinement.services.main(), QStringLiteral("openai"));

        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "0");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "0");
        QCOMPARE(settings.dictationSnapshot().refinement.services.main(), QStringLiteral("openai"));

        settings.edit([](AppSettings &draft) {
            draft.refinement.services.order = {QStringLiteral("anthropic")};
            return true;
        });
        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "1");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "1");
        QCOMPARE(settings.dictationSnapshot().refinement.services.main(), QStringLiteral("anthropic"));
    }

    void settingsSnapshotApplyPersistsCliProxyAccounts()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.accounts.openAiCliproxyAccount = QStringLiteral("codex-user@example.com.json");
        draft.accounts.anthropicCliproxyAccount = QStringLiteral("claude-user@example.com.json");

        settings.applySnapshot(draft);

        QCOMPARE(settings.dictationSnapshot().accounts.openAiCliproxyAccount, QStringLiteral("codex-user@example.com.json"));
        QCOMPARE(settings.dictationSnapshot().accounts.anthropicCliproxyAccount, QStringLiteral("claude-user@example.com.json"));
    }

    // Files from before instructions existed load with none, and the new
    // fields survive a save and a reload.
    void refinementInstructionsRoundTrip()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(QStringLiteral("refinement/writingProfiles"),
                                QByteArray(R"([{"profile":"email","cleanupStrength":"balanced","tone":"casual"}])"));
        AppSettings draft = settings.snapshot();
        QCOMPARE(writingProfileSettingsFor(draft.refinement.writingProfiles, WritingProfile::Email).instructions,
                 QString());
        QCOMPARE(draft.refinement.additionalInstructions, QString());
        QCOMPARE(draft.refinement.customSystemPromptEnabled, false);
        QCOMPARE(draft.refinement.customSystemPrompt, QString());

        draft.refinement.writingProfiles[1].instructions = QStringLiteral("Sign off with Best,\nEnzo");
        draft.refinement.writingProfiles[1].outputLanguage = QStringLiteral("Spanish");
        draft.refinement.additionalInstructions = QStringLiteral("Spell it Speecher.");
        draft.refinement.customSystemPromptEnabled = true;
        draft.refinement.customSystemPrompt = QStringLiteral("Clean up my dictation.");
        settings.applySnapshot(draft);

        const AppSettings loaded = SettingsStore().snapshot();
        QCOMPARE(writingProfileSettingsFor(loaded.refinement.writingProfiles, WritingProfile::Email).instructions,
                 QStringLiteral("Sign off with Best,\nEnzo"));
        QCOMPARE(writingProfileSettingsFor(loaded.refinement.writingProfiles, WritingProfile::Email).outputLanguage,
                 QStringLiteral("Spanish"));
        QCOMPARE(loaded.refinement.additionalInstructions, QStringLiteral("Spell it Speecher."));
        QCOMPARE(loaded.refinement.customSystemPromptEnabled, true);
        QCOMPARE(loaded.refinement.customSystemPrompt, QStringLiteral("Clean up my dictation."));
    }

    // A profile's own services survive a save and a reload; one this build
    // can't name reads as the pages' choice, and a profile on the pages'
    // choice stores nothing new.
    void profileProvidersRoundTrip()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(
            QStringLiteral("refinement/writingProfiles"),
            QByteArray(R"([{"profile":"work","cleanupStrength":"balanced","tone":"none","speechProvider":"bogus",)"
                       R"("speechModel":"parakeet","refinementProvider":"none"}])"));
        AppSettings draft = settings.snapshot();
        const auto profile = [](const AppSettings &loaded, const QString &id) {
            return writingProfileSettingsFor(loaded.refinement.writingProfiles, id);
        };
        QCOMPARE(profile(draft, WritingProfile::Work).speechFirst, std::nullopt);
        QCOMPARE(profile(draft, WritingProfile::Work).refinementFirst, std::nullopt);

        draft.refinement.writingProfiles[1].refinementFirst = ServiceChoice{QStringLiteral("anthropic"), QStringLiteral("claude-sonnet-5-5")};
        draft.refinement.writingProfiles[3].speechFirst = ServiceChoice{QStringLiteral("local"), QStringLiteral("moonshine-small")};
        settings.applySnapshot(draft);

        const AppSettings loaded = SettingsStore().snapshot();
        QCOMPARE(profile(loaded, WritingProfile::Email).refinementFirst,
                 (ServiceChoice{QStringLiteral("anthropic"), QStringLiteral("claude-sonnet-5-5")}));
        QCOMPARE(profile(loaded, WritingProfile::Email).speechFirst, std::nullopt);
        QCOMPARE(profile(loaded, WritingProfile::AiCoding).speechFirst,
                 (ServiceChoice{QStringLiteral("local"), QStringLiteral("moonshine-small")}));
        const QByteArray stored = settings.raw().value(QStringLiteral("refinement/writingProfiles")).toByteArray();
        QVERIFY(stored.contains(R"("profile":"work","tone":"none"})"));

        // A profile saved on Haiku 4.5 loads on Haiku 5.5, which replaced it.
        settings.raw().setValue(
            QStringLiteral("refinement/writingProfiles"),
            QByteArray(R"([{"profile":"email","cleanupStrength":"balanced","tone":"none",)"
                       R"("refinementProvider":"anthropic","refinementModel":"claude-haiku-4-5"}])"));
        QCOMPARE(profile(SettingsStore().snapshot(), WritingProfile::Email).refinementFirst->model,
                 QStringLiteral("claude-haiku-5-5"));
        settings.raw().clear();
    }

    // The profile's service goes first, then the page's whole order without
    // it; its model rides along, and the page's entry stays where the
    // profile names no model.
    void profileProvidersLeadThePagesChains()
    {
        AppSettings settings;
        settings.speech.services.order = {QStringLiteral("claude"), QStringLiteral("local"), QStringLiteral("codex")};
        localModelId(settings.speech) = QStringLiteral("parakeet");
        settings.refinement.services.order = {QStringLiteral("openai"), QStringLiteral("local")};
        settings.refinement.services.choice(QStringLiteral("anthropic")).model = QStringLiteral("claude-opus-5-5");

        WritingProfileSettings profile;
        AppSettings resolved = withWritingProfileProviders(settings, profile);
        QCOMPARE(resolved.speech.services.main(), QStringLiteral("claude"));
        QCOMPARE(resolved.speech.services.order.mid(1), (QStringList{QStringLiteral("local"), QStringLiteral("codex")}));
        QCOMPARE(resolved.refinement.services.main(), QStringLiteral("openai"));

        profile.speechFirst = ServiceChoice{QStringLiteral("local"), QStringLiteral("moonshine-small")};
        profile.refinementFirst = ServiceChoice{QStringLiteral("anthropic"), QStringLiteral("claude-sonnet-5-5")};
        resolved = withWritingProfileProviders(settings, profile);
        QCOMPARE(resolved.speech.services.main(), QStringLiteral("local"));
        QCOMPARE(localModelId(resolved.speech), QStringLiteral("moonshine-small"));
        QCOMPARE(resolved.speech.services.order.mid(1), (QStringList{QStringLiteral("claude"), QStringLiteral("codex")}));
        QCOMPARE(resolved.refinement.services.main(), QStringLiteral("anthropic"));
        QCOMPARE(resolved.refinement.services.choice(QStringLiteral("anthropic")).model, QStringLiteral("claude-sonnet-5-5"));
        QCOMPARE(resolved.refinement.services.choice(QStringLiteral("openai")).model, settings.refinement.services.choice(QStringLiteral("openai")).model);
        QCOMPARE(resolved.refinement.services.order.mid(1),
                 (QStringList{QStringLiteral("openai"), QStringLiteral("local")}));

        // The page's whole chain follows, even a full one: the profile's
        // service makes it one longer rather than dropping the last.
        settings.speech.services.order = {settings.speech.services.main(), QStringLiteral("codex"), QStringLiteral("endpoint")};
        settings.refinement.services.order = {settings.refinement.services.main(), QStringLiteral("local"), QStringLiteral("endpoint")};
        resolved = withWritingProfileProviders(settings, profile);
        QCOMPARE(resolved.speech.services.order.mid(1),
                 (QStringList{QStringLiteral("claude"), QStringLiteral("codex"), QStringLiteral("endpoint")}));
        QCOMPARE(serviceChain(resolved.speech),
                 (QStringList{QStringLiteral("local"), QStringLiteral("claude"), QStringLiteral("codex"),
                              QStringLiteral("endpoint")}));
        QCOMPARE(resolved.refinement.services.order.mid(1),
                 (QStringList{QStringLiteral("openai"), QStringLiteral("local"), QStringLiteral("endpoint")}));

        // With Clean up my words off nothing refines, whatever the profile
        // picks.
        settings.refinement.enabled = false;
        resolved = withWritingProfileProviders(settings, profile);
        QVERIFY(serviceChain(resolved.refinement).isEmpty());
    }

    // A profile's Local Model that can't hear the Spoken Language is passed
    // over for the Dictation page's service.
    void profileSpeechThatCantHearTheLanguageIsPassedOver()
    {
        AppSettings settings;
        settings.speech.services.order = {QStringLiteral("claude")};
        settings.speech.language = QStringLiteral("ja");
        WritingProfileSettings profile;
        profile.speechFirst = ServiceChoice{QStringLiteral("local"), QStringLiteral("parakeet")};

        AppSettings resolved = withWritingProfileProviders(settings, profile);
        QCOMPARE(resolved.speech.services.main(), QStringLiteral("claude"));
        QCOMPARE(localModelId(resolved.speech), localModelId(settings.speech));

        profile.speechFirst->model = QStringLiteral("qwen3-asr");
        resolved = withWritingProfileProviders(settings, profile);
        QCOMPARE(resolved.speech.services.main(), QStringLiteral("local"));
        QCOMPARE(localModelId(resolved.speech), QStringLiteral("qwen3-asr"));
    }

    // A profile keeps a custom tone or level that still exists, and falls back
    // to no tone override and Medium once it is deleted.
    void profilesKeepCustomChoicesUntilTheyAreDeleted()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.refinement.customTones = {
            {QStringLiteral("custom_terse"), QStringLiteral("Terse"), QStringLiteral("Short.")}};
        draft.refinement.customCleanupLevels = {{QStringLiteral("custom_notes"), QStringLiteral("Notes"),
                                                 QStringLiteral("light_cleanup"), QStringLiteral("Bullets.")}};
        draft.refinement.writingProfiles = {
            {WritingProfile::Email, QStringLiteral("custom_notes"), QStringLiteral("custom_terse")}};
        settings.applySnapshot(draft);
        const auto email = [](const AppSettings &loaded) {
            return writingProfileSettingsFor(loaded.refinement.writingProfiles, WritingProfile::Email);
        };
        AppSettings loaded = SettingsStore().snapshot();
        QCOMPARE(loaded.refinement.customTones, draft.refinement.customTones);
        QCOMPARE(loaded.refinement.customCleanupLevels, draft.refinement.customCleanupLevels);
        QCOMPARE(email(loaded).cleanupStrength, QStringLiteral("custom_notes"));
        QCOMPARE(email(loaded).tone, QStringLiteral("custom_terse"));

        loaded.refinement.customTones.clear();
        loaded.refinement.customCleanupLevels.clear();
        settings.applySnapshot(loaded);
        const AppSettings deleted = SettingsStore().snapshot();
        QCOMPARE(email(deleted).cleanupStrength, QStringLiteral("balanced"));
        QCOMPARE(email(deleted).tone, QStringLiteral("none"));
    }

    // A file from before custom profiles loads the five built-ins, and saving
    // it back writes the same array.
    void oldSettingsLoadTheSameProfiles()
    {
        SettingsStore settings;
        settings.raw().clear();
        const QByteArray stored(
            R"([{"cleanupStrength":"strong_polish","instructions":"","profile":"work","tone":"formal"},)"
            R"({"cleanupStrength":"balanced","instructions":"","profile":"email","tone":"none"},)"
            R"({"cleanupStrength":"balanced","instructions":"","profile":"personal","tone":"none"},)"
            R"({"cleanupStrength":"balanced","instructions":"","profile":"ai_coding","tone":"none"},)"
            R"({"cleanupStrength":"light_cleanup","instructions":"","profile":"other","tone":"none"}])");
        settings.raw().setValue(QStringLiteral("refinement/writingProfiles"), stored);
        QStringList ids;
        for (const WritingProfileSettings &profile : settings.dictationSnapshot().refinement.writingProfiles) {
            ids << profile.profile;
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("work"), QStringLiteral("email"), QStringLiteral("personal"),
                                   QStringLiteral("ai_coding"), QStringLiteral("other")}));
        settings.applySnapshot(settings.snapshot());
        QCOMPARE(settings.raw().value(QStringLiteral("refinement/writingProfiles")).toByteArray(), stored);
        settings.raw().clear();
    }

    // An App or App Type keeps a custom profile while it exists. Deleting
    // the profile leaves each following its wider place, and Other apps on
    // the Other profile.
    void deletingAProfileClearsItFromAppsAndTypes()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.refinement.writingProfiles.append(
            {QStringLiteral("custom_standup"), QStringLiteral("light_cleanup"), QStringLiteral("none"), QString(),
             QStringLiteral("Standup")});
        test::setOtherAppsProfile(draft.apps, QStringLiteral("custom_standup"));
        draft.apps.apps = {
            test::textApp(QStringLiteral("zulip"), std::nullopt, QStringLiteral("custom_standup")),
            test::textApp(QStringLiteral("mattermost"), AppCategory::Browser, QStringLiteral("custom_standup")),
        };
        settings.applySnapshot(draft);
        AppSettings loaded = SettingsStore().snapshot();
        QCOMPARE(loaded.apps, draft.apps);

        loaded.refinement.writingProfiles.removeLast();
        settings.applySnapshot(loaded);
        const AppSettings deleted = SettingsStore().snapshot();
        QCOMPARE(deleted.apps.apps.size(), 2);
        QCOMPARE(deleted.apps.apps.at(0).writingProfile, std::nullopt);
        QCOMPARE(deleted.apps.apps.at(1).type, AppCategory::Browser);
        QCOMPARE(deleted.apps.apps.at(1).writingProfile, std::nullopt);
        QCOMPARE(fallbackWritingProfile(deleted.apps), WritingProfile::Other);
        settings.raw().clear();
    }

    void snapshotApplyNeverPinsADetectedCliProxyDirectory()
    {
        SettingsStore settings;
        settings.raw().clear();

        // An untouched snapshot round-trip must not write the auto-detected
        // directory into settings: cliproxyOauthDir() resolves a real path,
        // but only the configured value may persist.
        settings.applySnapshot(settings.snapshot());
        QCOMPARE(settings.dictationSnapshot().accounts.cliproxyOauthDirConfigured, QString());

        AppSettings draft = settings.snapshot();
        draft.accounts.cliproxyOauthDirConfigured = QStringLiteral("/custom/cliproxy");
        settings.applySnapshot(draft);
        QCOMPARE(settings.dictationSnapshot().accounts.cliproxyOauthDirConfigured, QStringLiteral("/custom/cliproxy"));
        QCOMPARE(settings.dictationSnapshot().accounts.cliproxyOauthDir, QStringLiteral("/custom/cliproxy"));

        // Clearing the field returns to automatic detection.
        draft = settings.snapshot();
        draft.accounts.cliproxyOauthDirConfigured.clear();
        settings.applySnapshot(draft);
        QCOMPARE(settings.dictationSnapshot().accounts.cliproxyOauthDirConfigured, QString());
    }

    void launchAtLoginRoundTripsThroughSnapshotApply()
    {
        SettingsStore settings;
        settings.raw().clear();
        std::optional<bool> reconciled;
        settings.setLaunchAtLoginReconciler(
            [&reconciled](bool enabled, QString *) {
                reconciled = enabled;
                return true;
            });
        AppSettings draft = settings.snapshot();
        draft.launchAtLogin = !draft.launchAtLogin;

        settings.applySnapshot(draft);

        QCOMPARE(settings.dictationSnapshot().launchAtLogin, draft.launchAtLogin);
        QCOMPARE(settings.snapshot().launchAtLogin, draft.launchAtLogin);
        QCOMPARE(reconciled, std::optional<bool>(draft.launchAtLogin));
    }

    // Saving a key waits for the keyring while other work runs. An edit made
    // then outlives the save that was waiting, whether it saves the key being
    // written or one still to come.
    void anEditDuringAKeyringWaitIsNotOverwritten()
    {
        qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE"); });
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.speech.server.apiKey = QStringLiteral("speech-key");
        draft.refinement.server.apiKey = QStringLiteral("refinement-key");
        QTimer::singleShot(0, &settings, [&settings] {
            settings.edit([](AppSettings &nested) {
                nested.refinement.services.choice(QStringLiteral("openai")).model = QStringLiteral("nested-model");
                nested.speech.server.apiKey = QStringLiteral("nested-speech-key");
                nested.accounts.cliproxyApiKey = QStringLiteral("nested-proxy-key");
                return true;
            });
        });

        settings.applySnapshot(draft);

        QTRY_COMPARE(SettingsStore().dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).model, QStringLiteral("nested-model"));
        QTRY_COMPARE(SettingsStore().snapshot().speech.server.apiKey, QStringLiteral("nested-speech-key"));
        QTRY_COMPARE(SettingsStore().snapshot().accounts.cliproxyApiKey, QStringLiteral("nested-proxy-key"));
        QTRY_COMPARE(SettingsStore().snapshot().refinement.server.apiKey, QStringLiteral("refinement-key"));
        QCOMPARE(settings.snapshot().speech.server.apiKey, QStringLiteral("nested-speech-key"));
        settings.raw().clear();
    }

    // An edit made during the wait reads the key being saved, so leaving it
    // alone does not save the key it replaces back over it.
    void anEditDuringAKeyringWaitKeepsTheKeyBeingSaved()
    {
        qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE"); });
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.speech.server.apiKey = QStringLiteral("speech-key");
        QTimer::singleShot(0, &settings, [&settings] {
            settings.edit([](AppSettings &nested) {
                nested.refinement.services.choice(QStringLiteral("openai")).model = QStringLiteral("nested-model");
                return true;
            });
        });

        settings.applySnapshot(draft);

        QTRY_COMPARE(SettingsStore().dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).model, QStringLiteral("nested-model"));
        QTRY_COMPARE(SettingsStore().snapshot().speech.server.apiKey, QStringLiteral("speech-key"));
        settings.raw().clear();
    }

    void settingsBindingRulesRoundTrip()
    {
        SettingsStore().raw().clear();
        SettingsCodecs settings;

        const QList<BindingRule> rules{
            {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
            {QStringLiteral("signature"), QStringLiteral("Line 1\nLine 2")},
        };
        QString error;
        QVERIFY(settings.setBindingRules(rules, &error));
        QVERIFY(error.isEmpty());

        const QList<BindingRule> loaded = settings.bindingRules();
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(0).phrase, QStringLiteral("my email"));
        QCOMPARE(loaded.at(0).replacement, QStringLiteral("efox@example.com"));
        QCOMPARE(loaded.at(1).phrase, QStringLiteral("signature"));
        QCOMPARE(loaded.at(1).replacement, QStringLiteral("Line 1\nLine 2"));

        const AppSettings snapshot = settings.snapshot();
        QCOMPARE(snapshot.bindings.size(), 2);
        QCOMPARE(snapshot.bindings.at(1).replacement, QStringLiteral("Line 1\nLine 2"));

        QVERIFY(!settings.setBindingRules({
            {QStringLiteral("my,email"), QStringLiteral("one")},
            {QStringLiteral("MY email"), QStringLiteral("two")},
        }, &error));
        QVERIFY(error.contains(QStringLiteral("duplicates")));
        QCOMPARE(settings.bindingRules().size(), 2);
    }

    void correctionEvidenceActivatesByConfidenceWithoutExposingPendingEvidence()
    {
        SettingsStore settings;
        settings.raw().clear();
        QVERIFY(settings.dictationSnapshot().correctionLearningEnabled);

        const CorrectionEvidence medium{QStringLiteral("cute"), QStringLiteral("Qt"), 0.75};
        QVERIFY(!settings.recordCorrectionEvidence(medium, QStringLiteral("org.kde.kate")));
        QVERIFY(settings.dictationSnapshot().learnedCorrections.isEmpty());
        QVERIFY(settings.snapshot().learnedCorrections.isEmpty());

        QVERIFY(settings.recordCorrectionEvidence(medium, QStringLiteral("org.kde.kate")));

        QList<LearnedCorrection> corrections = settings.dictationSnapshot().learnedCorrections;
        QCOMPARE(corrections.size(), 1);
        QCOMPARE(corrections.first().original, QStringLiteral("cute"));
        QCOMPARE(corrections.first().corrected, QStringLiteral("Qt"));
        QCOMPARE(corrections.first().applicationId, QStringLiteral("org.kde.kate"));
        QCOMPARE(corrections.first().confidence, 0.75);
        QCOMPARE(corrections.first().evidenceCount, 2);
        QVERIFY(corrections.first().enabled);

        AppSettings snapshot = settings.snapshot();
        QVERIFY(snapshot.speech.vocabulary.contains(QStringLiteral("Qt")));
        QVERIFY(!snapshot.bindings.contains(BindingRule{QStringLiteral("cute"), QStringLiteral("Qt")}));

        settings.edit([](AppSettings &draft) { draft.learnedCorrections.first().enabled = false; return true; });
        snapshot = settings.snapshot();
        QVERIFY(!snapshot.speech.vocabulary.contains(QStringLiteral("Qt")));

        settings.edit([](AppSettings &draft) { draft.learnedCorrections.first().enabled = true; return true; });
        QVERIFY(settings.recordCorrectionEvidence(medium, QStringLiteral("org.kde.kate")));
        corrections = settings.dictationSnapshot().learnedCorrections;
        QCOMPARE(corrections.size(), 1);
        QCOMPARE(corrections.first().corrected, QStringLiteral("Qt"));
        QCOMPARE(corrections.first().evidenceCount, 3);

        settings.edit([](AppSettings &draft) { draft.learnedCorrections.clear(); return true; });
        QVERIFY(settings.dictationSnapshot().learnedCorrections.isEmpty());
    }

    void correctionLearningSettingChangesAreObservableWithoutRestart()
    {
        SettingsStore settings;
        settings.raw().clear();
        QSignalSpy changed(&settings, &SettingsStore::correctionLearningEnabledChanged);

        settings.edit([](AppSettings &draft) { draft.correctionLearningEnabled = false; return true; });
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.takeFirst().first().toBool(), false);

        settings.edit([](AppSettings &draft) { draft.correctionLearningEnabled = false; return true; });
        QCOMPARE(changed.count(), 0);

        settings.edit([](AppSettings &draft) { draft.correctionLearningEnabled = true; return true; });
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.takeFirst().first().toBool(), true);
    }

    void highConfidenceEvidenceActivatesImmediatelyAndContradictionsDoNotOverwrite()
    {
        SettingsStore settings;
        settings.raw().clear();

        QVERIFY(settings.recordCorrectionEvidence(
            {QStringLiteral("open ai"), QStringLiteral("OpenAI"), 0.98},
            QStringLiteral("org.kde.kate")));
        QVERIFY(!settings.recordCorrectionEvidence(
            {QStringLiteral("open ai"), QStringLiteral("Open API"), 0.98},
            QStringLiteral("org.kde.kate")));

        const QList<LearnedCorrection> corrections = settings.dictationSnapshot().learnedCorrections;
        QCOMPARE(corrections.size(), 1);
        QCOMPARE(corrections.first().corrected, QStringLiteral("OpenAI"));
        QCOMPARE(corrections.first().evidenceCount, 1);
    }

    void correctionEvidenceHonorsDisableAndPromotesMatchingCrossApplicationEvidence()
    {
        SettingsStore settings;
        settings.raw().clear();
        const CorrectionEvidence evidence{
            QStringLiteral("open ai"), QStringLiteral("OpenAI"), 0.98};

        settings.edit([](AppSettings &draft) { draft.correctionLearningEnabled = false; return true; });
        QVERIFY(!settings.recordCorrectionEvidence(evidence, QStringLiteral("org.kde.kate")));
        QVERIFY(settings.dictationSnapshot().learnedCorrections.isEmpty());

        settings.edit([](AppSettings &draft) { draft.correctionLearningEnabled = true; return true; });
        QVERIFY(settings.recordCorrectionEvidence(evidence, QStringLiteral("org.kde.kate")));
        QVERIFY(settings.recordCorrectionEvidence(evidence, QStringLiteral("org.mozilla.firefox")));

        const QList<LearnedCorrection> corrections = settings.dictationSnapshot().learnedCorrections;
        QCOMPARE(corrections.size(), 1);
        QVERIFY(corrections.first().applicationId.isEmpty());
        QCOMPARE(corrections.first().evidenceCount, 2);
        QVERIFY(corrections.first().lastObservedAtMs >= corrections.first().createdAtMs);

        settings.raw().clear();
        const CorrectionEvidence medium{
            QStringLiteral("cute"), QStringLiteral("Qt"), 0.75};
        QVERIFY(!settings.recordCorrectionEvidence(medium, QStringLiteral("org.kde.kate")));
        QVERIFY(settings.recordCorrectionEvidence(medium, QStringLiteral("org.mozilla.firefox")));
        QCOMPARE(settings.dictationSnapshot().learnedCorrections.size(), 1);
        QVERIFY(settings.dictationSnapshot().learnedCorrections.first().applicationId.isEmpty());
    }

    void lowConfidenceEvidenceIsDiscarded()
    {
        SettingsStore settings;
        settings.raw().clear();

        QVERIFY(!settings.recordCorrectionEvidence(
            {QStringLiteral("cute"), QStringLiteral("Qt"), 0.4},
            QStringLiteral("org.kde.kate")));
        QVERIFY(settings.dictationSnapshot().learnedCorrections.isEmpty());
        QVERIFY(!settings.raw().contains(QStringLiteral("vocabulary/correctionEvidence")));
        QVERIFY(!settings.recordCorrectionEvidence(
            {QStringLiteral("open ai"), QStringLiteral("OpenAI"), 0.98}, {}));
    }

    void correctionAnalysisExtractsLocalizedEvidence()
    {
        const std::optional<CorrectionEvidence> evidence = analyzeCorrection(
            QStringLiteral("I use cute every day"),
            QStringLiteral("I use Qt every day"));

        QVERIFY(evidence);
        QCOMPARE(evidence->original, QStringLiteral("cute"));
        QCOMPARE(evidence->corrected, QStringLiteral("Qt"));
        QVERIFY(evidence->confidence >= 0.65);
    }

    void correctionAnalysisRejectsUnsafeOrAmbiguousEdits()
    {
        QVERIFY(!analyzeCorrection(QStringLiteral("unchanged"), QStringLiteral("unchanged")));
        QVERIFY(!analyzeCorrection(QStringLiteral("hello"), QStringLiteral("hello there")));
        QVERIFY(!analyzeCorrection(QStringLiteral("hello there"), QStringLiteral("hello")));
        QVERIFY(!analyzeCorrection(QStringLiteral("file"), QStringLiteral("files")));
        QVERIFY(!analyzeCorrection(QStringLiteral("files"), QStringLiteral("file")));
        QVERIFY(!analyzeCorrection(QStringLiteral("hello"), QStringLiteral("hello!")));
        QVERIFY(!analyzeCorrection(QStringLiteral("hello!"), QStringLiteral("hello")));
        QVERIFY(!analyzeCorrection(
            QStringLiteral("cute and plasma"),
            QStringLiteral("Qt and Plasma")));
        QVERIFY(!analyzeCorrection(QStringLiteral("cat dog"), QStringLiteral("bat fog")));
        QVERIFY(!analyzeCorrection(
            QStringLiteral("token=old-value"),
            QStringLiteral("token=abcdefghijklmnopqrstuv")));
        QVERIFY(!analyzeCorrection(
            QStringLiteral("email a@example.test"),
            QStringLiteral("email b@example.test")));
        QVERIFY(!analyzeCorrection(
            QStringLiteral("card 4111 1111 1111 1111"),
            QStringLiteral("card 4111 1111 1111 1112")));
        QVERIFY(!analyzeCorrection(
            QStringLiteral("card 4111-1111-1111-1111"),
            QStringLiteral("card 4111-1111-1111-1112")));
        QVERIFY(!analyzeCorrection(
            QStringLiteral("this entire sentence is being rewritten into something else"),
            QStringLiteral("a completely unrelated and substantially different paragraph")));

        const auto casing = analyzeCorrection(QStringLiteral("qt"), QStringLiteral("Qt"));
        QVERIFY(casing);
        QCOMPARE(casing->original, QStringLiteral("qt"));
        QCOMPARE(casing->corrected, QStringLiteral("Qt"));
        QVERIFY(casing->confidence >= 0.9);

        const auto spacing = analyzeCorrection(
            QStringLiteral("Use open ai here"),
            QStringLiteral("Use OpenAI here"));
        QVERIFY(spacing);
        QCOMPARE(spacing->original, QStringLiteral("open ai"));
        QCOMPARE(spacing->corrected, QStringLiteral("OpenAI"));
        QVERIFY(spacing->confidence >= 0.9);
    }

    // Each stored field, set away from its default, reads back the same from
    // the file. The keyring keys are SecretStore's, tested with it.
    void everyStoredFieldRoundTripsThroughAnEdit()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings edited;
        QVERIFY(settings.edit([&edited](AppSettings &s) {
            s.setupCompleted = true;
            s.launchAtLogin = !s.launchAtLogin;
            s.ui = {.previewWords = 12,
                    .transcriptionPreviewEnabled = false,
                    .refinementPreviewEnabled = false,
                    .selectionEditReviewEnabled = false,
                    .theme = QStringLiteral("dark"),
                    .colorScheme = QStringLiteral("installed:Breeze"),
                    .translucency = false,
                    .windowLayout = QStringLiteral("split"),
                    .roundedCorners = false,
                    .boringMode = true,
                    .pauseMediaDuringTranscription = false,
                    .soundsEnabled = true};
            s.speech.services.order = {QStringLiteral("codex"), QStringLiteral("local")};
            s.speech.codexFinalRetranscribe = false;
            s.speech.language = QStringLiteral("de");
            localModelId(s.speech) = QStringLiteral("whisper-small");
            s.speech.local = {.runsOn = {.backend = QStringLiteral("cpu")},
                              .idleUnloadMinutes = 0,
                              .speedTestSeconds = {{QStringLiteral("whisper-small/cpu"), 2.5}},
                              .modelChosen = true};
            s.speech.server = {.baseUrl = QStringLiteral("http://localhost:8080"), .path = QStringLiteral("/inference")};
            s.speech.services.choice(QStringLiteral("endpoint")).model = QStringLiteral("whisper");
            s.audio = {.deviceId = QStringLiteral("device-1"),
                       .mode = QStringLiteral("warm"),
                       .vadEnabled = true,
                       .preRollMs = 300,
                       .postRollMs = 250,
                       .readinessTimeoutMs = 700,
                       .vadThresholdPercent = 4};
            s.refinement.services.order = {QStringLiteral("anthropic"), QStringLiteral("local")};
            s.refinement.customTones = {{.id = QStringLiteral("custom_pirate"),
                                         .name = QStringLiteral("Pirate"),
                                         .instruction = QStringLiteral("Talk like a pirate.")}};
            s.refinement.customCleanupLevels = {{.id = QStringLiteral("custom_tight"),
                                                 .name = QStringLiteral("Tight"),
                                                 .base = QStringLiteral("balanced"),
                                                 .instructions = QStringLiteral("Cut filler.")}};
            for (WritingProfileSettings &profile : s.refinement.writingProfiles) {
                if (profile.profile == WritingProfile::Work) {
                    profile.cleanupStrength = QStringLiteral("custom_tight");
                    profile.tone = QStringLiteral("custom_pirate");
                }
            }
            s.apps.apps = {{.id = QStringLiteral("app:notes"),
                            .name = QStringLiteral("Notes"),
                            .icon = QStringLiteral("org.example.Notes"),
                            .matches = {{AppMatchKind::Id, QStringLiteral("org.example.Notes")},
                                        {AppMatchKind::Name, QStringLiteral("notes")},
                                        {AppMatchKind::WindowTitle, QStringLiteral("Gmail")}},
                            .type = AppCategory::Office,
                            .writingProfile = WritingProfile::Work,
                            .pasting = PasteMethod::StandardPaste}};
            test::setOtherAppsProfile(s.apps, WritingProfile::Work);
            s.apps.types[1].pasting = PasteMethod::ClipboardOnly;
            s.apps.types[0].writingProfile.reset();
            s.refinement.useTargetContext = false;
            s.refinement.includeScreenshotContext = true;
            s.refinement.additionalInstructions = QStringLiteral("Use British spelling.");
            s.refinement.customSystemPromptEnabled = true;
            s.refinement.customSystemPrompt = QStringLiteral("Clean this up.");
            s.refinement.services.choice(QStringLiteral("openai")).model = QStringLiteral("gpt-6.1-sol");
            s.refinement.services.choice(QStringLiteral("openai")).effort = QStringLiteral("high");
            s.refinement.services.choice(QStringLiteral("openai")).speed = QStringLiteral("standard");
            s.refinement.services.choice(QStringLiteral("anthropic")).model = QStringLiteral("claude-fable-5");
            s.refinement.services.choice(QStringLiteral("anthropic")).effort = QStringLiteral("max");
            s.refinement.services.choice(QStringLiteral("anthropic")).speed = QStringLiteral("standard");
            s.accounts.cliproxyBaseUrl = QStringLiteral("http://localhost:8317");
            s.refinement.server = {.baseUrl = QStringLiteral("http://localhost:11434/v1"),
                                   .format = QStringLiteral("anthropic")};
            s.refinement.services.choice(QStringLiteral("endpoint")).model = QStringLiteral("llama");
            s.refinement.localRunner = {.runner = QStringLiteral("lmstudio")};
            s.refinement.services.choice(QStringLiteral("local")).model = QStringLiteral("qwen");
            s.accounts.openAiAuthMode = OpenAiAuthMode::Cliproxy;
            s.accounts.openAiCliproxyAccount = QStringLiteral("codex-a.json");
            s.accounts.anthropicAuthMode = AnthropicAuthMode::Cliproxy;
            s.accounts.anthropicCliproxyAccount = QStringLiteral("claude-b.json");
            s.accounts.cliproxyOauthDirConfigured = QStringLiteral("/custom/cliproxy");
            s.output.steps = {.insert = true, .press = false, .keep = false};
            s.output.format = OutputFormat::Html;
            s.output.completionStatusDurationMs = 2000;
            s.shortcutActivationMode = ShortcutActivationMode::Toggle;
            s.updates = {.channel = UpdateChannel::Nightly,
                         .autoCheck = false,
                         .autoInstall = true,
                         .checkIntervalMinutes = 480,
                         .checkIntervalUnit = QStringLiteral("hours")};
            s.vocabulary = {{.term = QStringLiteral("Speecher"),
                             .source = QStringLiteral("manual"),
                             .context = QStringLiteral("The app.")}};
            s.learnedCorrections = {{.id = QStringLiteral("c1"),
                                     .original = QStringLiteral("teh"),
                                     .corrected = QStringLiteral("the"),
                                     .createdAtMs = 1000,
                                     .confidence = 0.95,
                                     .enabled = true,
                                     .evidenceCount = 3,
                                     .lastObservedAtMs = 2000}};
            s.correctionLearningEnabled = false;
            s.insightsEnabled = false;
            s.bindings = {{.phrase = QStringLiteral("my email"), .replacement = QStringLiteral("efox@example.com")}};
            edited = s;
            return true;
        }));

        const AppSettings saved = SettingsStore().snapshot();
        // Derived from stored fields when read, so they follow the edit.
        edited.speech.vocabulary = saved.speech.vocabulary;
        edited.accounts.cliproxyOauthDir = saved.accounts.cliproxyOauthDir;
        QCOMPARE(saved.setupCompleted, edited.setupCompleted);
        QCOMPARE(saved.launchAtLogin, edited.launchAtLogin);
        QCOMPARE(saved.ui, edited.ui);
        QCOMPARE(saved.speech, edited.speech);
        QCOMPARE(saved.audio, edited.audio);
        QCOMPARE(saved.apps, edited.apps);
        QCOMPARE(saved.refinement, edited.refinement);
        QCOMPARE(saved.accounts, edited.accounts);
        QCOMPARE(saved.output, edited.output);
        QCOMPARE(saved.updates, edited.updates);
        QCOMPARE(saved.shortcutActivationMode, edited.shortcutActivationMode);
        QCOMPARE(saved.bindings, edited.bindings);
        QCOMPARE(saved.vocabulary, edited.vocabulary);
        QCOMPARE(saved.learnedCorrections, edited.learnedCorrections);
        QCOMPARE(saved.correctionLearningEnabled, edited.correctionLearningEnabled);
        QCOMPARE(saved.insightsEnabled, edited.insightsEnabled);
        settings.raw().clear();
    }

    // A field a snapshot derives, or one only its owner writes, would be
    // dropped on save.
    void anEditOfAFieldThatIsNeverStoredSavesNothing()
    {
        SettingsStore settings;
        settings.raw().clear();
        QTest::ignoreMessage(QtCriticalMsg,
                             "Settings: not saved, since the edit changed speech.vocabulary, which is never stored");
        QVERIFY(!settings.edit([](AppSettings &s) {
            s.refinement.services.choice(QStringLiteral("openai")).model = QStringLiteral("gpt-6.1-sol");
            s.speech.vocabulary = {QStringLiteral("Speecher")};
            return true;
        }));
        QTest::ignoreMessage(QtCriticalMsg,
                             "Settings: not saved, since the edit changed output.ydotoolEnabled, which is never stored");
        QVERIFY(!settings.edit([](AppSettings &s) {
            s.refinement.services.choice(QStringLiteral("openai")).model = QStringLiteral("gpt-6.1-sol");
            s.output.ydotoolEnabled = true;
            return true;
        }));
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.services.choice(QStringLiteral("openai")).model, QStringLiteral("gpt-6-luna"));
    }

    void settingsSnapshot()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.ui.previewWords = 12;
            draft.ui.theme = QStringLiteral("dark");
            draft.ui.pauseMediaDuringTranscription = false;
            draft.ui.soundsEnabled = true;
            draft.speech.services.order = {QStringLiteral("claude")};
            return true;
        });
        settings.edit([](AppSettings &draft) {
            draft.vocabulary = {{QStringLiteral("Deepgram Nova 3")}, {QStringLiteral("Speecher")}};
            return true;
        });
        QVERIFY(settings.edit([](AppSettings &draft) {
            draft.bindings = {{QStringLiteral("my email"), QStringLiteral("efox@example.com")}};
            return true;
        }));
        settings.edit([](AppSettings &draft) { draft.refinement.services.order = {QStringLiteral("openai")}; return true; });
        settings.raw().setValue(SettingsKeys::RefinementStyle, QStringLiteral("strong_polish"));
        settings.edit([](AppSettings &draft) {
            draft.refinement.writingProfiles = {
                {WritingProfile::Work, QStringLiteral("strong_polish"), QStringLiteral("formal")},
                {WritingProfile::Email, QStringLiteral("balanced"), QStringLiteral("none")},
                {WritingProfile::Personal, QStringLiteral("light_cleanup"), QStringLiteral("casual")},
                {WritingProfile::AiCoding, QStringLiteral("none"), QStringLiteral("formal")},
                {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
            };
            draft.refinement.useTargetContext = false;
            draft.refinement.includeScreenshotContext = true;
            draft.accounts.openAiAuthMode = OpenAiAuthMode::Env;
            draft.refinement.services.choice(QStringLiteral("openai")).effort = QStringLiteral("high");
            draft.refinement.services.choice(QStringLiteral("openai")).speed = QStringLiteral("standard");
            draft.refinement.services.choice(QStringLiteral("anthropic")).model = QStringLiteral("claude-opus-5");
            draft.accounts.anthropicAuthMode = AnthropicAuthMode::Oauth;
            draft.refinement.services.choice(QStringLiteral("anthropic")).effort = QStringLiteral("xhigh");
            draft.refinement.services.choice(QStringLiteral("anthropic")).speed = QStringLiteral("standard");
            draft.output.format = OutputFormat::Html;
            draft.output.steps.keep = false;
            draft.output.completionStatusDurationMs = 900;
            draft.audio = {
                QStringLiteral("device-1"),
                QStringLiteral("warm"),
                true,
                300,
                250,
                700,
                4,
            };
            return true;
        });

        const AppSettings snapshot = settings.snapshot();
        QCOMPARE(snapshot.ui.previewWords, 12);
        QCOMPARE(snapshot.ui.theme, QStringLiteral("dark"));
        QCOMPARE(snapshot.ui.pauseMediaDuringTranscription, false);
        QCOMPARE(snapshot.ui.soundsEnabled, true);
        QCOMPARE(snapshot.speech.services.main(), QStringLiteral("claude"));
        QCOMPARE(snapshot.speech.vocabulary.size(), 2);
        QCOMPARE(snapshot.audio.deviceId, QStringLiteral("device-1"));
        QCOMPARE(snapshot.audio.mode, QStringLiteral("warm"));
        QCOMPARE(snapshot.audio.vadEnabled, true);
        QCOMPARE(snapshot.audio.preRollMs, 300);
        QCOMPARE(snapshot.audio.postRollMs, 250);
        QCOMPARE(snapshot.audio.readinessTimeoutMs, 700);

        // Values stored by older builds at the old 150 ms floor load at the new floor.
        settings.edit([](AppSettings &draft) {
            draft.audio = {QStringLiteral("device-1"), QStringLiteral("warm"), true, 300, 250, 150, 4};
            return true;
        });
        QCOMPARE(settings.snapshot().audio.readinessTimeoutMs, 500);
        QCOMPARE(snapshot.audio.vadThresholdPercent, 4);
        QCOMPARE(snapshot.bindings.size(), 1);
        QCOMPARE(snapshot.bindings.at(0).replacement, QStringLiteral("efox@example.com"));
        QCOMPARE(snapshot.refinement.services.main(), QStringLiteral("openai"));
        QCOMPARE(snapshot.refinement.style, QStringLiteral("strong_polish"));
        QCOMPARE(writingProfileSettingsFor(snapshot.refinement.writingProfiles, WritingProfile::Work).tone,
                 QStringLiteral("formal"));
        QCOMPARE(writingProfileSettingsFor(snapshot.refinement.writingProfiles, WritingProfile::AiCoding).tone,
                 QStringLiteral("formal"));
        QCOMPARE(snapshot.refinement.useTargetContext, false);
        QCOMPARE(snapshot.refinement.includeScreenshotContext, true);
        QCOMPARE(snapshot.accounts.openAiAuthMode, OpenAiAuthMode::Env);
        QCOMPARE(snapshot.refinement.services.choice(QStringLiteral("openai")).effort, QStringLiteral("high"));
        QCOMPARE(snapshot.refinement.services.choice(QStringLiteral("openai")).speed, QStringLiteral("standard"));
        QCOMPARE(snapshot.refinement.services.choice(QStringLiteral("anthropic")).model, QStringLiteral("claude-opus-5"));
        QCOMPARE(snapshot.accounts.anthropicAuthMode, AnthropicAuthMode::Oauth);
        QCOMPARE(snapshot.refinement.services.choice(QStringLiteral("anthropic")).effort, QStringLiteral("xhigh"));
        QCOMPARE(snapshot.refinement.services.choice(QStringLiteral("anthropic")).speed, QStringLiteral("standard"));
        // Claude Code's config directory, as CLAUDE_CONFIG_DIR (set by test_main) moves it.
        QCOMPARE(snapshot.accounts.claudeCredentialsPath,
                 QDir(qEnvironmentVariable("CLAUDE_CONFIG_DIR")).filePath(QStringLiteral(".credentials.json")));
        QCOMPARE(snapshot.output.steps, (DeliverySteps{.insert = true, .press = true, .keep = false}));
        QCOMPARE(snapshot.output.format, OutputFormat::Html);
        QCOMPARE(snapshot.output.ydotoolEnabled, false);
        QCOMPARE(snapshot.output.completionStatusDurationMs, 900);
    }

    void removingAToneMovesItsProfilesAndUndoMovesBackOnlyTheUnchanged()
    {
        AppSettings settings;
        settings.refinement.customTones = {{QStringLiteral("custom_dry"), QStringLiteral("Dry"), QStringLiteral("Deadpan.")},
                                           {QStringLiteral("custom_warm"), QStringLiteral("Warm"), QStringLiteral("Kind.")}};
        const auto setTone = [&settings](const QString &profile, const QString &tone) {
            for (WritingProfileSettings &entry : settings.refinement.writingProfiles) {
                if (entry.profile == profile) {
                    entry.tone = tone;
                }
            }
        };
        const auto toneOf = [&settings](const QString &profile) {
            return writingProfileSettingsFor(settings.refinement.writingProfiles, profile).tone;
        };
        setTone(WritingProfile::Work, QStringLiteral("custom_dry"));
        setTone(WritingProfile::Email, QStringLiteral("custom_dry"));
        setTone(WritingProfile::Personal, QStringLiteral("formal"));

        const SettingsEdit undo = removeCustomTone(settings, QStringLiteral("custom_dry"));
        QCOMPARE(settings.refinement.customTones.size(), 1);
        QCOMPARE(settings.refinement.customTones.first().id, QStringLiteral("custom_warm"));
        QCOMPARE(toneOf(WritingProfile::Work), QStringLiteral("none"));
        QCOMPARE(toneOf(WritingProfile::Email), QStringLiteral("none"));
        QCOMPARE(toneOf(WritingProfile::Personal), QStringLiteral("formal"));

        // Changed while the Undo was offered: the change stays.
        setTone(WritingProfile::Email, QStringLiteral("casual"));
        QVERIFY(undo);
        undo(settings);
        QCOMPARE(settings.refinement.customTones.size(), 2);
        QCOMPARE(settings.refinement.customTones.first().id, QStringLiteral("custom_dry"));
        QCOMPARE(toneOf(WritingProfile::Work), QStringLiteral("custom_dry"));
        QCOMPARE(toneOf(WritingProfile::Email), QStringLiteral("casual"));

        QVERIFY(!removeCustomTone(settings, QStringLiteral("custom_gone")));
    }

    void removingACleanupLevelFallsBackToItsBase()
    {
        AppSettings settings;
        settings.refinement.customCleanupLevels = {
            {QStringLiteral("custom_notes"), QStringLiteral("Notes"), QStringLiteral("strong_polish"), QStringLiteral("Bullets.")},
            {QStringLiteral("custom_raw"), QStringLiteral("Raw"), kCustomOnlyCleanupBase, QStringLiteral("Only commas.")}};
        for (WritingProfileSettings &entry : settings.refinement.writingProfiles) {
            if (entry.profile == WritingProfile::Work) {
                entry.cleanupStrength = QStringLiteral("custom_notes");
            } else if (entry.profile == WritingProfile::Email) {
                entry.cleanupStrength = QStringLiteral("custom_raw");
            }
        }
        const auto levelOf = [&settings](const QString &profile) {
            return writingProfileSettingsFor(settings.refinement.writingProfiles, profile).cleanupStrength;
        };

        const SettingsEdit undoNotes = removeCustomCleanupLevel(settings, QStringLiteral("custom_notes"));
        const SettingsEdit undoRaw = removeCustomCleanupLevel(settings, QStringLiteral("custom_raw"));
        QVERIFY(settings.refinement.customCleanupLevels.isEmpty());
        QCOMPARE(levelOf(WritingProfile::Work), QStringLiteral("strong_polish"));
        QCOMPARE(levelOf(WritingProfile::Email), QStringLiteral("balanced"));

        undoRaw(settings);
        undoNotes(settings);
        QCOMPARE(settings.refinement.customCleanupLevels.size(), 2);
        QCOMPARE(settings.refinement.customCleanupLevels.first().id, QStringLiteral("custom_notes"));
        QCOMPARE(levelOf(WritingProfile::Work), QStringLiteral("custom_notes"));
        QCOMPARE(levelOf(WritingProfile::Email), QStringLiteral("custom_raw"));
    }

    void newCustomIdsAreFreshAndPrefixed()
    {
        const QString first = newCustomId(QStringLiteral("custom_tone"), {});
        const QString second = newCustomId(QStringLiteral("custom_tone"), {first});
        QVERIFY(first.startsWith(QStringLiteral("custom_tone_")));
        QVERIFY(first.size() > QStringLiteral("custom_tone_").size());
        QVERIFY(second != first);
        QCOMPARE(writingProfileFromName(first), first);
    }
};

int runSettingsTests(int argc, char **argv)
{
    SettingsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_settings.moc"
