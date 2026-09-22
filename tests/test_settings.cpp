#include "common/test_prelude.h"
#include "core/settings/SettingsKeys.h"
#ifdef SPEECHER_WITH_QKEYCHAIN
#include "core/KeyringResult.h"
#include "core/ShortcutBinding.h"

#include <QSet>
#endif

using namespace speecher;


class SettingsTests : public QObject {
    Q_OBJECT

private slots:
    void settingsRespectConfiguredStorageFormat()
    {
        const auto previous = QSettings::defaultFormat();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        SettingsStore settings;
        const auto actual = settings.raw().format();
        QSettings::setDefaultFormat(previous);
        QCOMPARE(actual, QSettings::IniFormat);
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
        QCOMPARE(settings.openAiFastMode(), true);
        QCOMPARE(settings.anthropicModel(), QStringLiteral("claude-opus-5-5"));
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("oauth"));
        QCOMPARE(settings.anthropicEffort(), QStringLiteral("low"));
        QCOMPARE(settings.anthropicFastMode(), true);
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
        settings.setOpenAiFastMode(false);
        QCOMPARE(settings.openAiFastMode(), false);
        settings.setOpenAiFastMode(true);
        QCOMPARE(settings.openAiFastMode(), true);

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
        settings.setOpenAiFastMode(false);
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
        QCOMPARE(snapshot.refinement.openAiFastMode, false);
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
