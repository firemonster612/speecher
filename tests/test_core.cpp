#include "core/SecretStore.h"
#include "core/AppSettings.h"
#include "core/BindingProcessor.h"
#include "core/OutputMethod.h"
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
#include "output/TextDelivery.h"
#include "output/YdotoolDelivery.h"
#include "output/YdotoolSetup.h"

#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

#include <utility>

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

    void emitFailure(const QString &message)
    {
        emit failed(message);
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
        lastBindingVocabulary = settings.bindingVocabulary;
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
    QStringList lastBindingVocabulary;
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

class FakeBackend final : public DeliveryBackend {
public:
    FakeBackend(QString method, QList<QString> *attempts, QHash<QString, bool> *results)
        : m_method(std::move(method))
        , m_attempts(attempts)
        , m_results(results)
    {
    }

    bool deliver(const QString &, QString *error) override
    {
        m_attempts->append(m_method);
        const bool ok = m_results->value(m_method, false);
        if (!ok && error) {
            *error = m_method + QStringLiteral(" failed");
        }
        return ok;
    }

private:
    QString m_method;
    QList<QString> *m_attempts = nullptr;
    QHash<QString, bool> *m_results = nullptr;
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

static int httpContentLength(const QByteArray &headers)
{
    for (const QByteArray &line : headers.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            bool ok = false;
            const int value = trimmed.mid(QByteArrayLiteral("content-length:").size()).trimmed().toInt(&ok);
            return ok ? value : -1;
        }
    }
    return -1;
}

static QByteArray readHttpRequest(QTcpSocket *socket, int timeoutMs)
{
    QByteArray request;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        request += socket->readAll();

        const int headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd >= 0) {
            const int contentLength = httpContentLength(request.left(headerEnd));
            if (contentLength >= 0 && request.size() >= headerEnd + 4 + contentLength) {
                return request;
            }
        }

        socket->waitForReadyRead(20);
    }
    request += socket->readAll();
    return request;
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

    void bindingNormalizationAndValidation()
    {
        QCOMPARE(BindingProcessor::normalizedPhrase(QStringLiteral(" My, EMAIL! C++ repo_path ")),
                 QStringLiteral("my email c repo path"));
        QCOMPARE(BindingProcessor::normalizedTokens(QStringLiteral("alpha.beta/gamma")),
                 QStringList({QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")}));

        const BindingValidationResult validation = BindingProcessor::validateRules({
            {QStringLiteral("my,email"), QStringLiteral("one")},
            {QStringLiteral("MY email"), QStringLiteral("two")},
            {QStringLiteral("++"), QStringLiteral("symbols only")},
            {QStringLiteral("empty replacement"), QStringLiteral("   ")},
        });
        QVERIFY(!validation.ok());
        QCOMPARE(validation.issues.size(), 3);
        QVERIFY(validation.issues.at(0).type == BindingValidationIssue::Type::DuplicatePhrase);
        QVERIFY(validation.issues.at(1).type == BindingValidationIssue::Type::EmptyPhrase);
        QVERIFY(validation.issues.at(2).type == BindingValidationIssue::Type::EmptyReplacement);
        QCOMPARE(validation.rules.size(), 1);
        QCOMPARE(validation.rules.at(0).phrase, QStringLiteral("my,email"));

        QCOMPARE(BindingProcessor::refinementVocabulary({
                     {QStringLiteral("my,email"), QStringLiteral("efox@example.com")},
                     {QStringLiteral("speecher repo"), QStringLiteral("/home/efox/projects/speecher3")},
                 }),
                 QStringList({QStringLiteral("my,email"),
                              QStringLiteral("my email"),
                              QStringLiteral("speecher repo")}));
        QCOMPARE(BindingProcessor::explicitNoBindPhrases(
                     QStringLiteral("please write my email but don't turn that into a binding"),
                     {{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}),
                 QStringList({QStringLiteral("my email")}));
        QCOMPARE(BindingProcessor::explicitNoBindPhrases(
                     QStringLiteral("please write my email and my phone but don't turn that into a binding"),
                     {
                         {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
                         {QStringLiteral("my phone"), QStringLiteral("+1 555 0100")},
                     }),
                 QStringList({QStringLiteral("my phone")}));
        QVERIFY(BindingProcessor::hasExplicitNoBindDirective(
            QStringLiteral("please write my evil but don't turn that into a binding")));
    }

    void bindingProcessorMatchesCasePunctuationAndSkipsCoveredText()
    {
        const BindingProcessingResult result = BindingProcessor::process(
            QStringLiteral("My, email! my phone"),
            {
                {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
                {QStringLiteral("my phone"), QStringLiteral("+1 555 0100")},
            });

        QCOMPARE(result.boundText, QStringLiteral("efox@example.com! +1 555 0100"));
        QCOMPARE(result.placeholderText, QStringLiteral("SPEECHER_BINDING_0! SPEECHER_BINDING_1"));
        QCOMPARE(result.canSkipRefinement, true);
        QCOMPARE(result.placeholders.size(), 2);
    }

    void bindingProcessorRejectsGluedWordsAndPrefersLongestMatch()
    {
        const BindingProcessingResult result = BindingProcessor::process(
            QStringLiteral("open main repo and repo mainrepo"),
            {
                {QStringLiteral("repo"), QStringLiteral("R")},
                {QStringLiteral("main repo"), QStringLiteral("M")},
            });

        QCOMPARE(result.boundText, QStringLiteral("open M and R mainrepo"));
        QCOMPARE(result.placeholderText, QStringLiteral("open SPEECHER_BINDING_0 and SPEECHER_BINDING_1 mainrepo"));
        QCOMPARE(result.canSkipRefinement, false);
    }

    void bindingProcessorRestoresPlaceholdersAndAllowsMissingOnes()
    {
        const BindingProcessingResult result = BindingProcessor::process(
            QStringLiteral("my email and my email"),
            {{QStringLiteral("my email"), QStringLiteral("efox@example.com")}});

        QCOMPARE(result.boundText, QStringLiteral("efox@example.com and efox@example.com"));
        QCOMPARE(result.placeholderText, QStringLiteral("SPEECHER_BINDING_0 and SPEECHER_BINDING_1"));
        QCOMPARE(result.placeholders.size(), 2);

        BindingRestoreResult restored = BindingProcessor::restorePlaceholders(
            QStringLiteral("Please send SPEECHER_BINDING_0."),
            result.placeholders);
        QVERIFY(restored.ok);
        QCOMPARE(restored.text, QStringLiteral("Please send efox@example.com."));

        restored = BindingProcessor::restorePlaceholders(QStringLiteral("Please send Alex."), result.placeholders);
        QVERIFY(restored.ok);
        QCOMPARE(restored.text, QStringLiteral("Please send Alex."));

        restored = BindingProcessor::restorePlaceholders(
            QStringLiteral("Please send SPEECHER_BINDING_99."),
            result.placeholders);
        QVERIFY(!restored.ok);

        restored = BindingProcessor::restorePlaceholders(
            QStringLiteral("Please send Speecher binding 0."),
            result.placeholders);
        QVERIFY(!restored.ok);

        restored = BindingProcessor::restorePlaceholders(
            QStringLiteral("Please send Speecher binding zero."),
            result.placeholders);
        QVERIFY(!restored.ok);

        QCOMPARE(BindingProcessor::applyBindingsOutsidePlaceholders(
                     QStringLiteral("SPEECHER_BINDING_0 and speecher binding"),
                     {{QStringLiteral("speecher binding"), QStringLiteral("bound")}}),
                 QStringLiteral("SPEECHER_BINDING_0 and bound"));
    }

    void settingsDefaults()
    {
        SettingsStore settings;
        settings.raw().clear();
        QCOMPARE(settings.previewWords(), 8);
        QCOMPARE(settings.theme(), QStringLiteral("system"));
        QCOMPARE(settings.pauseMediaDuringTranscription(), true);
        QCOMPARE(settings.customVocabulary(), QStringList());
        QCOMPARE(settings.bindingRules().size(), 0);
        QCOMPARE(settings.refinementProvider(), QStringLiteral("openai"));
        QCOMPARE(settings.refinementStyle(), QStringLiteral("balanced"));
        QCOMPARE(settings.openAiModel(), QStringLiteral("gpt-5.4-mini"));
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("auto"));
        QCOMPARE(settings.outputMethod(), QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(settings.ydotoolEnabled(), false);
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

        settings.setOpenAiModel(QStringLiteral(" gpt-5.4-nano "));
        QCOMPARE(settings.openAiModel(), QStringLiteral("gpt-5.4-nano"));
        settings.setOpenAiModel(QString());
        QCOMPARE(settings.openAiModel(), QStringLiteral("gpt-5.4-mini"));

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

    void settingsSnapshot()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setPreviewWords(12);
        settings.setTheme(QStringLiteral("dark"));
        settings.setPauseMediaDuringTranscription(false);
        settings.setSpeechProvider(QStringLiteral("claude"));
        settings.setCustomVocabulary({QStringLiteral("Deepgram Nova 3"), QStringLiteral("Speecher")});
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setRefinementStyle(QStringLiteral("strong_polish"));
        settings.setOpenAiAuthMode(QStringLiteral("env"));
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
        QCOMPARE(snapshot.refinement.openAiAuthMode, QStringLiteral("env"));
        QCOMPARE(snapshot.output.method, QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(snapshot.output.ydotoolEnabled, false);
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

    void outputDeliverySelection()
    {
        OutputSettings settings;
        settings.method = QString::fromLatin1(OutputMethod::Automatic);
        settings.ydotoolEnabled = true;
        QCOMPARE(TextDelivery::orderedMethods(settings),
                 QStringList({QString::fromLatin1(OutputMethod::Ydotool),
                              QString::fromLatin1(OutputMethod::WlCopy),
                              QString::fromLatin1(OutputMethod::QtClipboard)}));

        settings.ydotoolEnabled = false;
        QCOMPARE(TextDelivery::orderedMethods(settings),
                 QStringList({QString::fromLatin1(OutputMethod::WlCopy),
                              QString::fromLatin1(OutputMethod::QtClipboard)}));

        settings.method = QString::fromLatin1(OutputMethod::Ydotool);
        QCOMPARE(TextDelivery::orderedMethods(settings), QStringList());
        settings.ydotoolEnabled = true;
        QCOMPARE(TextDelivery::orderedMethods(settings), QStringList({QString::fromLatin1(OutputMethod::Ydotool)}));

        settings.method = QString::fromLatin1(OutputMethod::QtClipboard);
        QCOMPARE(TextDelivery::orderedMethods(settings), QStringList({QString::fromLatin1(OutputMethod::QtClipboard)}));
    }

    void outputAutomaticFallbackOrder()
    {
        QList<QString> attempts;
        QHash<QString, bool> results;
        results.insert(QString::fromLatin1(OutputMethod::Ydotool), false);
        results.insert(QString::fromLatin1(OutputMethod::WlCopy), true);
        results.insert(QString::fromLatin1(OutputMethod::QtClipboard), true);

        TextDelivery delivery([&attempts, &results](const QString &method, const OutputSettings &) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        });

        OutputSettings settings;
        settings.method = QString::fromLatin1(OutputMethod::Automatic);
        settings.ydotoolEnabled = true;
        const DeliveryResult result = delivery.deliver(settings, QStringLiteral("hello"));
        QVERIFY(result.ok);
        QCOMPARE(result.copied, true);
        QCOMPARE(attempts, QList<QString>({QString::fromLatin1(OutputMethod::Ydotool), QString::fromLatin1(OutputMethod::WlCopy)}));
    }

    void outputExplicitMethodDoesNotFallback()
    {
        QList<QString> attempts;
        QHash<QString, bool> results;
        results.insert(QString::fromLatin1(OutputMethod::WlCopy), false);
        results.insert(QString::fromLatin1(OutputMethod::QtClipboard), true);

        TextDelivery delivery([&attempts, &results](const QString &method, const OutputSettings &) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        });

        OutputSettings settings;
        settings.method = QString::fromLatin1(OutputMethod::WlCopy);
        settings.ydotoolEnabled = true;
        const DeliveryResult result = delivery.deliver(settings, QStringLiteral("hello"));
        QVERIFY(!result.ok);
        QCOMPARE(attempts, QList<QString>({QString::fromLatin1(OutputMethod::WlCopy)}));
    }

    void ydotoolDeliveryBuildsTypeAndPasteCommands()
    {
        const QString text = QStringLiteral("hello\nworld\n \t");
        const QStringList args = YdotoolDelivery::commandArguments(text);
        QCOMPARE(args,
                 QStringList({QStringLiteral("type"),
                              QStringLiteral("--key-delay=1"),
                              QStringLiteral("--key-hold=2"),
                              QStringLiteral("--escape=0"),
                              QStringLiteral("--"),
                              QStringLiteral("hello\nworld")}));
        QCOMPARE(YdotoolDelivery::withoutTrailingWhitespace(QStringLiteral("one\n\n")), QStringLiteral("one"));
        QCOMPARE(YdotoolDelivery::withoutTrailingWhitespace(QStringLiteral("one\n \t")), QStringLiteral("one"));
        QCOMPARE(YdotoolDelivery::withoutTrailingWhitespace(QStringLiteral(" one\ntwo")), QStringLiteral(" one\ntwo"));
        QVERIFY(!args.contains(QStringLiteral("--file=-")));
        QVERIFY(std::none_of(args.cbegin(), args.cend(), [](const QString &arg) {
            return arg.startsWith(QStringLiteral("--file="));
        }));

        QCOMPARE(YdotoolDelivery::pasteShortcutArguments(),
                 QStringList({QStringLiteral("key"),
                              QStringLiteral("--key-delay=2"),
                              QStringLiteral("29:1"),
                              QStringLiteral("42:1"),
                              QStringLiteral("47:1"),
                              QStringLiteral("47:0"),
                              QStringLiteral("42:0"),
                              QStringLiteral("29:0")}));
    }

    void ydotoolStatusEvaluation()
    {
        YdotoolProbeFacts facts;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::NotInstalled);

        facts.ydotoolInstalled = true;
        facts.ydotooldInstalled = true;
        facts.uinputExists = true;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::NeedsUinputPermission);

        facts.userInConfiguredGroup = true;
        facts.currentSessionInConfiguredGroup = false;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::NeedsSignOut);

        facts.currentSessionInConfiguredGroup = true;
        facts.uinputReadWrite = true;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::DaemonNotRunning);

        facts.socketExists = true;
        facts.socketWritable = true;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::Disabled);

        facts.enabledInSpeecher = true;
        const YdotoolSetupStatus ready = YdotoolSetup::evaluate(facts);
        QCOMPARE(ready.state, YdotoolSetupState::Ready);
        QVERIFY(ready.ready());
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
        QCOMPARE(delivery->lastSettings.method, QString::fromLatin1(OutputMethod::Automatic));
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

    void dictationSessionAppliesBindingsWhenRefinementDisabled()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        speech->emitFinalText(QStringLiteral("My, email!"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("efox@example.com!"));
    }

    void dictationSessionSkipsRefinementWhenBindingsCoverTranscript()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({
            {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
            {QStringLiteral("my phone"), QStringLiteral("+1 555 0100")},
        }));

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
        speech->emitFinalText(QStringLiteral("my email, my phone"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("efox@example.com, +1 555 0100"));
        QCOMPARE(refiner->prepareCalls, 0);
        QCOMPARE(refiner->refineCalls, 0);
    }

    void dictationSessionProtectsBindingsDuringRefinement()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setCustomVocabulary({QStringLiteral("Qt")});
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please send my email to Alex"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please send SPEECHER_BINDING_0 to Alex.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("please send SPEECHER_BINDING_0 to Alex"));
        QCOMPARE(refiner->lastVocabulary, QStringList({QStringLiteral("Qt")}));
        QCOMPARE(refiner->lastBindingVocabulary, QStringList({QStringLiteral("my email")}));
        QVERIFY(!refiner->lastVocabulary.contains(QStringLiteral("efox@example.com")));
        QVERIFY(!refiner->lastBindingVocabulary.contains(QStringLiteral("efox@example.com")));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please send efox@example.com to Alex."));
    }

    void dictationSessionAppliesBindingsCorrectedByRefinement()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setCustomVocabulary({QStringLiteral("Qt")});
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please send my evil to Alex"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please send my email to Alex.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("please send my evil to Alex"));
        QCOMPARE(refiner->lastVocabulary, QStringList({QStringLiteral("Qt")}));
        QCOMPARE(refiner->lastBindingVocabulary, QStringList({QStringLiteral("my email")}));
        QVERIFY(!refiner->lastVocabulary.contains(QStringLiteral("efox@example.com")));
        QVERIFY(!refiner->lastBindingVocabulary.contains(QStringLiteral("efox@example.com")));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please send efox@example.com to Alex."));
    }

    void dictationSessionPostRefinementBindingsPreserveExistingPlaceholders()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({
            {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
            {QStringLiteral("speecher binding"), QStringLiteral("bad replacement")},
        }));

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
        speech->emitFinalText(QStringLiteral("please send my email and my evil"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please send SPEECHER_BINDING_0 and my email.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("please send SPEECHER_BINDING_0 and my evil"));
        QCOMPARE(refiner->lastVocabulary, QStringList());
        QCOMPARE(refiner->lastBindingVocabulary,
                 QStringList({QStringLiteral("my email"), QStringLiteral("speecher binding")}));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please send efox@example.com and efox@example.com."));
    }

    void dictationSessionHonorsDoNotBindRequest()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please write my email but don't turn that into a binding"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please write my email.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript,
                 QStringLiteral("please write my email but don't turn that into a binding"));
        QCOMPARE(refiner->lastBindingVocabulary, QStringList({QStringLiteral("my email")}));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please write my email."));
    }

    void dictationSessionDoesNotPostBindAmbiguousDoNotBindRequest()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please write my evil but don't turn that into a binding"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please write my email.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript,
                 QStringLiteral("please write my evil but don't turn that into a binding"));
        QCOMPARE(refiner->lastBindingVocabulary, QStringList({QStringLiteral("my email")}));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please write my email."));
    }

    void dictationSessionRefinerFailureFallsBackToBoundText()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please send my email"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        refiner->emitFailure(QStringLiteral("refinement failed"));

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("please send efox@example.com"));
    }

    void dictationSessionCorruptedPlaceholderFallsBackToBoundText()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please send my email"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please send SPEECHER_BINDING_ZERO.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("please send efox@example.com"));
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

    void dictationSessionStopsOnEmptyAudioFailure()
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
        audio->emitFailure(QStringLiteral("microphone blocked"));

        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 200);
        QCOMPARE(audio->isActive(), false);
        QCOMPARE(speech->stopCalls, 1);
        QCOMPARE(media->resumeCalls, 1);
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(session.lastMessage(), QStringLiteral("microphone blocked"));
    }

    void refinementInstructionsCompose()
    {
        const QString light = openAiRefinementInstructions(QStringLiteral("light_cleanup"));
        QVERIFY(light.startsWith(QStringLiteral("You are Speecher's transcript refinement engine.")));
        QVERIFY(light.contains(QStringLiteral("Your job is to produce the final text the user intended to paste or send.")));
        QVERIFY(light.contains(QStringLiteral("This is transcription cleanup and rewriting, not conversation")));
        QVERIFY(light.contains(QStringLiteral("Rule: return_only_refined_text.")));
        QVERIFY(light.contains(QStringLiteral("Rule: preserve_speecher_binding_placeholders.")));
        QVERIFY(light.contains(QStringLiteral("SPEECHER_BINDING_[0-9]+")));
        QVERIFY(light.contains(QStringLiteral("Rule: binding_alias_near_matches.")));
        QVERIFY(light.contains(QStringLiteral("correct obvious speech-to-text mistakes")));
        QVERIFY(light.contains(QStringLiteral("Rule: honor_do_not_bind_requests.")));
        QVERIFY(light.contains(QStringLiteral("Remove the instruction text")));
        QVERIFY(light.contains(QStringLiteral("Rule: spoken_unordered_list_cues.")));
        QVERIFY(light.contains(QStringLiteral("render it as a short lead-in followed by hyphen bullets")));
        QVERIFY(light.contains(QStringLiteral("Rule: spoken_order_cues.")));
        QVERIFY(light.contains(QStringLiteral("For procedures, recipes, instructions, checklists, rankings, or ordered sequences")));
        QVERIFY(light.contains(QStringLiteral("render a vertical Markdown numbered list by default")));
        QVERIFY(light.contains(QStringLiteral("Rule: no_inferred_structure.")));
        QVERIFY(light.contains(QStringLiteral("Output style: adaptive_markdown.")));
        QVERIFY(light.contains(QStringLiteral("Keep short simple lists inside a sentence with commas or semicolons")));
        QVERIFY(light.contains(QStringLiteral("Ingredients needed for an apple pie:\n- Apples\n- Cinnamon")));
        QVERIFY(light.contains(QStringLiteral("1. Gather your ingredients: apples, butter, cinnamon, caramel sauce, and pie crust.")));
        QVERIFY(light.contains(QStringLiteral("even Light may produce a bullet list")));
        QVERIFY(!light.contains(QStringLiteral("Rule: infer_simple_structure.")));
        QVERIFY(!light.contains(QStringLiteral("Rule: useful_organization.")));
        QVERIFY(!light.contains(QStringLiteral("plain_sentences")));

        const QString balanced = openAiRefinementInstructions(QStringLiteral("balanced"));
        QVERIFY(balanced.contains(QStringLiteral("Rule: no_inferred_structure.")));
        QVERIFY(balanced.contains(QStringLiteral("Rule: infer_simple_structure.")));
        QVERIFY(balanced.contains(QStringLiteral("Rule: adaptive_markdown.")));
        QVERIFY(balanced.contains(QStringLiteral("Use hyphen bullets for unordered multi-item lists.")));
        QVERIFY(!balanced.contains(QStringLiteral("Rule: useful_organization.")));

        const QString strong = openAiRefinementInstructions(QStringLiteral("strong_polish"));
        QVERIFY(strong.contains(QStringLiteral("Rule: no_inferred_structure.")));
        QVERIFY(strong.contains(QStringLiteral("Rule: infer_simple_structure.")));
        QVERIFY(strong.contains(QStringLiteral("Rule: useful_organization.")));
        QVERIFY(strong.contains(QStringLiteral("Rule: technical_literal_priority.")));
    }

    void openAiRefinerSendsAdaptiveInstructions()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        OpenAiRefiner refiner;
        QSignalSpy completed(&refiner, &OpenAiRefiner::completed);
        QSignalSpy failed(&refiner, &OpenAiRefiner::failed);

        const QString rawTranscript = QStringLiteral("to make an apple pie, the first step is to gather your ingredients. You need apples, butter, cinnamon, caramel sauce, and pie crust. Then you assemble the ingredients. Then number three is you bake your apple pie for fifty minutes. And then the fourth step is take it out and enjoy.");
        refiner.refine(rawTranscript,
                       QStringList{QStringLiteral("Qt"), QStringLiteral("Pie crust")},
                       QStringList{QStringLiteral("my email"), QStringLiteral("speecher repo")},
                       QStringLiteral("test-token"),
                       QStringLiteral("org-id"),
                       QStringLiteral("project-id"),
                       QStringLiteral("http://127.0.0.1:%1/v1/").arg(server.serverPort()),
                       QStringLiteral("acct-id"),
                       true,
                       QStringLiteral("gpt-test"),
                       QStringLiteral("balanced"));

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(socket);

        const QByteArray request = readHttpRequest(socket, 1000);
        const int headerEnd = request.indexOf("\r\n\r\n");
        QVERIFY2(headerEnd >= 0, request.constData());
        const QByteArray headers = request.left(headerEnd);
        const int contentLength = httpContentLength(headers);
        QVERIFY(contentLength > 0);
        QVERIFY(request.size() >= headerEnd + 4 + contentLength);

        QCOMPARE(headers.left(headers.indexOf('\n')).trimmed(), QByteArrayLiteral("POST /v1/responses HTTP/1.1"));
        const QByteArray lowerHeaders = headers.toLower();
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("authorization: bearer test-token")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("openai-organization: org-id")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("openai-project: project-id")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("chatgpt-account-id: acct-id")));

        QJsonParseError parseError;
        const QByteArray payload = request.mid(headerEnd + 4, contentLength);
        const QJsonObject body = QJsonDocument::fromJson(payload, &parseError).object();
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-test"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), true);
        QCOMPARE(body.value(QStringLiteral("store")).toBool(), false);

        const QString instructions = body.value(QStringLiteral("instructions")).toString();
        QVERIFY(instructions.contains(QStringLiteral("Rule: preserve_speecher_binding_placeholders.")));
        QVERIFY(instructions.contains(QStringLiteral("Do not change their case, punctuation, spacing, digits, or underscores.")));
        QVERIFY(instructions.contains(QStringLiteral("Rule: binding_alias_near_matches.")));
        QVERIFY(instructions.contains(QStringLiteral("Rule: honor_do_not_bind_requests.")));
        QVERIFY(instructions.contains(QStringLiteral("Rule: spoken_unordered_list_cues.")));
        QVERIFY(instructions.contains(QStringLiteral("If that list is the main content of the transcript or has four or more items")));
        QVERIFY(instructions.contains(QStringLiteral("Ingredients needed for an apple pie:\n- Apples\n- Cinnamon")));
        QVERIFY(instructions.contains(QStringLiteral("Rule: spoken_order_cues.")));
        QVERIFY(instructions.contains(QStringLiteral("render a vertical Markdown numbered list by default")));
        QVERIFY(instructions.contains(QStringLiteral("1. Gather your ingredients: apples, butter, cinnamon, caramel sauce, and pie crust.")));
        QVERIFY(!instructions.contains(QStringLiteral("plain_sentences")));

        const QJsonArray input = body.value(QStringLiteral("input")).toArray();
        QCOMPARE(input.size(), 1);
        const QJsonObject user = input.at(0).toObject();
        QCOMPARE(user.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        const QString content = user.value(QStringLiteral("content")).toString();
        QVERIFY(content.contains(rawTranscript));
        QVERIFY(content.contains(QStringLiteral("Preferred vocabulary:\nQt, Pie crust")));
        QVERIFY(content.contains(QStringLiteral("Binding aliases:\nmy email, speecher repo")));

        const QByteArray sse = QByteArrayLiteral("event: response.output_text.delta\n"
                                                 "data: {\"delta\":\"1. Gather\"}\n\n"
                                                 "event: response.completed\n"
                                                 "data: {\"type\":\"response.completed\"}\n\n");
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/event-stream\r\n"
                                        "Content-Length: ")
                      + QByteArray::number(sse.size())
                      + QByteArrayLiteral("\r\n"
                                          "Connection: close\r\n"
                                          "\r\n")
                      + sse);
        QVERIFY(socket->waitForBytesWritten(1000));
        socket->disconnectFromHost();

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 1000);
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("1. Gather"));
        QTest::qWait(50);
        QCOMPARE(completed.size(), 1);
        QCOMPARE(failed.size(), 0);
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
