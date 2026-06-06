#include "core/SecretStore.h"
#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/VocabularyLimit.h"
#include "core/WordPreview.h"
#include "dictation/DictationSession.h"
#include "dictation/DictationTypes.h"
#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeVoiceClient.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/OpenAiRefiner.h"
#include "providers/ProviderRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

using namespace speecher;

class FakeAudioInput final : public AudioInput {
public:
    explicit FakeAudioInput(QObject *parent = nullptr)
        : AudioInput(parent)
    {
    }

    bool start(QString *error = nullptr) override
    {
        if (!startResult) {
            if (error) {
                *error = startError;
            }
            return false;
        }
        started = true;
        active = true;
        return true;
    }

    void stop() override
    {
        active = false;
        emit levelChanged(0.0f);
    }

    bool isActive() const override
    {
        return active;
    }

    void pushAudio(const QByteArray &pcm)
    {
        emit audioChunk(pcm);
    }

    bool startResult = true;
    QString startError = QStringLiteral("audio failed");
    bool started = false;
    bool active = false;
};

class FakeMediaController final : public MediaController {
public:
    explicit FakeMediaController(QObject *parent = nullptr)
        : MediaController(parent)
    {
    }

    void pausePlaying() override
    {
        ++pauseCalls;
    }

    void resumePaused() override
    {
        ++resumeCalls;
    }

    int pauseCalls = 0;
    int resumeCalls = 0;
};

class FakeSpeechTranscriber final : public SpeechTranscriber {
public:
    explicit FakeSpeechTranscriber(QObject *parent = nullptr)
        : SpeechTranscriber(parent)
    {
    }

    QString id() const override
    {
        return QStringLiteral("claude");
    }

    QString label() const override
    {
        return QStringLiteral("Fake Speech");
    }

    bool requiresRefresh(const SpeechSettings &) const override
    {
        return refreshRequired;
    }

    SpeechPrepareResult prepare(const SpeechSettings &) override
    {
        ++prepareCalls;
        return prepareResult;
    }

    void start(const SpeechSettings &settings) override
    {
        ++startCalls;
        lastVocabulary = settings.vocabulary;
    }

    void sendAudio(const QByteArray &pcm) override
    {
        audioChunks << pcm;
    }

    void stop() override
    {
        ++stopCalls;
    }

    void emitPartialText(const QString &text)
    {
        emit partialTranscript(text);
    }

    void emitFinalText(const QString &text)
    {
        emit finalTranscript(text);
    }

    void emitFailure(const QString &message)
    {
        emit failed(message);
    }

    bool refreshRequired = false;
    SpeechPrepareResult prepareResult{true, {}};
    int prepareCalls = 0;
    int startCalls = 0;
    int stopCalls = 0;
    QList<QByteArray> audioChunks;
    QStringList lastVocabulary;
};

class FakeRefiner final : public TranscriptRefiner {
public:
    explicit FakeRefiner(QObject *parent = nullptr)
        : TranscriptRefiner(parent)
    {
    }

    QString id() const override
    {
        return QStringLiteral("openai");
    }

    QString label() const override
    {
        return QStringLiteral("Fake Refiner");
    }

    bool requiresRefresh(const RefinementSettings &) const override
    {
        return refreshRequired;
    }

    void refresh(const RefinementSettings &) override
    {
        ++refreshCalls;
    }

    RefinementPrepareResult prepare(const RefinementSettings &) override
    {
        ++prepareCalls;
        return prepareResult;
    }

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const RefinementSettings &settings) override
    {
        ++refineCalls;
        lastRawTranscript = rawTranscript;
        lastVocabulary = vocabulary;
        lastStyle = settings.style;
        if (autoComplete) {
            emit completed(autoCompleteText);
        }
    }

    void cancel() override
    {
        ++cancelCalls;
    }

    void emitDeltaText(const QString &text)
    {
        emit delta(text);
    }

    void emitCompletedText(const QString &text)
    {
        emit completed(text);
    }

    void emitFailure(const QString &message)
    {
        emit failed(message);
    }

    bool refreshRequired = false;
    bool autoComplete = false;
    QString autoCompleteText;
    RefinementPrepareResult prepareResult{true, {}};
    int refreshCalls = 0;
    int prepareCalls = 0;
    int refineCalls = 0;
    int cancelCalls = 0;
    QString lastRawTranscript;
    QStringList lastVocabulary;
    QString lastStyle;
};

