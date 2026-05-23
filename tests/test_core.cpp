#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/VocabularyLimit.h"
#include "core/WordPreview.h"
#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeVoiceClient.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/OpenAiRefiner.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace speecher;

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void wordPreview()
    {
        QCOMPARE(WordPreview::lastWords(QStringLiteral(" one  two, three\nfour "), 2), QStringLiteral("three four"));
        QCOMPARE(WordPreview::lastWords(QStringLiteral("short"), 8), QStringLiteral("short"));
        QCOMPARE(WordPreview::lastWords(QString(), 8), QString());
        QCOMPARE(WordPreview::lastWords(QStringLiteral("alpha beta gamma"), 1), QStringLiteral("gamma"));
        QCOMPARE(WordPreview::lastWords(QStringLiteral("alpha beta gamma"), 0), QString());
    }

    void transcriptStateMerges()
    {
        TranscriptState state;
        state.setPartial(QStringLiteral("hello"));
        QCOMPARE(state.text(), QStringLiteral("hello"));
        state.commitFinal(QStringLiteral("hello"));
        QCOMPARE(state.text(), QStringLiteral("hello"));
        state.setPartial(QStringLiteral("world"));
        QCOMPARE(state.text(), QStringLiteral("hello world"));
    }

    void settingsDefaults()
    {
        SettingsStore settings;
        settings.raw().clear();
        QCOMPARE(settings.previewWords(), 8);
        QCOMPARE(settings.theme(), QStringLiteral("system"));
        QCOMPARE(settings.pauseMediaDuringTranscription(), true);
        QCOMPARE(settings.customVocabulary(), QStringList());
        QCOMPARE(settings.refinementProvider(), QStringLiteral("openai"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("balanced"));
        QCOMPARE(settings.refinementOutputFormat(), QStringLiteral("plain_sentences"));
        QCOMPARE(settings.openAiModel(), QStringLiteral("gpt-5.4-mini"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("auto"));

        settings.setRefinementStyle(QStringLiteral("strong_polish"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("strong_polish"));
        settings.setRefinementStyle(QStringLiteral("balanced"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("balanced"));
        settings.setRefinementStyle(QStringLiteral("light_cleanup"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("light_cleanup"));
        settings.setRefinementStyle(QStringLiteral("unknown"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("balanced"));

        settings.setRefinementOutputFormat(QStringLiteral("markdown"));
        QCOMPARE(settings.refinementOutputFormat(), QStringLiteral("markdown"));
        settings.setRefinementOutputFormat(QStringLiteral("plain_sentences"));
        QCOMPARE(settings.refinementOutputFormat(), QStringLiteral("plain_sentences"));
        settings.setRefinementOutputFormat(QStringLiteral("unknown"));
        QCOMPARE(settings.refinementOutputFormat(), QStringLiteral("plain_sentences"));

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

        settings.setPauseMediaDuringTranscription(true);
        QCOMPARE(settings.pauseMediaDuringTranscription(), true);
        settings.setPauseMediaDuringTranscription(false);
        QCOMPARE(settings.pauseMediaDuringTranscription(), false);
    }

    void refinementInstructionsCompose()
    {
        const QString lightMarkdown = openAiRefinementInstructions(QStringLiteral("light_cleanup"), QStringLiteral("markdown"));
        QVERIFY(lightMarkdown.contains(QStringLiteral("Rule: return_only_refined_text.")));
        QVERIFY(lightMarkdown.contains(QStringLiteral("Rule: no_inferred_structure.")));
        QVERIFY(lightMarkdown.contains(QStringLiteral("Output format: markdown.")));
        QVERIFY(lightMarkdown.contains(QStringLiteral("Do not create structure that the selected refinement level would not otherwise allow.")));
        QVERIFY(lightMarkdown.contains(QStringLiteral("even Light may produce a bullet list when Markdown output is selected")));
        QVERIFY(!lightMarkdown.contains(QStringLiteral("Rule: infer_simple_structure.")));
        QVERIFY(!lightMarkdown.contains(QStringLiteral("Rule: useful_organization.")));

        const QString balancedPlain = openAiRefinementInstructions(QStringLiteral("balanced"), QStringLiteral("plain_sentences"));
        QVERIFY(balancedPlain.contains(QStringLiteral("Rule: no_inferred_structure.")));
        QVERIFY(balancedPlain.contains(QStringLiteral("Rule: infer_simple_structure.")));
        QVERIFY(balancedPlain.contains(QStringLiteral("Output format: plain_sentences.")));
        QVERIFY(balancedPlain.contains(QStringLiteral("Render any permitted structure as compact prose.")));
        QVERIFY(!balancedPlain.contains(QStringLiteral("Rule: useful_organization.")));

        const QString strongMarkdown = openAiRefinementInstructions(QStringLiteral("strong_polish"), QStringLiteral("markdown"));
        QVERIFY(strongMarkdown.contains(QStringLiteral("Rule: no_inferred_structure.")));
        QVERIFY(strongMarkdown.contains(QStringLiteral("Rule: infer_simple_structure.")));
        QVERIFY(strongMarkdown.contains(QStringLiteral("Rule: useful_organization.")));
        QVERIFY(strongMarkdown.contains(QStringLiteral("Rule: technical_literal_priority.")));
    }

    void vocabularyLimits()
    {
        QCOMPARE(VocabularyLimit::tokenCount(QStringLiteral("Deepgram Nova 3")), 3);
        QCOMPARE(VocabularyLimit::tokenCount(QStringList{QStringLiteral("Deepgram Nova 3"), QStringLiteral("API")}), 4);

        QStringList tooManyTerms;
        for (int i = 0; i < 105; ++i) {
            tooManyTerms << QStringLiteral("term%1").arg(i);
        }
        QCOMPARE(VocabularyLimit::limited(tooManyTerms).size(), VocabularyLimit::maxKeyterms);

        QStringList tooManyTokens;
        for (int i = 0; i < 101; ++i) {
            tooManyTokens << QStringLiteral("alpha%1 beta gamma delta epsilon").arg(i);
        }
        const QStringList limitedTokens = VocabularyLimit::limited(tooManyTokens);
        QCOMPARE(VocabularyLimit::tokenCount(limitedTokens), VocabularyLimit::maxTokens);
        QCOMPARE(limitedTokens.size(), VocabularyLimit::maxKeyterms);

        QStringList phrases;
        for (int i = 0; i < 90; ++i) {
            phrases << QStringLiteral("two token%1").arg(i);
        }
        phrases << QStringLiteral("this term has far too many tokens to fit inside the remaining keyterm budget");
        QVERIFY(VocabularyLimit::tokenCount(VocabularyLimit::limited(phrases)) <= VocabularyLimit::maxTokens);

        SettingsStore settings;
        settings.raw().clear();
        settings.setCustomVocabulary(tooManyTerms);
        QCOMPARE(settings.customVocabulary().size(), VocabularyLimit::maxKeyterms);
    }

    void claudeCredentialsParse()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("credentials.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QJsonObject oauth{
            {QStringLiteral("accessToken"), QStringLiteral("secret-token")},
            {QStringLiteral("refreshToken"), QStringLiteral("refresh-token")},
            {QStringLiteral("expiresAt"), double(QDateTime::currentDateTimeUtc().addDays(1).toSecsSinceEpoch())},
            {QStringLiteral("subscriptionType"), QStringLiteral("pro")},
            {QStringLiteral("rateLimitTier"), QStringLiteral("tier")},
        };
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("claudeAiOauth"), oauth}}).toJson());
        file.close();

        const ClaudeCredentialResult result = ClaudeCredentials::load(path);
        QVERIFY(result.ok);
        QCOMPARE(result.accessToken, QStringLiteral("secret-token"));
        QVERIFY(!result.error.contains(QStringLiteral("secret-token")));
    }

    void claudeCredentialsExpired()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("credentials.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QJsonObject oauth{
            {QStringLiteral("accessToken"), QStringLiteral("secret-token")},
            {QStringLiteral("expiresAt"), double(QDateTime::currentDateTimeUtc().addSecs(-60).toSecsSinceEpoch())},
        };
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("claudeAiOauth"), oauth}}).toJson());
        file.close();
        const ClaudeCredentialResult result = ClaudeCredentials::load(path);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("claude")));
    }

    void claudeInstalledVersion()
    {
        const QString version = ClaudeCredentials::installedVersion();
        if (!version.isEmpty()) {
            QVERIFY(QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+")).match(version).hasMatch());
        }
    }

    void claudeVoiceStreamQueryMatchesClaudeCode()
    {
        const QUrlQuery query = claudeVoiceStreamQuery(QStringList{
            QStringLiteral("Deepgram Nova 3"),
            QStringLiteral("Speecher"),
        });

        QCOMPARE(query.queryItemValue(QStringLiteral("encoding")), QStringLiteral("linear16"));
        QCOMPARE(query.queryItemValue(QStringLiteral("sample_rate")), QStringLiteral("16000"));
        QCOMPARE(query.queryItemValue(QStringLiteral("channels")), QStringLiteral("1"));
        QCOMPARE(query.queryItemValue(QStringLiteral("endpointing_ms")), QStringLiteral("300"));
        QCOMPARE(query.queryItemValue(QStringLiteral("utterance_end_ms")), QStringLiteral("1000"));
        QCOMPARE(query.queryItemValue(QStringLiteral("language")), QStringLiteral("en"));
        QCOMPARE(query.queryItemValue(QStringLiteral("use_conversation_engine")), QStringLiteral("true"));
        QCOMPARE(query.queryItemValue(QStringLiteral("forward_interims")), QStringLiteral("typed"));
        QCOMPARE(query.queryItemValue(QStringLiteral("stt_provider")), QStringLiteral("deepgram-nova3"));
        QCOMPARE(query.allQueryItemValues(QStringLiteral("keyterms")).size(), 2);
    }
};

QTEST_MAIN(CoreTests)
#include "test_core.moc"
