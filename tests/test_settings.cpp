#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;


class SettingsTests : public QObject {
    Q_OBJECT

private slots:
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
        QCOMPARE(settings.previewWords(), 7);
        QCOMPARE(settings.theme(), QStringLiteral("system"));
        QCOMPARE(settings.pauseMediaDuringTranscription(), true);
        QCOMPARE(settings.soundsEnabled(), false);
        QCOMPARE(settings.customVocabulary(), QStringList());
        QCOMPARE(settings.bindingRules().size(), 0);
        QCOMPARE(settings.refinementProvider(), QStringLiteral("openai"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("balanced"));
        QCOMPARE(settings.defaultWritingProfile(), QStringLiteral("other"));
        QCOMPARE(settings.writingProfileSettings(), defaultWritingProfileSettings());
        QVERIFY(settings.writingProfileOverrides().isEmpty());
        QCOMPARE(settings.useTargetContext(), true);
        QCOMPARE(settings.includeScreenshotContext(), false);
        QCOMPARE(settings.openAiModel(), QStringLiteral("gpt-5.6-luna"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("auto"));
        QCOMPARE(settings.openAiEffort(), QStringLiteral("none"));
        QCOMPARE(settings.anthropicModel(), QStringLiteral("claude-sonnet-4-6"));
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("oauth"));
        QCOMPARE(settings.anthropicEffort(), QStringLiteral("low"));
        QCOMPARE(settings.outputMethod(), QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(settings.outputFormat(), OutputFormat::PlainText);
        QCOMPARE(settings.pasteRules(), defaultPasteRules());
        QCOMPARE(settings.ydotoolEnabled(), false);
        QCOMPARE(settings.restoreClipboardAfterTyping(), false);
        QCOMPARE(settings.audioInputDeviceId(), QString());
        QCOMPARE(settings.audioCaptureMode(), QStringLiteral("on_demand"));
        QCOMPARE(settings.audioVadEnabled(), false);
        QCOMPARE(settings.audioPreRollMs(), 250);
        QCOMPARE(settings.audioPostRollMs(), 200);
        QCOMPARE(settings.audioReadinessTimeoutMs(), 900);
        QCOMPARE(settings.audioVadThresholdPercent(), 2);

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
        settings.setWritingProfileSettings({
            {WritingProfile::Work, QStringLiteral("strong_polish"), QStringLiteral("formal")},
            {WritingProfile::Email, QStringLiteral("balanced"), QStringLiteral("casual")},
            {WritingProfile::Personal, QStringLiteral("light_cleanup"), QStringLiteral("very_casual")},
            {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
        });
        QCOMPARE(writingProfileSettingsFor(settings.writingProfileSettings(), WritingProfile::Work).cleanupStrength,
                 QStringLiteral("strong_polish"));
        QCOMPARE(writingProfileSettingsFor(settings.writingProfileSettings(), WritingProfile::Personal).tone,
                 QStringLiteral("very_casual"));
        settings.setWritingProfileOverrides({
            {QStringLiteral("org.mozilla.firefox"), WritingProfile::Personal, true},
            {QStringLiteral("org.kde.kate"), WritingProfile::Other, false},
        });
        QCOMPARE(settings.writingProfileOverrides().size(), 2);
        QCOMPARE(settings.writingProfileOverrides().first().profile, WritingProfile::Personal);
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
        QCOMPARE(settings.openAiModel(), QStringLiteral("gpt-5.6-luna"));

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

        settings.setRefinementProvider(QStringLiteral("anthropic"));
        QCOMPARE(settings.refinementProvider(), QStringLiteral("anthropic"));
        settings.setRefinementProvider(QStringLiteral("unknown"));
        QCOMPARE(settings.refinementProvider(), QStringLiteral("openai"));

        settings.setAnthropicModel(QStringLiteral(" claude-opus-4-8 "));
        QCOMPARE(settings.anthropicModel(), QStringLiteral("claude-opus-4-8"));
        settings.setAnthropicModel(QString());
        QCOMPARE(settings.anthropicModel(), QStringLiteral("claude-sonnet-4-6"));
        settings.setAnthropicAuthMode(QStringLiteral("oauth"));
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("oauth"));
        settings.setAnthropicAuthMode(QStringLiteral("unknown"));
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("oauth"));
        settings.setAnthropicEffort(QStringLiteral("high"));
        QCOMPARE(settings.anthropicEffort(), QStringLiteral("high"));
        settings.setAnthropicEffort(QStringLiteral("max"));
        QCOMPARE(settings.anthropicEffort(), QStringLiteral("max"));
        settings.setAnthropicEffort(QStringLiteral("none"));
        QCOMPARE(settings.anthropicEffort(), QStringLiteral("low"));

        settings.setPauseMediaDuringTranscription(true);
        QCOMPARE(settings.pauseMediaDuringTranscription(), true);
        settings.setPauseMediaDuringTranscription(false);
        QCOMPARE(settings.pauseMediaDuringTranscription(), false);

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
        QCOMPARE(settings.audioReadinessTimeoutMs(), 150);
        QCOMPARE(settings.audioVadThresholdPercent(), 20);
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

    void learnedCorrectionsPersistLocallyAndFeedSessionVocabulary()
    {
        SettingsStore settings;
        settings.raw().clear();
        QCOMPARE(settings.correctionLearningEnabled(), false);
        settings.setCorrectionLearningEnabled(true);
        QVERIFY(settings.correctionLearningEnabled());

        QVERIFY(!settings.addLearnedCorrection({}, QStringLiteral("Qt"), {}, 0.9));
        QVERIFY(!settings.addLearnedCorrection(QStringLiteral("cute"), QStringLiteral("cute"), {}, 0.9));
        QVERIFY(settings.addLearnedCorrection(
            QStringLiteral("cute"),
            QStringLiteral("Qt"),
            QStringLiteral("org.kde.kate"),
            0.94));

        QList<LearnedCorrection> corrections = settings.learnedCorrections();
        QCOMPARE(corrections.size(), 1);
        QCOMPARE(corrections.first().original, QStringLiteral("cute"));
        QCOMPARE(corrections.first().corrected, QStringLiteral("Qt"));
        QCOMPARE(corrections.first().applicationId, QStringLiteral("org.kde.kate"));
        QCOMPARE(corrections.first().confidence, 0.94);
        QVERIFY(corrections.first().enabled);

        AppSettings snapshot = settings.snapshot();
        QVERIFY(snapshot.speech.vocabulary.contains(QStringLiteral("Qt")));
        QVERIFY(snapshot.bindings.contains(BindingRule{QStringLiteral("cute"), QStringLiteral("Qt")}));

        const QString id = corrections.first().id;
        settings.setLearnedCorrectionEnabled(id, false);
        snapshot = settings.snapshot();
        QVERIFY(!snapshot.speech.vocabulary.contains(QStringLiteral("Qt")));
        QVERIFY(!snapshot.bindings.contains(BindingRule{QStringLiteral("cute"), QStringLiteral("Qt")}));

        settings.setLearnedCorrectionEnabled(id, true);
        QVERIFY(settings.addLearnedCorrection(
            QStringLiteral("CUTE"),
            QStringLiteral("Qt 6"),
            QStringLiteral("org.kde.kate"),
            0.99));
        corrections = settings.learnedCorrections();
        QCOMPARE(corrections.size(), 1);
        QCOMPARE(corrections.first().corrected, QStringLiteral("Qt 6"));

        settings.removeLearnedCorrection(id);
        QVERIFY(settings.learnedCorrections().isEmpty());
    }

    void learnedCorrectionRequiresUniqueStableAnchors()
    {
        const std::optional<QString> correction = correctionBetweenAnchors(
            QStringLiteral("before text Qt 6 after text"),
            QStringLiteral("before text "),
            QStringLiteral(" after text"),
            QStringLiteral("cute"));
        QVERIFY(correction);
        QCOMPARE(*correction, QStringLiteral("Qt 6"));
        QVERIFY(!correctionBetweenAnchors(
            QStringLiteral("short Qt short"),
            QStringLiteral("short "),
            QStringLiteral(" short"),
            QStringLiteral("cute")));
        QVERIFY(!correctionBetweenAnchors(
            QStringLiteral("before text Qt after text before text duplicate after text"),
            QStringLiteral("before text "),
            QStringLiteral(" after text"),
            QStringLiteral("cute")));
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
            {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
        });
        settings.setWritingProfileOverrides({
            {QStringLiteral("org.mozilla.firefox"), WritingProfile::Personal, true},
        });
        settings.setUseTargetContext(false);
        settings.setIncludeScreenshotContext(true);
        settings.setOpenAiAuthMode(QStringLiteral("env"));
        settings.setOpenAiEffort(QStringLiteral("high"));
        settings.setAnthropicModel(QStringLiteral("claude-opus-4-8"));
        settings.setAnthropicAuthMode(QStringLiteral("oauth"));
        settings.setAnthropicEffort(QStringLiteral("xhigh"));
        settings.setOutputFormat(OutputFormat::Html);
        settings.setPasteRules({
            {PasteRuleScope::Category, QStringLiteral("terminal"), PasteMethod::TerminalPaste, true},
            {PasteRuleScope::Global, QString(), PasteMethod::ClipboardOnly, true},
        });
        settings.setRestoreClipboardAfterTyping(true);
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
        QCOMPARE(snapshot.refinement.useTargetContext, false);
        QCOMPARE(snapshot.refinement.includeScreenshotContext, true);
        QCOMPARE(snapshot.refinement.openAiAuthMode, QStringLiteral("env"));
        QCOMPARE(snapshot.refinement.openAiEffort, QStringLiteral("high"));
        QCOMPARE(snapshot.refinement.anthropicModel, QStringLiteral("claude-opus-4-8"));
        QCOMPARE(snapshot.refinement.anthropicAuthMode, QStringLiteral("oauth"));
        QCOMPARE(snapshot.refinement.anthropicEffort, QStringLiteral("xhigh"));
        QVERIFY(snapshot.refinement.claudeCredentialsPath.endsWith(QStringLiteral("/.claude/.credentials.json")));
        QCOMPARE(snapshot.output.method, QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(snapshot.output.format, OutputFormat::Html);
        QCOMPARE(snapshot.output.ydotoolEnabled, false);
        QCOMPARE(snapshot.output.restoreClipboardAfterTyping, true);
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
