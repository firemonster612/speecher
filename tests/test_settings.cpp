#include "common/test_prelude.h"
#include "core/settings/SettingsKeys.h"
#include <QProcess>
#ifdef SPEECHER_WITH_QKEYCHAIN
#include "core/KeyringResult.h"
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
        QVERIFY(settings.localSpeechSettings().modelChosen);
        auto saved = settings.localSpeechSettings();
        saved.idleUnloadMinutes = 60;
        settings.setLocalSpeechSettings(saved);
        QVERIFY(SettingsStore().localSpeechSettings().modelChosen);
    }

    void localRunsOnDefaultsToAutomaticAndKeepsAnOldCard()
    {
        SettingsStore settings;
        settings.raw().clear();
        QCOMPARE(settings.localSpeechSettings().runsOn, LocalRunsOn{});
        QCOMPARE(settings.localSpeechSettings().runsOn.backend, QStringLiteral("auto"));

        // A graphics card saved before backends could be chosen is kept, on
        // whichever backend reaches it, and survives the next write.
        settings.raw().setValue(SettingsKeys::LocalDevice, QStringLiteral("0000:c1:00.0"));
        const LocalRunsOn legacy{QStringLiteral("auto"), QStringLiteral("0000:c1:00.0")};
        QCOMPARE(settings.localSpeechSettings().runsOn, legacy);
        settings.setLocalSpeechSettings(settings.localSpeechSettings());
        QCOMPARE(SettingsStore().localSpeechSettings().runsOn, legacy);
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
        QCOMPARE(SettingsStore().snapshot().speech.endpoint.apiKey, QStringLiteral("speech-secret"));
    }

#ifdef SPEECHER_WITH_QKEYCHAIN
    void missingKeyringEntryOrBackendCountsAsSuccessfulDeletion()
    {
        QVERIFY(keyringDeletionSucceeded(QKeychain::NoError));
        QVERIFY(keyringDeletionSucceeded(QKeychain::EntryNotFound));
        QVERIFY(keyringDeletionSucceeded(QKeychain::NoBackendAvailable));
        QVERIFY(!keyringDeletionSucceeded(QKeychain::AccessDeniedByUser));
    }