class FakeDelivery final : public TextDeliveryAdapter {
public:
    explicit FakeDelivery(QObject *parent = nullptr)
        : TextDeliveryAdapter(parent)
    {
    }

    DeliveryResult deliver(const OutputSettings &settings, const QString &text) override
    {
        ++calls;
        lastSettings = settings;
        lastText = text;
        return result;
    }

    DeliveryResult result{true, false, QStringLiteral("Delivered")};
    int calls = 0;
    OutputSettings lastSettings;
    QString lastText;
};

static void registerFakeSpeechProvider(ProviderRegistry &registry, FakeSpeechTranscriber **speech)
{
    registry.registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Fake Speech")}, [speech](QObject *parent) {
        *speech = new FakeSpeechTranscriber(parent);
        return *speech;
    });
}

static void registerFakeRefiner(ProviderRegistry &registry, FakeRefiner **refiner)
{
    registry.registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake Refiner")}, [refiner](QObject *parent) {
        *refiner = new FakeRefiner(parent);
        return *refiner;
    });
}

class CoreTests : public QObject {
    Q_OBJECT

    static bool writeJsonCredentials(const QString &path, const QString &accessToken, const QDateTime &expiresAt)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        QJsonObject oauth{
            {QStringLiteral("accessToken"), accessToken},
            {QStringLiteral("expiresAt"), double(expiresAt.toSecsSinceEpoch())},
        };
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("claudeAiOauth"), oauth}}).toJson());
        return true;
    }

    static QString writeFakeClaudeScript(const QString &path, const QString &body)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return {};
        }
        QTextStream stream(&file);
        stream << "#!/bin/sh\n" << body;
        file.close();
        QFile::setPermissions(path,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                  | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                  | QFileDevice::ReadOther | QFileDevice::ExeOther);
        return path;
    }

    static QString jwtWithExpiry(const QDateTime &expiresAt)
    {
        const QByteArray header = QJsonDocument(QJsonObject{{QStringLiteral("alg"), QStringLiteral("none")}}).toJson(QJsonDocument::Compact)
                                      .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("exp"), double(expiresAt.toSecsSinceEpoch())}}).toJson(QJsonDocument::Compact)
                                       .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        return QString::fromLatin1(header + "." + payload + ".");
    }

    static bool writeCodexAuth(const QString &homePath, const QString &accessToken)
    {
        QDir dir(homePath);
        if (!dir.mkpath(QStringLiteral(".codex"))) {
            return false;
        }
        QFile file(dir.filePath(QStringLiteral(".codex/auth.json")));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        QJsonObject tokens{
            {QStringLiteral("access_token"), accessToken},
            {QStringLiteral("account_id"), QStringLiteral("acct")},
        };
        file.write(QJsonDocument(QJsonObject{
                                      {QStringLiteral("auth_mode"), QStringLiteral("chatgpt")},
                                      {QStringLiteral("tokens"), tokens},
                                  })
                       .toJson());
        return true;
    }

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

    void settingsSnapshot()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setPreviewWords(12);
        settings.setTheme(QStringLiteral("dark"));
        settings.setPauseMediaDuringTranscription(false);
        settings.setSpeechProvider(QStringLiteral("claude"));
        settings.setCustomVocabulary({QStringLiteral("Deepgram Nova 3"), QStringLiteral("Speecher")});
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setRefinementStyle(QStringLiteral("strong_polish"));
        settings.setRefinementOutputFormat(QStringLiteral("markdown"));
        settings.setOpenAiAuthMode(QStringLiteral("env"));

        const AppSettings snapshot = settings.snapshot();
        QCOMPARE(snapshot.ui.previewWords, 12);
        QCOMPARE(snapshot.ui.theme, QStringLiteral("dark"));
        QCOMPARE(snapshot.ui.pauseMediaDuringTranscription, false);
        QCOMPARE(snapshot.speech.providerId, QStringLiteral("claude"));
        QCOMPARE(snapshot.speech.vocabulary.size(), 2);
        QCOMPARE(snapshot.refinement.providerId, QStringLiteral("openai"));
        QCOMPARE(snapshot.refinement.style, QStringLiteral("strong_polish"));
        QCOMPARE(snapshot.refinement.outputFormat, QStringLiteral("markdown"));
        QCOMPARE(snapshot.refinement.openAiAuthMode, QStringLiteral("env"));
        QCOMPARE(snapshot.output.typeCommand, QStringLiteral("wtype"));
        QCOMPARE(snapshot.output.fallbackClipboard, true);
    }

    void providerRegistryReturnsSingletonAdapters()
    {
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);

        QCOMPARE(registry.speechProviders().size(), 1);
        QCOMPARE(registry.refinementProviders().size(), 1);
        QVERIFY(registry.speechProvider(QStringLiteral("missing")) == nullptr);
        QVERIFY(registry.refinementProvider(QStringLiteral("missing")) == nullptr);
        SpeechTranscriber *speechProvider = registry.speechProvider(QStringLiteral("claude"));
        TranscriptRefiner *refinementProvider = registry.refinementProvider(QStringLiteral("openai"));
        QVERIFY(speechProvider);
        QVERIFY(refinementProvider);
        QCOMPARE(registry.speechProvider(QStringLiteral("claude")), speechProvider);
        QCOMPARE(registry.refinementProvider(QStringLiteral("openai")), refinementProvider);
        QCOMPARE(speechProvider, speech);
        QCOMPARE(refinementProvider, refiner);
    }

    void dictationSessionDeliversRawTranscript()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setPauseMediaDuringTranscription(true);
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setCustomVocabulary({QStringLiteral("Speecher")});

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QCOMPARE(int(session.state()), int(DictationState::Listening));
        QVERIFY(audio->started);
        QCOMPARE(media->pauseCalls, 1);
        QCOMPARE(speech->prepareCalls, 1);
        QCOMPARE(speech->startCalls, 1);
        QCOMPARE(speech->lastVocabulary, QStringList{QStringLiteral("Speecher")});

        audio->pushAudio(QByteArrayLiteral("pcm"));
        QCOMPARE(speech->audioChunks.size(), 1);
        speech->emitFinalText(QStringLiteral("hello world"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("hello world"));
        QCOMPARE(delivery->lastSettings.typeCommand, QStringLiteral("wtype"));
        QCOMPARE(media->resumeCalls, 1);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 1800);
    }

    void dictationSessionRefinesTranscript()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setRefinementStyle(QStringLiteral("light_cleanup"));
        settings.setCustomVocabulary({QStringLiteral("Qt")});

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        speech->emitFinalText(QStringLiteral("rough text"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Polished text.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("rough text"));
        QCOMPARE(refiner->lastVocabulary, QStringList{QStringLiteral("Qt")});
        QCOMPARE(refiner->lastStyle, QStringLiteral("light_cleanup"));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Polished text."));
    }

    void dictationSessionFallsBackWhenRefinementUnavailable()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        speech->emitFinalText(QStringLiteral("raw fallback"));
        refiner->prepareResult = {false, QStringLiteral("No OpenAI credential found")};
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(refiner->prepareCalls, 1);
        QCOMPARE(refiner->refineCalls, 0);
        QCOMPARE(delivery->lastText, QStringLiteral("raw fallback"));
    }

    void dictationSessionStopsOnEmptySpeechFailure()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        speech->emitFailure(QStringLiteral("provider failed"));

        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 200);
        QCOMPARE(audio->isActive(), false);
        QCOMPARE(media->resumeCalls, 1);
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(session.lastMessage(), QStringLiteral("provider failed"));
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
        QVERIFY(writeJsonCredentials(path,
                                     QStringLiteral("secret-token"),
                                     QDateTime::currentDateTimeUtc().addSecs(-60)));
        QVERIFY(ClaudeCredentials::requiresRefresh(path));
        const ClaudeCredentialResult result = ClaudeCredentials::load(path);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("claude")));
    }

    void claudeCredentialsInteractiveRefresh()
    {
        QTemporaryDir dir;
        const QString credentialsPath = dir.filePath(QStringLiteral("credentials.json"));
        QVERIFY(writeJsonCredentials(credentialsPath,
                                     QStringLiteral("expired-token"),
                                     QDateTime::currentDateTimeUtc().addSecs(-60)));
        const QString fakeClaude = writeFakeClaudeScript(dir.filePath(QStringLiteral("claude-fake")), QStringLiteral(R"(
read prompt
cat > "$SPEECHER_TEST_CREDENTIALS_PATH" <<'JSON'
{"claudeAiOauth":{"accessToken":"refreshed-token","expiresAt":4102444800}}
JSON
echo OK
read command
exit 0
)"));
        QVERIFY(!fakeClaude.isEmpty());

        qputenv("SPEECHER_TEST_CLAUDE_EXECUTABLE", QFile::encodeName(fakeClaude));
        qputenv("SPEECHER_TEST_CREDENTIALS_PATH", QFile::encodeName(credentialsPath));
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CLAUDE_EXECUTABLE");
            qunsetenv("SPEECHER_TEST_CREDENTIALS_PATH");
        });

        const ClaudeCredentialResult result = ClaudeCredentials::load(credentialsPath, true);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.accessToken, QStringLiteral("refreshed-token"));
    }

    void claudeCredentialsInteractiveRefreshFailure()
    {
        QTemporaryDir dir;
        const QString credentialsPath = dir.filePath(QStringLiteral("credentials.json"));
        QVERIFY(writeJsonCredentials(credentialsPath,
                                     QStringLiteral("expired-token"),
                                     QDateTime::currentDateTimeUtc().addSecs(-60)));
        const QString fakeClaude = writeFakeClaudeScript(dir.filePath(QStringLiteral("claude-fake")), QStringLiteral(R"(
read prompt
exit 12
)"));
        QVERIFY(!fakeClaude.isEmpty());

        qputenv("SPEECHER_TEST_CLAUDE_EXECUTABLE", QFile::encodeName(fakeClaude));
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CLAUDE_EXECUTABLE");
        });

        const ClaudeCredentialResult result = ClaudeCredentials::load(credentialsPath, true);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("Claude refresh session")));
    }

    void claudeInstalledVersion()
    {
        const QString version = ClaudeCredentials::installedVersion();
        if (!version.isEmpty()) {
            QVERIFY(QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+")).match(version).hasMatch());
        }
    }

    void codexOauthRefreshesExpiredToken()
    {
        QTemporaryDir dir;
        QVERIFY(writeCodexAuth(dir.path(), jwtWithExpiry(QDateTime::currentDateTimeUtc().addSecs(-60))));
        const QString fakeCodex = writeFakeClaudeScript(dir.filePath(QStringLiteral("codex-fake")), QStringLiteral(R"(
test "$1" = "exec" || exit 10
test "$2" = "i" || exit 11
test "$3" = "--skip-git-repo-check" || exit 12
cat > "$HOME/.codex/auth.json" <<'JSON'
{"auth_mode":"chatgpt","tokens":{"access_token":"REFRESHED_TOKEN","account_id":"acct"}}
JSON
exit 0
)"));
        QVERIFY(!fakeCodex.isEmpty());

        const QByteArray oldHome = qgetenv("HOME");
        qputenv("HOME", QFile::encodeName(dir.path()));
        qputenv("SPEECHER_TEST_CODEX_EXECUTABLE", QFile::encodeName(fakeCodex));
        const auto cleanup = qScopeGuard([oldHome] {
            if (oldHome.isEmpty()) {
                qunsetenv("HOME");
            } else {
                qputenv("HOME", oldHome);
            }
            qunsetenv("SPEECHER_TEST_CODEX_EXECUTABLE");
        });

        OpenAiAuthProvider provider(nullptr, QStringLiteral("codex_oauth"));
        QVERIFY(provider.requiresCodexOauthRefresh());
        const OpenAiAuth auth = provider.resolve();
        QVERIFY2(auth.ok, qPrintable(auth.status));
        QCOMPARE(auth.bearerToken, QStringLiteral("REFRESHED_TOKEN"));
        QVERIFY(!provider.requiresCodexOauthRefresh());
    }

    void codexOauthRefreshFailure()
    {
        QTemporaryDir dir;
        QVERIFY(writeCodexAuth(dir.path(), jwtWithExpiry(QDateTime::currentDateTimeUtc().addSecs(-60))));
        const QString fakeCodex = writeFakeClaudeScript(dir.filePath(QStringLiteral("codex-fake")), QStringLiteral(R"(
echo failed >&2
exit 12
)"));
        QVERIFY(!fakeCodex.isEmpty());

        const QByteArray oldHome = qgetenv("HOME");
        qputenv("HOME", QFile::encodeName(dir.path()));
        qputenv("SPEECHER_TEST_CODEX_EXECUTABLE", QFile::encodeName(fakeCodex));
        const auto cleanup = qScopeGuard([oldHome] {
            if (oldHome.isEmpty()) {
                qunsetenv("HOME");
            } else {
                qputenv("HOME", oldHome);
            }
            qunsetenv("SPEECHER_TEST_CODEX_EXECUTABLE");
        });

        OpenAiAuthProvider provider(nullptr, QStringLiteral("codex_oauth"));
        const OpenAiAuth auth = provider.resolve();
        QVERIFY(!auth.ok);
        QVERIFY(auth.status.contains(QStringLiteral("Codex OAuth refresh")));
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