#endif

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

    // Paste with no longer offers inserting directly or copying only. Under
    // either, a rule that said to paste only inserted, or only copied, so those
    // rules take that method now.
    void outputMethodMigrationMovesPastingRulesToTheOldMethod()
    {
        using Scope = PasteRuleScope;
        using Method = PasteMethod;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        const QList<PasteRule> stored{
            {Scope::Category, QStringLiteral("terminal"), Method::TerminalPaste, true},
            {Scope::Category, QStringLiteral("browser"), Method::ClipboardOnly, true},
            {Scope::Application, QStringLiteral("org.kde.kate"), Method::StandardPaste, false},
            {Scope::Application, QStringLiteral("org.kde.konsole"), Method::DirectInsert, true},
            {Scope::Global, QString(), Method::StandardPaste, true},
        };
        const auto migrated = [&settings, &stored](const QString &method) {
            settings.setValue(QStringLiteral("output/method"), method);
            settings.setValue(QStringLiteral("output/pasteRules"), pasteRulesToJson(stored));
            migrateOutputMethod(settings);
            return pasteRulesFromJson(settings.value(QStringLiteral("output/pasteRules")).toByteArray());
        };

        QCOMPARE(migrated(QStringLiteral("direct_insert")),
                 (QList<PasteRule>{
                     {Scope::Category, QStringLiteral("terminal"), Method::DirectInsert, true},
                     {Scope::Category, QStringLiteral("browser"), Method::ClipboardOnly, true},
                     {Scope::Application, QStringLiteral("org.kde.kate"), Method::DirectInsert, false},
                     {Scope::Application, QStringLiteral("org.kde.konsole"), Method::DirectInsert, true},
                     {Scope::Global, QString(), Method::DirectInsert, true},
                 }));
        QCOMPARE(settings.value(QStringLiteral("output/method")).toString(), QStringLiteral("automatic"));

        const QList<PasteRule> copyOnly{
            {Scope::Category, QStringLiteral("terminal"), Method::ClipboardOnly, true},
            {Scope::Category, QStringLiteral("browser"), Method::ClipboardOnly, true},
            {Scope::Application, QStringLiteral("org.kde.kate"), Method::ClipboardOnly, false},
            {Scope::Application, QStringLiteral("org.kde.konsole"), Method::DirectInsert, true},
            {Scope::Global, QString(), Method::ClipboardOnly, true},
        };
        QCOMPARE(migrated(QStringLiteral("qt-clipboard")), copyOnly);
        QCOMPARE(migrated(QStringLiteral("wl-copy")), copyOnly);
        QCOMPARE(settings.value(QStringLiteral("output/method")).toString(), QStringLiteral("automatic"));

        // Nothing stored means the default rules, which migrate the same way.
        settings.remove(QStringLiteral("output/pasteRules"));
        settings.setValue(QStringLiteral("output/method"), QStringLiteral("direct_insert"));
        migrateOutputMethod(settings);
        QCOMPARE(pasteRulesFromJson(settings.value(QStringLiteral("output/pasteRules")).toByteArray()),
                 (QList<PasteRule>{
                     {Scope::Category, QStringLiteral("terminal"), Method::DirectInsert, true},
                     {Scope::Global, QString(), Method::DirectInsert, true},
                 }));

        // Automatic and keyboard paste are left alone.
        QCOMPARE(migrated(QStringLiteral("ydotool")), stored);
        QCOMPARE(settings.value(QStringLiteral("output/method")).toString(), QStringLiteral("ydotool"));
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

    // A bare key is one that types or that apps act on: no modifier but Shift.
    void bareKeysAreCombinationsWithoutARealModifier()
    {
        QVERIFY(ShortcutBinding(QKeySequence(Qt::Key_C)).isBareKey());
        QVERIFY(ShortcutBinding(QKeySequence(Qt::Key_Escape)).isBareKey());
        QVERIFY(ShortcutBinding(QKeySequence(Qt::SHIFT | Qt::Key_P)).isBareKey());
        QVERIFY(!ShortcutBinding(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X)).isBareKey());
        QVERIFY(!ShortcutBinding::singleKey(QStringLiteral("F13")).isBareKey());
        QVERIFY(!ShortcutBinding().isBareKey());
    }

    // A watched single key and a combination without modifiers can name the
    // same physical key; two shortcuts must not share it either way.
    void aSingleKeyAndABareCombinationCanBeTheSameKey()
    {
        const ShortcutBinding f13 = ShortcutBinding::singleKey(QStringLiteral("F13"));
        QVERIFY(f13.sameKeysAs(ShortcutBinding(QKeySequence(Qt::Key_F13))));
        QVERIFY(ShortcutBinding(QKeySequence(Qt::Key_C))
                    .sameKeysAs(ShortcutBinding::singleKey(QStringLiteral("KeyC"))));
        QVERIFY(ShortcutBinding::singleKey(QStringLiteral("Escape"))
                    .sameKeysAs(ShortcutBinding(QKeySequence(Qt::Key_Escape))));
        QVERIFY(!f13.sameKeysAs(ShortcutBinding(QKeySequence(Qt::CTRL | Qt::Key_F13))));
        QVERIFY(!f13.sameKeysAs(ShortcutBinding(QKeySequence(Qt::Key_F14))));
        QVERIFY(!f13.sameKeysAs(ShortcutBinding()));
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
        QCOMPARE(settings.setupCompleted(), false);
        QCOMPARE(settings.previewWords(), 7);
        QCOMPARE(settings.theme(), QStringLiteral("system"));
        QCOMPARE(settings.pauseMediaDuringTranscription(), true);
        QCOMPARE(settings.soundsEnabled(), false);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        QCOMPARE(settings.launchAtLogin(), true);
#else
        QCOMPARE(settings.launchAtLogin(), false);
#endif
        QCOMPARE(settings.customVocabulary(), QStringList());
        QCOMPARE(settings.bindingRules().size(), 0);
        QCOMPARE(settings.refinementProvider(), QStringLiteral("openai"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("balanced"));
        QCOMPARE(settings.defaultWritingProfile(), QStringLiteral("other"));
        QCOMPARE(settings.writingProfileSettings(), defaultWritingProfileSettings());
        QCOMPARE(settings.writingProfileSettings().size(), 5);
        QVERIFY(settings.writingProfileOverrides().isEmpty());
        QVERIFY(settings.appRecognitionRules().isEmpty());
        QCOMPARE(settings.useTargetContext(), true);
        QCOMPARE(settings.includeScreenshotContext(), false);
        QCOMPARE(settings.openAiModel(), QStringLiteral("gpt-6-luna"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("auto"));
        QCOMPARE(settings.openAiEffort(), QStringLiteral("none"));
        QCOMPARE(settings.openAiSpeed(), QStringLiteral("fast"));
        QCOMPARE(settings.anthropicModel(), QStringLiteral("claude-opus-5-5"));
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("oauth"));
        QCOMPARE(settings.anthropicEffort(), QStringLiteral("low"));
        QCOMPARE(settings.anthropicFastMode(), true);
        // The Codex accuracy pass is on until someone turns it off.
        QCOMPARE(settings.codexFinalRetranscribe(), true);
        settings.setCodexFinalRetranscribe(false);
        QCOMPARE(settings.codexFinalRetranscribe(), false);
        QCOMPARE(settings.snapshot().speech.language, QStringLiteral("en"));
        QCOMPARE(settings.outputMethod(), QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(settings.outputFormat(), OutputFormat::PlainText);
        QCOMPARE(settings.pasteRules(), defaultPasteRules());
        QCOMPARE(settings.ydotoolEnabled(), false);
        QCOMPARE(settings.restoreClipboardAfterTyping(), false);
        QCOMPARE(settings.snapshot().output.completionStatusDurationMs, 500);
        QCOMPARE(settings.audioInputDeviceId(), QString());
        QCOMPARE(settings.audioCaptureMode(), QStringLiteral("on_demand"));
        QCOMPARE(settings.audioVadEnabled(), false);
        QCOMPARE(settings.audioPreRollMs(), 250);
        QCOMPARE(settings.audioPostRollMs(), 200);
        QCOMPARE(settings.audioReadinessTimeoutMs(), 900);
        QCOMPARE(settings.audioVadThresholdPercent(), 2);
        QCOMPARE(settings.shortcutActivationMode(), ShortcutActivationMode::Hybrid);

        settings.setSetupCompleted(true);
        QCOMPARE(settings.setupCompleted(), true);
        QCOMPARE(settings.snapshot().setupCompleted, true);

        settings.setRefinementStyle(QStringLiteral("strong_polish"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("strong_polish"));
        settings.setRefinementStyle(QStringLiteral("balanced"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("balanced"));
        settings.setRefinementStyle(QStringLiteral("light_cleanup"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("light_cleanup"));
        settings.setRefinementStyle(QStringLiteral("unknown"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("balanced"));
        settings.setDefaultWritingProfile(QStringLiteral("personal"));
        QCOMPARE(settings.defaultWritingProfile(), QStringLiteral("personal"));
        settings.setDefaultWritingProfile(QStringLiteral("ai_coding"));
        QCOMPARE(settings.defaultWritingProfile(), QStringLiteral("ai_coding"));
        settings.raw().setValue(QStringLiteral("refinement/writingProfiles"),
                                QByteArray(R"([{"profile":"work","cleanupStrength":"strong_polish","tone":"formal"},)"
                                           R"({"profile":"email","cleanupStrength":"balanced","tone":"casual"},)"
                                           R"({"profile":"personal","cleanupStrength":"light_cleanup","tone":"very_casual"},)"
                                           R"({"profile":"other","cleanupStrength":"balanced","tone":"none"}])"));
        const WritingProfileSettings migratedAiCoding =
            writingProfileSettingsFor(settings.writingProfileSettings(), WritingProfile::AiCoding);
        QCOMPARE(migratedAiCoding.cleanupStrength, QStringLiteral("strong_polish"));
        QCOMPARE(migratedAiCoding.tone, QStringLiteral("formal"));
        settings.setWritingProfileSettings({
            {WritingProfile::Work, QStringLiteral("strong_polish"), QStringLiteral("formal")},
            {WritingProfile::Email, QStringLiteral("balanced"), QStringLiteral("casual")},
            {WritingProfile::Personal, QStringLiteral("light_cleanup"), QStringLiteral("very_casual")},
            {WritingProfile::AiCoding, QStringLiteral("none"), QStringLiteral("casual")},
            {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
        });
        QCOMPARE(writingProfileSettingsFor(settings.writingProfileSettings(), WritingProfile::Work).cleanupStrength,
                 QStringLiteral("strong_polish"));
        QCOMPARE(writingProfileSettingsFor(settings.writingProfileSettings(), WritingProfile::Personal).tone,
                 QStringLiteral("very_casual"));
        QCOMPARE(writingProfileSettingsFor(settings.writingProfileSettings(), WritingProfile::AiCoding).cleanupStrength,
                 QStringLiteral("none"));
        settings.setWritingProfileOverrides({
            {QStringLiteral("org.mozilla.firefox"), WritingProfile::Personal, true},
            {QStringLiteral("org.kde.kate"), WritingProfile::Other, false},
        });
        QCOMPARE(settings.writingProfileOverrides().size(), 2);
        QCOMPARE(settings.writingProfileOverrides().first().profile, WritingProfile::Personal);
        settings.setAppRecognitionRules({
            {QStringLiteral("com.acme.shell"), AppCategory::Terminal, WritingProfile::Work},
            {QStringLiteral("chat.example"), std::nullopt, WritingProfile::Personal},
        });
        QCOMPARE(settings.appRecognitionRules().size(), 2);
        QCOMPARE(settings.appRecognitionRules().first().category, AppCategory::Terminal);
        QCOMPARE(settings.appRecognitionRules().last().writingProfile, WritingProfile::Personal);
        QCOMPARE(settings.snapshot().appRecognitionRules.size(), 2);
        settings.setUseTargetContext(false);
        QCOMPARE(settings.useTargetContext(), false);
        settings.setIncludeScreenshotContext(true);
        QCOMPARE(settings.includeScreenshotContext(), true);
        settings.setSoundsEnabled(true);
        QCOMPARE(settings.soundsEnabled(), true);
        settings.setSoundsEnabled(false);
        QCOMPARE(settings.soundsEnabled(), false);

        settings.setOpenAiModel(QStringLiteral(" gpt-5.4-nano "));
        QCOMPARE(settings.openAiModel(), QStringLiteral("gpt-5.4-nano"));
        settings.setOpenAiModel(QString());
        QCOMPARE(settings.openAiModel(), QStringLiteral("gpt-6-luna"));

        settings.raw().setValue(QStringLiteral("openai/auth/mode"), QStringLiteral("api_key_env"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("env"));
        settings.raw().setValue(QStringLiteral("openai/auth/mode"), QStringLiteral("api_key_settings"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("settings"));
        settings.raw().setValue(QStringLiteral("openai/auth/mode"), QStringLiteral("codex_then_api_key"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("auto"));
        settings.raw().setValue(QStringLiteral("openai/auth/mode"), QStringLiteral("codex_oauth"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("codex_oauth"));
        settings.setOpenAiAuthMode(QStringLiteral("codex_oauth"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("codex_oauth"));
        settings.setOpenAiAuthMode(QStringLiteral("cliproxy"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("cliproxy"));
        settings.setOpenAiAuthMode(QStringLiteral("env"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("env"));
        settings.setOpenAiEffort(QStringLiteral("xhigh"));
        QCOMPARE(settings.openAiEffort(), QStringLiteral("xhigh"));
        settings.setOpenAiEffort(QStringLiteral("none"));
        QCOMPARE(settings.openAiEffort(), QStringLiteral("none"));
        settings.raw().setValue(QStringLiteral("openai/effort"), QStringLiteral("minimal"));
        QCOMPARE(settings.openAiEffort(), QStringLiteral("none"));
        settings.setOpenAiEffort(QStringLiteral("unsupported"));
        QCOMPARE(settings.openAiEffort(), QStringLiteral("none"));
        // Fast mode turned off before Speed existed reads as Standard.
        settings.raw().setValue(QStringLiteral("openai/fastMode"), false);
        QCOMPARE(settings.openAiSpeed(), QStringLiteral("standard"));
        settings.setOpenAiSpeed(QStringLiteral("ultrafast"));
        QCOMPARE(settings.openAiSpeed(), QStringLiteral("ultrafast"));
        settings.setOpenAiSpeed(QStringLiteral("unsupported"));
        QCOMPARE(settings.openAiSpeed(), QStringLiteral("fast"));

        settings.setRefinementProvider(QStringLiteral("anthropic"));
        QCOMPARE(settings.refinementProvider(), QStringLiteral("anthropic"));
        settings.setRefinementProvider(QStringLiteral("unknown"));
        QCOMPARE(settings.refinementProvider(), QStringLiteral("openai"));

        settings.setAnthropicModel(QStringLiteral(" claude-opus-5 "));
        QCOMPARE(settings.anthropicModel(), QStringLiteral("claude-opus-5"));
        settings.setAnthropicModel(QString());
        QCOMPARE(settings.anthropicModel(), QStringLiteral("claude-opus-5-5"));
        settings.setAnthropicModel(QStringLiteral("claude-haiku-4-5-20251001"));
        QCOMPARE(settings.anthropicModel(), QStringLiteral("claude-haiku-4-5"));
        settings.setAnthropicAuthMode(QStringLiteral("oauth"));
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("oauth"));
        settings.setAnthropicAuthMode(QStringLiteral("cliproxy"));
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("cliproxy"));
        settings.setAnthropicAuthMode(QStringLiteral("unknown"));
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("oauth"));
        settings.setOpenAiCliproxyAccount(QStringLiteral(" codex-user@example.com.json "));
        QCOMPARE(settings.openAiCliproxyAccount(), QStringLiteral("codex-user@example.com.json"));
        settings.setAnthropicCliproxyAccount(QStringLiteral("claude-user@example.com.json"));
        QCOMPARE(settings.anthropicCliproxyAccount(), QStringLiteral("claude-user@example.com.json"));
        QCOMPARE(settings.snapshot().refinement.openAiCliproxyAccount, QStringLiteral("codex-user@example.com.json"));
        QCOMPARE(settings.snapshot().refinement.anthropicCliproxyAccount, QStringLiteral("claude-user@example.com.json"));
        // The default is autodetected from the machine: whichever CLI Proxy API
        // auth dir holds accounts, or the stock ~/.cli-proxy-api on a machine
        // with neither.
        const QString oauthDir = settings.snapshot().refinement.cliproxyOauthDir;
        QVERIFY(oauthDir.endsWith(QStringLiteral("cliproxy-api/oauth"))
                || oauthDir.endsWith(QStringLiteral(".cli-proxy-api")));
        settings.setAnthropicEffort(QStringLiteral("high"));
        QCOMPARE(settings.anthropicEffort(), QStringLiteral("high"));
        settings.setAnthropicEffort(QStringLiteral("max"));
        QCOMPARE(settings.anthropicEffort(), QStringLiteral("max"));
        settings.setAnthropicEffort(QStringLiteral("none"));
        QCOMPARE(settings.anthropicEffort(), QStringLiteral("low"));
        settings.setAnthropicFastMode(false);
        QCOMPARE(settings.anthropicFastMode(), false);
        settings.setAnthropicFastMode(true);
        QCOMPARE(settings.anthropicFastMode(), true);

        settings.setPauseMediaDuringTranscription(true);
        QCOMPARE(settings.pauseMediaDuringTranscription(), true);
        settings.setPauseMediaDuringTranscription(false);
        QCOMPARE(settings.pauseMediaDuringTranscription(), false);

        settings.setOutputMethod(QString::fromLatin1(OutputMethod::DirectInsert));
        QCOMPARE(settings.outputMethod(), QString::fromLatin1(OutputMethod::DirectInsert));
        settings.setOutputMethod(QStringLiteral("ydotool"));
        QCOMPARE(settings.outputMethod(), QString::fromLatin1(OutputMethod::Ydotool));
        settings.setOutputMethod(QStringLiteral("wtype"));
        QCOMPARE(settings.outputMethod(), QString::fromLatin1(OutputMethod::Automatic));
        settings.setOutputMethod(QStringLiteral("unknown"));
        QCOMPARE(settings.outputMethod(), QString::fromLatin1(OutputMethod::Automatic));
        settings.setOutputMethod(QStringLiteral("clipboard"));
        QCOMPARE(settings.outputMethod(), QString::fromLatin1(OutputMethod::WlCopy));
        settings.setYdotoolEnabled(true);
        QCOMPARE(settings.ydotoolEnabled(), true);
        settings.setOutputMethod(QString::fromLatin1(OutputMethod::Ydotool));
        settings.setYdotoolEnabled(false);
        QCOMPARE(settings.outputMethod(), QString::fromLatin1(OutputMethod::Automatic));
        settings.setRestoreClipboardAfterTyping(true);
        QCOMPARE(settings.restoreClipboardAfterTyping(), true);
        settings.setRestoreClipboardAfterTyping(false);
        QCOMPARE(settings.restoreClipboardAfterTyping(), false);
        settings.setOutputFormat(OutputFormat::Html);
        QCOMPARE(settings.outputFormat(), OutputFormat::Html);
        settings.raw().setValue(QStringLiteral("output/format"), QStringLiteral("unsupported"));
        QCOMPARE(settings.outputFormat(), OutputFormat::PlainText);
        settings.setPasteRules({
            {PasteRuleScope::Application, QStringLiteral("org.kde.kate"), PasteMethod::DirectInsert, true},
            {PasteRuleScope::Global, QString(), PasteMethod::StandardPaste, true},
        });
        QCOMPARE(settings.pasteRules().size(), 2);
        QCOMPARE(settings.pasteRules().first().match, QStringLiteral("org.kde.kate"));
        QCOMPARE(settings.pasteRules().first().method, PasteMethod::DirectInsert);

        settings.setAudioCaptureSettings({
            QStringLiteral(" mic-id "),
            QStringLiteral("always_open"),
            true,
            5000,
            -20,
            50,
            99,
        });
        QCOMPARE(settings.audioInputDeviceId(), QStringLiteral("mic-id"));
        QCOMPARE(settings.audioCaptureMode(), QStringLiteral("warm"));
        QCOMPARE(settings.audioVadEnabled(), true);
        QCOMPARE(settings.audioPreRollMs(), 1500);
        QCOMPARE(settings.audioPostRollMs(), 0);
        QCOMPARE(settings.audioReadinessTimeoutMs(), 500);
        QCOMPARE(settings.audioVadThresholdPercent(), 20);

        settings.raw().setValue(SettingsKeys::ShortcutActivationMode, QStringLiteral("hold"));
        QCOMPARE(settings.shortcutActivationMode(), ShortcutActivationMode::Hybrid);
        settings.setShortcutActivationMode(ShortcutActivationMode::PushToTalk);
        QCOMPARE(settings.shortcutActivationMode(), ShortcutActivationMode::PushToTalk);
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

    void localSpeechSettingsRoundTrip()
    {
        SettingsStore settings;
        settings.raw().clear();
        QCOMPARE(settings.snapshot().speech.local, LocalSpeechSettings{});
        QCOMPARE(settings.snapshot().speech.local.modelId, QStringLiteral("parakeet"));
        QCOMPARE(settings.snapshot().speech.local.idleUnloadMinutes, 10);

        AppSettings draft = settings.snapshot();
        draft.speech.local.modelId = QStringLiteral("cohere");
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
        QCOMPARE(settings.localSpeechSettings().speedTestSeconds.value(
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
        QCOMPARE(settings.refinementProvider(), QStringLiteral("anthropic"));

        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "1");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "1");
        QCOMPARE(settings.refinementProvider(), QStringLiteral("openai"));

        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "1");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "0");
        QCOMPARE(settings.refinementProvider(), QStringLiteral("openai"));

        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "0");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "0");
        QCOMPARE(settings.refinementProvider(), QStringLiteral("openai"));

        settings.setRefinementProvider(QStringLiteral("anthropic"));
        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "1");
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "1");
        QCOMPARE(settings.refinementProvider(), QStringLiteral("anthropic"));
    }

    void settingsSnapshotApplyPersistsCliProxyAccounts()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.refinement.openAiCliproxyAccount = QStringLiteral("codex-user@example.com.json");
        draft.refinement.anthropicCliproxyAccount = QStringLiteral("claude-user@example.com.json");

        settings.applySnapshot(draft);

        QCOMPARE(settings.openAiCliproxyAccount(), QStringLiteral("codex-user@example.com.json"));
        QCOMPARE(settings.anthropicCliproxyAccount(), QStringLiteral("claude-user@example.com.json"));
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
        for (const WritingProfileSettings &profile : settings.writingProfileSettings()) {
            ids << profile.profile;
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("work"), QStringLiteral("email"), QStringLiteral("personal"),
                                   QStringLiteral("ai_coding"), QStringLiteral("other")}));
        settings.applySnapshot(settings.snapshot());
        QCOMPARE(settings.raw().value(QStringLiteral("refinement/writingProfiles")).toByteArray(), stored);
        settings.raw().clear();
    }

    // A rule keeps a custom profile while it exists. Deleting the profile
    // clears it from a rule that also sets an app type, drops a rule that set
    // nothing else, and turns a fallback that named it into Other.
    void deletingAProfileClearsItsRulesAndTheFallback()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.refinement.writingProfiles.append(
            {QStringLiteral("custom_standup"), QStringLiteral("light_cleanup"), QStringLiteral("none"), QString(),
             QStringLiteral("Standup")});
        draft.refinement.defaultWritingProfile = QStringLiteral("custom_standup");
        draft.appRecognitionRules = {
            {QStringLiteral("zulip"), std::nullopt, QStringLiteral("custom_standup")},
            {QStringLiteral("mattermost"), AppCategory::Browser, QStringLiteral("custom_standup")},
        };
        settings.applySnapshot(draft);
        AppSettings loaded = SettingsStore().snapshot();
        QCOMPARE(loaded.appRecognitionRules, draft.appRecognitionRules);
        QCOMPARE(loaded.refinement.defaultWritingProfile, QStringLiteral("custom_standup"));

        loaded.refinement.writingProfiles.removeLast();
        settings.applySnapshot(loaded);
        const AppSettings deleted = SettingsStore().snapshot();
        QCOMPARE(deleted.appRecognitionRules,
                 (QList<AppRecognitionRule>{{QStringLiteral("mattermost"), AppCategory::Browser, std::nullopt}}));
        QCOMPARE(deleted.refinement.defaultWritingProfile, QStringLiteral("other"));
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
        QCOMPARE(settings.configuredCliproxyOauthDir(), QString());

        AppSettings draft = settings.snapshot();
        draft.refinement.cliproxyOauthDirConfigured = QStringLiteral("/custom/cliproxy");
        settings.applySnapshot(draft);
        QCOMPARE(settings.configuredCliproxyOauthDir(), QStringLiteral("/custom/cliproxy"));
        QCOMPARE(settings.cliproxyOauthDir(), QStringLiteral("/custom/cliproxy"));

        // Clearing the field returns to automatic detection.
        draft = settings.snapshot();
        draft.refinement.cliproxyOauthDirConfigured.clear();
        settings.applySnapshot(draft);
        QCOMPARE(settings.configuredCliproxyOauthDir(), QString());
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

        QCOMPARE(settings.launchAtLogin(), draft.launchAtLogin);
        QCOMPARE(settings.snapshot().launchAtLogin, draft.launchAtLogin);
        QCOMPARE(reconciled, std::optional<bool>(draft.launchAtLogin));
    }

    void settingsBindingRulesRoundTrip()
    {
        SettingsStore settings;
        settings.raw().clear();

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
        QVERIFY(settings.correctionLearningEnabled());

        const CorrectionEvidence medium{QStringLiteral("cute"), QStringLiteral("Qt"), 0.75};
        QVERIFY(!settings.recordCorrectionEvidence(medium, QStringLiteral("org.kde.kate")));
        QVERIFY(settings.learnedCorrections().isEmpty());
        QVERIFY(settings.snapshot().learnedCorrections.isEmpty());

        QVERIFY(settings.recordCorrectionEvidence(medium, QStringLiteral("org.kde.kate")));

        QList<LearnedCorrection> corrections = settings.learnedCorrections();
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

        const QString id = corrections.first().id;
        settings.setLearnedCorrectionEnabled(id, false);
        snapshot = settings.snapshot();
        QVERIFY(!snapshot.speech.vocabulary.contains(QStringLiteral("Qt")));

        settings.setLearnedCorrectionEnabled(id, true);
        QVERIFY(settings.recordCorrectionEvidence(medium, QStringLiteral("org.kde.kate")));
        corrections = settings.learnedCorrections();
        QCOMPARE(corrections.size(), 1);
        QCOMPARE(corrections.first().corrected, QStringLiteral("Qt"));
        QCOMPARE(corrections.first().evidenceCount, 3);

        settings.removeLearnedCorrection(id);
        QVERIFY(settings.learnedCorrections().isEmpty());
    }

    void correctionLearningSettingChangesAreObservableWithoutRestart()
    {
        SettingsStore settings;
        settings.raw().clear();
        QSignalSpy changed(&settings, &SettingsStore::correctionLearningEnabledChanged);

        settings.setCorrectionLearningEnabled(false);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.takeFirst().first().toBool(), false);

        settings.setCorrectionLearningEnabled(false);
        QCOMPARE(changed.count(), 0);

        settings.setCorrectionLearningEnabled(true);
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

        const QList<LearnedCorrection> corrections = settings.learnedCorrections();
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

        settings.setCorrectionLearningEnabled(false);
        QVERIFY(!settings.recordCorrectionEvidence(evidence, QStringLiteral("org.kde.kate")));
        QVERIFY(settings.learnedCorrections().isEmpty());

        settings.setCorrectionLearningEnabled(true);
        QVERIFY(settings.recordCorrectionEvidence(evidence, QStringLiteral("org.kde.kate")));
        QVERIFY(settings.recordCorrectionEvidence(evidence, QStringLiteral("org.mozilla.firefox")));

        const QList<LearnedCorrection> corrections = settings.learnedCorrections();
        QCOMPARE(corrections.size(), 1);
        QVERIFY(corrections.first().applicationId.isEmpty());
        QCOMPARE(corrections.first().evidenceCount, 2);
        QVERIFY(corrections.first().lastObservedAtMs >= corrections.first().createdAtMs);

        settings.raw().clear();
        const CorrectionEvidence medium{
            QStringLiteral("cute"), QStringLiteral("Qt"), 0.75};
        QVERIFY(!settings.recordCorrectionEvidence(medium, QStringLiteral("org.kde.kate")));
        QVERIFY(settings.recordCorrectionEvidence(medium, QStringLiteral("org.mozilla.firefox")));
        QCOMPARE(settings.learnedCorrections().size(), 1);
        QVERIFY(settings.learnedCorrections().first().applicationId.isEmpty());
    }

    void lowConfidenceEvidenceIsDiscarded()
    {
        SettingsStore settings;
        settings.raw().clear();

        QVERIFY(!settings.recordCorrectionEvidence(
            {QStringLiteral("cute"), QStringLiteral("Qt"), 0.4},
            QStringLiteral("org.kde.kate")));
        QVERIFY(settings.learnedCorrections().isEmpty());
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

    void settingsSnapshot()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setPreviewWords(12);
        settings.setTheme(QStringLiteral("dark"));
        settings.setPauseMediaDuringTranscription(false);
        settings.setSoundsEnabled(true);
        settings.setSpeechProvider(QStringLiteral("claude"));
        settings.setCustomVocabulary({QStringLiteral("Deepgram Nova 3"), QStringLiteral("Speecher")});
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setRefinementStyle(QStringLiteral("strong_polish"));
        settings.setDefaultWritingProfile(QStringLiteral("personal"));
        settings.setWritingProfileSettings({
            {WritingProfile::Work, QStringLiteral("strong_polish"), QStringLiteral("formal")},
            {WritingProfile::Email, QStringLiteral("balanced"), QStringLiteral("none")},
            {WritingProfile::Personal, QStringLiteral("light_cleanup"), QStringLiteral("casual")},
            {WritingProfile::AiCoding, QStringLiteral("none"), QStringLiteral("formal")},
            {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
        });
        settings.setWritingProfileOverrides({
            {QStringLiteral("org.mozilla.firefox"), WritingProfile::Personal, true},
        });
        settings.setUseTargetContext(false);
        settings.setIncludeScreenshotContext(true);
        settings.setOpenAiAuthMode(QStringLiteral("env"));
        settings.setOpenAiEffort(QStringLiteral("high"));
        settings.setOpenAiSpeed(QStringLiteral("standard"));
        settings.setAnthropicModel(QStringLiteral("claude-opus-5"));
        settings.setAnthropicAuthMode(QStringLiteral("oauth"));
        settings.setAnthropicEffort(QStringLiteral("xhigh"));
        settings.setAnthropicFastMode(false);
        settings.setOutputFormat(OutputFormat::Html);
        settings.setPasteRules({
            {PasteRuleScope::Category, QStringLiteral("terminal"), PasteMethod::TerminalPaste, true},
            {PasteRuleScope::Global, QString(), PasteMethod::ClipboardOnly, true},
        });
        settings.setRestoreClipboardAfterTyping(true);
        settings.setCompletionStatusDurationMs(900);
        settings.setAudioCaptureSettings({
            QStringLiteral("device-1"),
            QStringLiteral("warm"),
            true,
            300,
            250,
            700,
            4,
        });

        const AppSettings snapshot = settings.snapshot();
        QCOMPARE(snapshot.ui.previewWords, 12);
        QCOMPARE(snapshot.ui.theme, QStringLiteral("dark"));
        QCOMPARE(snapshot.ui.pauseMediaDuringTranscription, false);
        QCOMPARE(snapshot.ui.soundsEnabled, true);
        QCOMPARE(snapshot.speech.providerId, QStringLiteral("claude"));
        QCOMPARE(snapshot.speech.vocabulary.size(), 2);
        QCOMPARE(snapshot.audio.deviceId, QStringLiteral("device-1"));
        QCOMPARE(snapshot.audio.mode, QStringLiteral("warm"));
        QCOMPARE(snapshot.audio.vadEnabled, true);
        QCOMPARE(snapshot.audio.preRollMs, 300);
        QCOMPARE(snapshot.audio.postRollMs, 250);
        QCOMPARE(snapshot.audio.readinessTimeoutMs, 700);

        // Values stored by older builds at the old 150 ms floor load at the new floor.
        settings.setAudioCaptureSettings({QStringLiteral("device-1"), QStringLiteral("warm"), true, 300, 250, 150, 4});
        QCOMPARE(settings.snapshot().audio.readinessTimeoutMs, 500);
        QCOMPARE(snapshot.audio.vadThresholdPercent, 4);
        QCOMPARE(snapshot.bindings.size(), 1);
        QCOMPARE(snapshot.bindings.at(0).replacement, QStringLiteral("efox@example.com"));
        QCOMPARE(snapshot.refinement.providerId, QStringLiteral("openai"));
        QCOMPARE(snapshot.refinement.style, QStringLiteral("strong_polish"));
        QCOMPARE(snapshot.refinement.defaultWritingProfile, QStringLiteral("personal"));
        QCOMPARE(snapshot.refinement.writingProfileOverrides.size(), 1);
        QCOMPARE(snapshot.refinement.writingProfileOverrides.first().applicationId,
                 QStringLiteral("org.mozilla.firefox"));
        QCOMPARE(writingProfileSettingsFor(snapshot.refinement.writingProfiles, WritingProfile::Work).tone,
                 QStringLiteral("formal"));
        QCOMPARE(writingProfileSettingsFor(snapshot.refinement.writingProfiles, WritingProfile::AiCoding).tone,
                 QStringLiteral("formal"));
        QCOMPARE(snapshot.refinement.useTargetContext, false);
        QCOMPARE(snapshot.refinement.includeScreenshotContext, true);
        QCOMPARE(snapshot.refinement.openAiAuthMode, QStringLiteral("env"));
        QCOMPARE(snapshot.refinement.openAiEffort, QStringLiteral("high"));
        QCOMPARE(snapshot.refinement.openAiSpeed, QStringLiteral("standard"));
        QCOMPARE(snapshot.refinement.anthropicModel, QStringLiteral("claude-opus-5"));
        QCOMPARE(snapshot.refinement.anthropicAuthMode, QStringLiteral("oauth"));
        QCOMPARE(snapshot.refinement.anthropicEffort, QStringLiteral("xhigh"));
        QCOMPARE(snapshot.refinement.anthropicFastMode, false);
        QVERIFY(snapshot.refinement.claudeCredentialsPath.endsWith(QStringLiteral("/.claude/.credentials.json")));
        QCOMPARE(snapshot.output.method, QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(snapshot.output.format, OutputFormat::Html);
        QCOMPARE(snapshot.output.ydotoolEnabled, false);
        QCOMPARE(snapshot.output.restoreClipboardAfterTyping, true);
        QCOMPARE(snapshot.output.completionStatusDurationMs, 900);
        QCOMPARE(snapshot.output.pasteRules.size(), 2);
        QCOMPARE(snapshot.output.pasteRules.last().method, PasteMethod::ClipboardOnly);
    }
};

int runSettingsTests(int argc, char **argv)
{
    SettingsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_settings.moc"
