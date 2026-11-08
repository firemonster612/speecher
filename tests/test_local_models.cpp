#include "app/LocalSetup.h"
#include "core/SettingsStore.h"
#include "core/SecretStore.h"
#include "core/settings/SettingsKeys.h"
#include "providers/ProviderRegistry.h"
#include "common/test_http.h"
#include "common/test_local_setup.h"
#include "common/test_suites.h"
#include "core/LocalModelCatalog.h"
#include "core/TranscriptState.h"
#include "providers/LocalModelStore.h"
#ifdef SPEECHER_WITH_LOCAL_SPEECH
#include "providers/LocalSpeechTranscriber.h"
#include "providers/LocalSpeechWorker.h"
#include "providers/SpeechAudio.h"
#endif

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <atomic>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

using namespace speecher;

namespace {

constexpr quint64 gb = 1000ull * 1000 * 1000;
constexpr quint64 gib = 1ull << 30;

HardwareProfile laptop4750u()
{
    // The prototype's "2020 laptop": 16 GB shared with an integrated GPU.
    HardwareProfile hardware;
    hardware.accelerator = HardwareProfile::Accelerator::IntegratedGpu;
    hardware.chipName = QStringLiteral("AMD Ryzen 7 PRO 4750U with Radeon Graphics");
    hardware.systemRamBytes = 16 * gb;
    hardware.availableRamBytes = 12 * gb;
    return hardware;
}

HardwareProfile m4Max36GB()
{
    HardwareProfile hardware;
    hardware.accelerator = HardwareProfile::Accelerator::AppleSilicon;
    hardware.chipName = QStringLiteral("Apple M4 Max");
    hardware.systemRamBytes = 36 * gb;
    hardware.gpuMemoryBytes = 28 * gb;
    return hardware;
}

LocalModel fakeModel(const QByteArray &content)
{
    LocalModel model;
    model.id = QStringLiteral("fake");
    model.huggingFaceRepo = QStringLiteral("owner/repo");
    model.revision = QStringLiteral("abc123");
    model.fileName = QStringLiteral("fake.gguf");
    model.sizeBytes = content.size();
    model.sha256 = QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
    return model;
}

#ifdef SPEECHER_WITH_LOCAL_SPEECH
// What the fake engine below did, read once its signals are in.
struct FakeModelCounts {
    std::atomic<int> loads = 0;
    std::atomic<int> sessions = 0;
};

// A 100 ms chunk the fake hears as the word "w<word>"; quiet() it hears as
// nothing.
QByteArray spoken(qint16 word)
{
    QByteArray chunk(kSpeechChunkBytes, 0);
    qToLittleEndian<qint16>(word, chunk.data());
    return chunk;
}

QByteArray quiet()
{
    return QByteArray(kSpeechChunkBytes, 0);
}

QString fakeWords(const QByteArray &pcm)
{
    QStringList words;
    for (qsizetype at = 0; at + kSpeechChunkBytes <= pcm.size(); at += kSpeechChunkBytes) {
        if (const qint16 word = qFromLittleEndian<qint16>(pcm.constData() + at)) {
            words << QStringLiteral("w%1").arg(word);
        }
    }
    return words.join(QLatin1Char(' '));
}

// A streaming session commits each word as it is fed.
class FakeSession final : public LocalSpeechEngine::Session {
public:
    std::optional<QString> transcribe(const QByteArray &pcm16, const QString &, QString *) override
    {
        return fakeWords(pcm16);
    }
    std::optional<QString> transcribeTimed(const QByteArray &pcm16, const QString &language,
                                           QList<TranscriptSegment> *, QString *error) override
    {
        return transcribe(pcm16, language, error);
    }
    bool beginStream(const QString &, QString *) override
    {
        m_committed.clear();
        return true;
    }
    bool feed(const QByteArray &pcm16, LocalSpeechEngine::StreamText *text, QString *) override
    {
        if (const QString words = fakeWords(pcm16); !words.isEmpty()) {
            m_committed += words + QLatin1Char(' ');
        }
        text->committed = m_committed;
        return true;
    }
    std::optional<QString> finalize(QString *) override { return m_committed.trimmed(); }

private:
    QString m_committed;
};

class FakeEngine final : public LocalSpeechEngine {
public:
    FakeEngine(FakeModelCounts &counts, bool streams)
        : m_counts(counts)
        , m_streams(streams)
    {
    }
    bool load(const QString &modelPath, const LocalRunsOn &runsOn, QString *) override
    {
        ++m_counts.loads;
        m_modelPath = modelPath;
        m_runsOn = runsOn;
        return true;
    }
    void unload() override { m_modelPath.clear(); }
    bool isLoaded(const QString &modelPath, const LocalRunsOn &runsOn) const override
    {
        return !m_modelPath.isEmpty() && modelPath == m_modelPath && runsOn == m_runsOn;
    }
    QString runsOnDescription() const override
    {
        return m_modelPath.isEmpty() ? QString() : QStringLiteral("Fake (CPU)");
    }
    bool streams() const override { return m_streams; }
    std::unique_ptr<Session> openSession(std::function<bool()>, QString *) override
    {
        ++m_counts.sessions;
        return std::make_unique<FakeSession>();
    }

private:
    FakeModelCounts &m_counts;
    bool m_streams;
    QString m_modelPath;
    LocalRunsOn m_runsOn;
};

// The model's file in store, the size the catalog gives, so it counts as
// downloaded.
bool fakeDownload(const LocalModelStore &store, const QString &modelId)
{
    const LocalModel *model = findLocalModel(modelId);
    QFile file(store.modelPath(*model));
    return QDir().mkpath(QFileInfo(file).path()) && file.open(QIODevice::WriteOnly) && file.resize(model->sizeBytes);
}

QStringList texts(const QSignalSpy &spy)
{
    QStringList result;
    for (const QList<QVariant> &signal : spy) {
        result << signal.at(1).toString();
    }
    return result;
}

#ifdef Q_OS_LINUX
qint64 residentBytes()
{
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) {
        return -1;
    }
    return statm.readAll().split(' ').value(1).toLongLong() * sysconf(_SC_PAGESIZE);
}
#endif
#endif

} // namespace

namespace {

// Header names are case-insensitive, and Qt on Windows sends them lowercase.
bool asksForRange(const QByteArray &request, const QByteArray &range)
{
    return request.toLower().contains("\r\nrange: " + range + "\r\n");
}

QUrl listenLocally(QTcpServer &server)
{
    if (!server.listen(QHostAddress::LocalHost)) qFatal("cannot listen on localhost");
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
}

void writePart(const LocalModelStore &store, const LocalModel &model, const QByteArray &bytes)
{
    QFile part(store.modelPath(model) + QStringLiteral(".part"));
    if (!part.open(QIODevice::WriteOnly) || part.write(bytes) != bytes.size()) qFatal("cannot write partial file");
}

// Answers one request with status and body, and hands back the request.
QByteArray serveOnce(QTcpServer &server, const QByteArray &statusLine, const QByteArray &body,
                     qint64 declaredLength = -1)
{
    if (!server.hasPendingConnections() && !server.waitForNewConnection(5000)) {
        return {};
    }
    QTcpSocket *socket = server.nextPendingConnection();
    const QByteArray request = test::readHttpRequest(socket, 2000);
    socket->write("HTTP/1.1 " + statusLine + "\r\nContent-Length: "
                  + QByteArray::number(declaredLength < 0 ? body.size() : declaredLength)
                  + "\r\nConnection: close\r\n\r\n" + body);
    socket->flush();
    socket->waitForBytesWritten(2000);
    socket->disconnectFromHost();
    return request;
}

class LocalModelsTests : public QObject {
    Q_OBJECT

private slots:
    void liveFactsDoesNotReadUnreadKeyringSecrets()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(SettingsKeys::SecretsInKeyring,
                               QStringList{QStringLiteral("speech-endpoint-key")});
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });
        ProviderRegistry providers;
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);

        QVERIFY(!setup.liveFacts().modelFolder.isEmpty());
        QVERIFY2(settings.secrets()->lastError().isEmpty(), "liveFacts tried to read the keyring");
        QVERIFY(!settings.secrets()->isSecretKnown(SecretStore::Secret::SpeechEndpointKey));
    }

    void endpointChecksDiscardSupersededResults_data()
    {
        QTest::addColumn<bool>("refinement");
        QTest::newRow("speech") << false;
        QTest::newRow("refinement") << true;
    }

    void endpointChecksDiscardSupersededResults()
    {
        QFETCH(bool, refinement);
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);
        QTcpServer slow, fast;
        QVERIFY(slow.listen(QHostAddress::LocalHost));
        QVERIFY(fast.listen(QHostAddress::LocalHost));
        const auto endpoint = [](quint16 port) {
            ServerSettings result;
            result.baseUrl = QString("http://127.0.0.1:%1").arg(port);
            return result;
        };
        const auto modelsForCheck = [&] {
            const auto facts = setup.liveFacts();
            return refinement ? facts.refinementEndpointModels : facts.speechEndpointModels;
        };
        const auto check = [&](AppSettings &snapshot) {
            snapshot.refinement.server.baseUrl = snapshot.speech.server.baseUrl;
            settings.applySnapshot(snapshot);
            if (refinement) setup.checkRefinementEndpoint(resolvedRefinementEndpoint(snapshot.refinement, snapshot.accounts));
            else setup.checkSpeechEndpoint(snapshot.speech.server);
        };
        auto snapshot = settings.snapshot();
        snapshot.speech.server = endpoint(slow.serverPort());
        check(snapshot);
        QTRY_VERIFY(slow.hasPendingConnections());
        snapshot.speech.server = endpoint(fast.serverPort());
        check(snapshot);
        QTRY_VERIFY(fast.hasPendingConnections());
        serveOnce(fast, "200 OK", R"({"data":[{"id":"new-B"}]})");
        QTRY_COMPARE(modelsForCheck(), QStringList{"new-B"});
        serveOnce(slow, "200 OK", R"({"data":[{"id":"old-A"}]})");
        QTest::qWait(100);
        QCOMPARE(modelsForCheck(), QStringList{"new-B"});
        // Picking a model keeps the verdict; another server does not.
        snapshot.speech.services.choice(QStringLiteral("endpoint")).model = "picked";
        snapshot.refinement.services.choice(QStringLiteral("endpoint")).model = "picked";
        settings.applySnapshot(snapshot);
        QCOMPARE(modelsForCheck(), QStringList{"new-B"});
        snapshot.speech.server = endpoint(slow.serverPort());
        snapshot.refinement.server.baseUrl = snapshot.speech.server.baseUrl;
        settings.applySnapshot(snapshot);
        const auto facts = setup.liveFacts();
        QVERIFY((refinement ? facts.refinementEndpointStatus : facts.speechEndpointStatus).isEmpty());
        QVERIFY(modelsForCheck().isEmpty());
    }

    void aSavedEndpointIsCheckedOnceEditsPauseAndGetsTheFirstModel()
    {
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto snapshot = settings.snapshot();
        snapshot.refinement.services.order = {"endpoint"};
        snapshot.refinement.server.baseUrl = "http://127.0.0.1:1/v";
        settings.applySnapshot(snapshot);
        // Still typing: the address grows before the pause.
        snapshot.refinement.server.baseUrl = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
        settings.applySnapshot(snapshot);
        QVERIFY(setup.liveFacts().refinementEndpointStatus.isEmpty());

        QTRY_VERIFY(server.hasPendingConnections());
        QVERIFY(serveOnce(server, "200 OK", R"({"data":[{"id":"first"},{"id":"second"}]})")
                    .startsWith("GET /v1/models"));
        QTRY_COMPARE(settings.dictationSnapshot().refinement.services.choice(QStringLiteral("endpoint")).model, QString("first"));
        QCOMPARE(setup.liveFacts().refinementEndpointStatus, QString("Connected. Models available: 2."));
        QCOMPARE(setup.liveFacts().refinementEndpointModels, QStringList({"first", "second"}));
        QVERIFY(!test::appRequestArrived(server, 1000));
    }

    void sharedChoicesPreserveSavedConfiguration()
    {
        const QList<DetectedRunner> runners{{"ollama", "Ollama", {}, {}, {"other", "gemma4:e4b:latest"}},
                                             {"lmstudio", "LM Studio", {}, {}, {"studio-model"}}};
        const CleanupModel suggestion{"gemma4:e4b", "Gemma", 100};
        auto choice = resolveRunnerChoice({"lmstudio", "studio-model"}, runners, suggestion);
        QCOMPARE(choice.selection.runner, QString("lmstudio"));
        QCOMPARE(choice.available->id, QString("lmstudio"));
        choice = resolveRunnerChoice({"missing", "saved-model"}, runners, suggestion);
        QVERIFY(!choice.available);
        QCOMPARE(choice.selection.model, QString("saved-model"));
        choice = resolveRunnerChoice({{}, {}}, runners, suggestion);
        QCOMPARE(choice.selection.runner, QString("ollama"));
        QCOMPARE(choice.selection.model, QString("gemma4:e4b:latest"));
        QVERIFY(!choice.offerPull);
        choice = resolveRunnerChoice({"ollama", "saved-model"}, runners, CleanupModel{"absent", {}, 0});
        QVERIFY(choice.offerPull);
        QVERIFY(choice.showSuggestion);
        choice = resolveRunnerChoice({"ollama", "saved-model"}, runners, std::nullopt);
        QVERIFY(choice.showSuggestion);
        QVERIFY(!choice.offerPull);
        // A pull is saved under the name Ollama lists it by.
        QCOMPARE(ollamaListedName("gemma4:e4b"), QString("gemma4:e4b"));
        QCOMPARE(ollamaListedName("LiquidAI/lfm2.5-1.2b-instruct"), QString("LiquidAI/lfm2.5-1.2b-instruct:latest"));
    }

    void interruptedDownloadsResumeButCancelledOnesDoNot()
    {
        QTemporaryDir directory;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QUrl origin(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        const auto &model = *findLocalModel("moonshine-small");
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        {
            LocalModelStore models(directory.path(), origin);
            LocalSetup setup(settings, providers, models);
            setup.download(model);
            QVERIFY(models.isDownloading(model.id));
        }
        {
            LocalModelStore models(directory.path(), origin);
            LocalSetup setup(settings, providers, models);
            QTRY_VERIFY(models.isDownloading(model.id));
            setup.cancelDownload(model.id);
        }
        LocalModelStore models(directory.path(), origin);
        LocalSetup setup(settings, providers, models);
        QCoreApplication::processEvents();
        QVERIFY(!models.isDownloading(model.id));
    }

    void speechModelChoiceAndDisplayAreShared()
    {
        QTemporaryDir directory;
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        providers.registerSpeechProvider({"local", "Local model"},
            [&models, worker = std::make_shared<LocalSpeechWorker>()](QObject *parent) {
                return new LocalSpeechTranscriber(models, worker, parent);
            });
#endif
        LocalSetup setup(settings, providers, models);
        QVERIFY(!settings.dictationSnapshot().speech.local.modelChosen);
        auto unchanged = settings.dictationSnapshot().speech.local;
        unchanged.idleUnloadMinutes = 60;
        settings.edit([&](AppSettings &draft) { draft.speech.local = unchanged; return true; });
        QVERIFY(!settings.dictationSnapshot().speech.local.modelChosen);
        const auto beforeProbe = localModelId(settings.dictationSnapshot().speech);
        setup.initializeSpeechModel();
        QVERIFY(!settings.dictationSnapshot().speech.local.modelChosen);
        QCOMPARE(localModelId(settings.dictationSnapshot().speech), beforeProbe);
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        setup.probeHardware();
        // The first Metal device init compiles ggml's shaders, which took
        // about 45 s on a CI runner; the suite has 120 s.
        QTRY_VERIFY_WITH_TIMEOUT(setup.hardwareKnown(), 90000);
        const AppSettings probed = settings.dictationSnapshot();
        QCOMPARE(localModelId(probed.speech), setup.suggestedModel(probed).id);
        QCOMPARE(setup.speechModelChoice(probed).id, setup.suggestedModel(probed).id);
        QVERIFY(!settings.dictationSnapshot().speech.local.modelChosen);
#endif
        setup.chooseSpeechModel("cohere");
        QVERIFY(settings.dictationSnapshot().speech.local.modelChosen);
        setup.initializeSpeechModel();
        QCOMPARE(setup.speechModelChoice(settings.dictationSnapshot()).id, QString("cohere"));
        const auto &model = *findLocalModel("cohere");
        auto speech = settings.snapshot().speech;
        QVERIFY(!setup.modelState(model, speech).inUse);
        // A result belongs to where it ran, so switching Runs on leaves the
        // model unmeasured until it is tested there.
        auto local = settings.dictationSnapshot().speech.local;
        local.speedTestSeconds.insert(localSpeedTestKey(model.id, {QStringLiteral("cpu"), {}}), 0.8);
        settings.edit([&](AppSettings &draft) { draft.speech.local = local; return true; });
        QCOMPARE(measuredSpeedTestSeconds(settings.dictationSnapshot().speech.local, model.id), std::nullopt);
        local.runsOn = {QStringLiteral("cpu"), {}};
        settings.edit([&](AppSettings &draft) { draft.speech.local = local; return true; });
        QCOMPARE(measuredSpeedTestSeconds(settings.dictationSnapshot().speech.local, model.id), std::optional(0.8));
        {
            QFile file(models.modelPath(model));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.resize(model.sizeBytes));
        }
        speech.services.order = {QStringLiteral("local")};
        QVERIFY(setup.modelState(model, speech).inUse);
        speech.services.order = {QStringLiteral("claude")};
        QVERIFY(!setup.modelState(model, speech).inUse);
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        auto *provider = qobject_cast<LocalSpeechTranscriber *>(providers.speechProvider("local"));
        emit provider->speedTestFinished(model.id, 0, "speed problem");
        QCOMPARE(setup.modelState(model, speech).problem, QString("speed problem"));
#endif
        emit models.downloadFailed(model.id, "download problem");
        QCOMPARE(setup.modelState(model, speech).problem, QString("download problem"));
    }

    void fitFollowsWhereModelsRun()
    {
        // A 2 GiB card and 16 GB of free RAM: Qwen3-ASR's 2.95 GB is too
        // large for the card, but fits on the CPU.
        HardwareSummary hardware;
        hardware.profile.accelerator = HardwareProfile::Accelerator::DedicatedGpu;
        hardware.profile.availableRamBytes = 16 * gb;
        hardware.profile.gpuMemoryBytes = 2 * gib;
        LocalSpeechEngine::Device card;
        card.id = QStringLiteral("0000:01:00.0");
        card.kind = QStringLiteral("vulkan");
        card.type = LocalSpeechEngine::Device::Type::Gpu;
        card.memoryTotalBytes = 2 * gib;
        hardware.gpus = {card};
        const LocalModel &qwen = *findLocalModel(QStringLiteral("qwen3-asr"));

        QCOMPARE(modelFit(qwen, runsOnProfile(hardware, {})), ModelFit::TooLarge);
        QCOMPARE(modelFit(qwen, runsOnProfile(hardware, {QStringLiteral("cpu"), {}})), ModelFit::Fits);
        QCOMPARE(modelFit(qwen, runsOnProfile(hardware, {QStringLiteral("vulkan"), card.id})), ModelFit::TooLarge);
        // A card saved before backends could be chosen is still that card.
        QCOMPARE(modelFit(qwen, runsOnProfile(hardware, {QStringLiteral("auto"), card.id})), ModelFit::TooLarge);

        // LocalSetup fits against the Runs on choice it is given.
        QTemporaryDir directory;
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);
        LocalSetupTestAccess::setHardware(setup, hardware);
        LocalSpeechSettings local;
        QCOMPARE(setup.fit(qwen, local), std::optional(ModelFit::TooLarge));
        local.runsOn = {QStringLiteral("cpu"), {}};
        QCOMPARE(setup.fit(qwen, local), std::optional(ModelFit::Fits));
    }

    // Acceleration offers Automatic, the CPU and each backend a card here
    // reaches, and keeps a saved one no card reaches; a backend names its own
    // cards once each, and a card saved under Automatic, or gone, stays listed.
    void accelerationAndCardChoicesFollowTheCardsHere()
    {
        const QString nvidia = QStringLiteral("0000:01:00.0");
        const QString amd = QStringLiteral("0000:0e:00.0");
        const QList<LocalGpu> gpus{{QStringLiteral("vulkan"), nvidia, QStringLiteral("RTX 4070")},
                                   {QStringLiteral("vulkan"), amd, QStringLiteral("Radeon")},
                                   {QStringLiteral("cuda"), nvidia, QStringLiteral("RTX 4070")}};
        const auto backends = [](const QList<LocalAccelerationChoice> &choices) {
            QStringList ids;
            for (const LocalAccelerationChoice &choice : choices) {
                ids.append(choice.backend + (choice.available ? QString() : QStringLiteral(" (unavailable)")));
            }
            return ids;
        };
        const auto cards = [](const QList<LocalGpu> &choices) {
            QStringList ids;
            for (const LocalGpu &card : choices) {
                ids.append(card.deviceId + QLatin1Char('=') + card.description);
            }
            return ids;
        };

        QCOMPARE(backends(localAccelerationChoices(gpus, {})), QStringList({"auto", "cpu", "vulkan", "cuda"}));
        QCOMPARE(backends(localAccelerationChoices({}, {QStringLiteral("metal"), {}})),
                 QStringList({"auto", "cpu", "metal (unavailable)"}));

        QVERIFY(localGraphicsCardChoices(gpus, {}).isEmpty());
        QVERIFY(localGraphicsCardChoices(gpus, {QStringLiteral("cpu"), {}}).isEmpty());
        QCOMPARE(cards(localGraphicsCardChoices(gpus, {QStringLiteral("vulkan"), {}})),
                 QStringList({nvidia + "=RTX 4070", amd + "=Radeon"}));
        QCOMPARE(cards(localGraphicsCardChoices(gpus, {QStringLiteral("auto"), amd})), QStringList({amd + "=Radeon"}));
        QCOMPARE(cards(localGraphicsCardChoices(gpus, {QStringLiteral("cuda"), amd})),
                 QStringList({nvidia + "=RTX 4070", amd + "="}));
    }

    // Cancelling a pull ends it without an error to show.
    void cancellingAPullEndsItQuietly()
    {
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);
        QSignalSpy changed(&setup, &LocalSetup::changed);

        setup.pullCleanupModel(QStringLiteral("gemma4:e4b"));
        QVERIFY(setup.pull().running);
        setup.cancelPull();
        QVERIFY(!setup.pull().running);
        QVERIFY(setup.pull().error.isEmpty());
        QCOMPARE(changed.count(), 2);
        QTest::qWait(100);
        QVERIFY(!setup.pull().running);
        QVERIFY(setup.pull().error.isEmpty());
        QCOMPARE(changed.count(), 2);
    }

#ifdef Q_OS_UNIX
    // A model whose file could not be deleted keeps its speed result and
    // stays the one dictation uses.
    void aModelThatStaysKeepsItsSettings()
    {
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);
        const LocalModel &model = *findLocalModel("cohere");
        {
            QFile file(models.modelPath(model));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.resize(model.sizeBytes));
        }
        const QString speedKey = localSpeedTestKey(model.id, {QStringLiteral("cpu"), {}});
        settings.edit([&](AppSettings &draft) {
            localModelId(draft.speech) = model.id;
            draft.speech.local.speedTestSeconds.insert(speedKey, 0.8);
            return true;
        });
        QFile folder(QFileInfo(models.modelPath(model)).absolutePath());
        const QFile::Permissions writable = folder.permissions();
        QVERIFY(folder.setPermissions(QFile::ReadOwner | QFile::ExeOwner));
        const auto restore = qScopeGuard([&] { folder.setPermissions(writable); });

        QVERIFY(!setup.removeModel(model));
        const SpeechSettings speech = settings.dictationSnapshot().speech;
        QCOMPARE(localModelId(speech), model.id);
        QCOMPARE(speech.local.speedTestSeconds.value(speedKey), 0.8);
    }
#endif

    void catalogEntriesArePinned()
    {
        for (const LocalModel &model : localModelCatalog()) {
            QCOMPARE(model.revision.size(), 40);
            QCOMPARE(model.sha256.size(), 64);
            QVERIFY(model.sizeBytes > 0);
        }
    }

    void fitComparesModelMemoryWithTheBudget()
    {
        // Parakeet needs 731 MB x 1.35 = 0.99 GB.
        const LocalModel &parakeet = *findLocalModel(QStringLiteral("parakeet"));
        HardwareProfile cpu;
        cpu.availableRamBytes = 5 * gb;      // budget 5 GB - 2 GiB = 2.85 GB
        QCOMPARE(modelFit(parakeet, cpu), ModelFit::Fits);
        cpu.availableRamBytes = 3 * gb;      // budget 0.85 GB
        QCOMPARE(modelFit(parakeet, cpu), ModelFit::TooLarge);
        cpu.availableRamBytes = 3.5 * gb;    // budget 1.35 GB
        QCOMPARE(modelFit(parakeet, cpu), ModelFit::Tight);

        // A dedicated GPU keeps 1 GiB back and ignores system RAM. Voxtral
        // Small needs 23.1 GB: too large for a 16 GB card, tight on a 24 GB one.
        const LocalModel &voxtral = *findLocalModel(QStringLiteral("voxtral-small"));
        HardwareProfile gpu;
        gpu.accelerator = HardwareProfile::Accelerator::DedicatedGpu;
        gpu.availableRamBytes = 64 * gb;
        gpu.gpuMemoryBytes = 16 * gib;
        QCOMPARE(modelFit(voxtral, gpu), ModelFit::TooLarge);
        gpu.gpuMemoryBytes = 24 * gib;
        QCOMPARE(modelFit(voxtral, gpu), ModelFit::Tight);

        // A 36 GB Mac: min(28 GB, 0.85 x (32.4 GB - 2.68 GB)) = 25.26 GB.
        QVERIFY(qAbs(double(memoryBudgetBytes(m4Max36GB())) - 25.258e9) < 0.01e9);
        QCOMPARE(modelFit(voxtral, m4Max36GB()), ModelFit::Tight);
    }

    void speedIsEstimatedFromTheNearestReferenceMachine()
    {
        const LocalModel &parakeet = *findLocalModel(QStringLiteral("parakeet"));
        const std::optional<SpeedEstimate> laptop = estimatedSpeed(parakeet, laptop4750u());
        QVERIFY(laptop);
        QCOMPARE(laptop->secondsFor10sSpeech, 10.0 / 26.01);
        QVERIFY(laptop->measuredOnThisChip);

        HardwareProfile otherPc = laptop4750u();
        otherPc.chipName = QStringLiteral("Intel Core i5-6200U");
        QVERIFY(!estimatedSpeed(parakeet, otherPc)->measuredOnThisChip);

        QCOMPARE(estimatedSpeed(parakeet, m4Max36GB())->secondsFor10sSpeech, 10.0 / 228.39);
        // transcribe.cpp never ran Voxtral Small on the 4750U.
        QVERIFY(!estimatedSpeed(*findLocalModel(QStringLiteral("voxtral-small")), otherPc));
    }

    void suggestionPrefersTheMostAccurateQuickStreamingModelThatFits()
    {
        // Values from the approved prototype's four machines.
        QCOMPARE(suggestedLocalModel(laptop4750u(), QStringLiteral("en")).id, QStringLiteral("parakeet"));
        QCOMPARE(suggestedLocalModel(m4Max36GB(), QStringLiteral("en")).id, QStringLiteral("parakeet"));

        // 4 GB, no GPU, little of it free: nothing fits, so the smallest model.
        HardwareProfile old;
        old.chipName = QStringLiteral("Intel Core i5-6200U");
        old.systemRamBytes = 4 * gb;
        old.availableRamBytes = 2.2 * gb;
        QCOMPARE(modelFit(*findLocalModel(QStringLiteral("moonshine-small")), old), ModelFit::TooLarge);
        QCOMPARE(suggestedLocalModel(old, QStringLiteral("en")).id, QStringLiteral("moonshine-small"));

        // Parakeet fits only tightly, both Moonshines fit: the more accurate one.
        HardwareProfile small;
        small.availableRamBytes = 3.8 * gb;
        QCOMPARE(modelFit(*findLocalModel(QStringLiteral("parakeet")), small), ModelFit::Tight);
        QCOMPARE(suggestedLocalModel(small, QStringLiteral("en")).id, QStringLiteral("moonshine-medium"));
    }

    // Filipino is "tl" in Speecher and every list; only the hint Qwen3-ASR
    // gets is "fil".
    void filipinoIsOneCodeUntilTheModelNeedsItsOwn()
    {
        QVERIFY(localModelListensFor(*findLocalModel(QStringLiteral("qwen3-asr")), QStringLiteral("tl")));
        QCOMPARE(localModelLanguageHint(QStringLiteral("tl"), {QStringLiteral("en"), QStringLiteral("fil")}),
                 QStringLiteral("fil"));
        QCOMPARE(localModelLanguageHint(QStringLiteral("tl"), {QStringLiteral("tl"), QStringLiteral("fil")}),
                 QStringLiteral("tl"));
    }

    void suggestionListensForTheSpokenLanguage()
    {
        // No streaming model takes German, so the smallest that does; Cohere
        // takes it as a hint but cannot detect a language.
        QCOMPARE(suggestedLocalModel(laptop4750u(), QStringLiteral("de")).id, QStringLiteral("parakeet-v3"));
        QCOMPARE(suggestedLocalModel(laptop4750u(), QStringLiteral("auto")).id, QStringLiteral("whisper-turbo"));
        // Parakeet v3 takes Greek but is never suggested for it.
        QVERIFY(localModelListensFor(*findLocalModel(QStringLiteral("parakeet-v3")), QStringLiteral("el")));
        QCOMPARE(suggestedLocalModel(laptop4750u(), QStringLiteral("el")).id, QStringLiteral("whisper-turbo"));
        QVERIFY(!localModelListensFor(*findLocalModel(QStringLiteral("cohere")), QStringLiteral("auto")));
    }

    void downloadResumesAPartialFileAndVerifiesIt()
    {
        QTemporaryDir dir;
        QTcpServer server;
        const QByteArray content = QByteArray("GGUF") + QByteArray(4096, 'x') + "end";
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
        writePart(store, model, content.left(1000));
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);
        QSignalSpy failed(&store, &LocalModelStore::downloadFailed);

        store.download(model);
        QVERIFY(store.isDownloading(model.id));
        const QByteArray request = serveOnce(server, "206 Partial Content", content.mid(1000));

        QVERIFY(request.startsWith("GET /owner/repo/resolve/abc123/fake.gguf "));
        QVERIFY(asksForRange(request, "bytes=1000-"));
        QVERIFY(finished.wait(5000));
        QCOMPARE(failed.size(), 0);
        QVERIFY(store.isDownloaded(model));
        QVERIFY(!store.isDownloading(model.id));
        QFile file(store.modelPath(model));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
        // Windows will not delete a file something still holds open.
        file.close();
        QVERIFY(!QFile::exists(store.modelPath(model) + QStringLiteral(".part")));

        QVERIFY(store.remove(model));
        QVERIFY(!QFile::exists(store.modelPath(model)));
    }

    void downloadWithTheWrongHashIsDeleted()
    {
        QTemporaryDir dir;
        QTcpServer server;
        const QByteArray content(2048, 'a');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);
        QSignalSpy failed(&store, &LocalModelStore::downloadFailed);

        store.download(model);
        serveOnce(server, "200 OK", QByteArray(2048, 'b'));

        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().at(0).toString(), model.id);
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("damaged")));
        QCOMPARE(finished.size(), 0);
        QVERIFY(!store.isDownloaded(model));
        QVERIFY(!QFile::exists(store.modelPath(model)));
        QVERIFY(!QFile::exists(store.modelPath(model) + QStringLiteral(".part")));
    }

    void interruptedDownloadKeepsWhatArrivedForTheNextTry()
    {
        QTemporaryDir dir;
        QTcpServer server;
        const QByteArray content(4000, 'c');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
        QSignalSpy failed(&store, &LocalModelStore::downloadFailed);

        store.download(model);
        // The server promises the whole file and hangs up after 1500 bytes.
        serveOnce(server, "200 OK", content.left(1500), content.size());

        QVERIFY(failed.wait(5000));
        QCOMPARE(QFileInfo(store.modelPath(model) + QStringLiteral(".part")).size(), 1500);
        QVERIFY(!store.isDownloading(model.id));
    }

    void cancelKeepsThePartialFile()
    {
        QTemporaryDir dir;
        QTcpServer server;
        const QByteArray content(4000, 'd');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
        QSignalSpy progress(&store, &LocalModelStore::downloadProgress);

        store.download(model);
        QVERIFY(server.waitForNewConnection(5000));
        QTcpSocket *socket = server.nextPendingConnection();
        test::readHttpRequest(socket, 2000);
        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 4000\r\n\r\n" + content.left(1200));
        socket->flush();
        QTRY_VERIFY(!progress.isEmpty());
        store.cancel(model.id);

        QVERIFY(!store.isDownloading(model.id));
        QCOMPARE(QFileInfo(store.modelPath(model) + QStringLiteral(".part")).size(), 1200);
    }

    void serverIgnoringTheRangeRestartsFromZero()
    {
        QTemporaryDir dir;
        QTcpServer server;
        const QByteArray content = QByteArray(3000, 'e') + "tail";
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
        writePart(store, model, QByteArray(1000, 'z'));
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);

        store.download(model);
        serveOnce(server, "200 OK", content);

        QVERIFY(finished.wait(5000));
        QFile file(store.modelPath(model));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
    }

    void rangeNotSatisfiableRestartsTheDownload()
    {
        QTemporaryDir dir;
        QTcpServer server;
        const QByteArray content(2500, 'f');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
        writePart(store, model, QByteArray(1000, 'z'));
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);

        store.download(model);
        QVERIFY(asksForRange(serveOnce(server, "416 Range Not Satisfiable", {}), "bytes=1000-"));
        // The retry comes from the event loop, which serveOnce does not run.
        QTRY_VERIFY(server.hasPendingConnections());
        const QByteArray retry = serveOnce(server, "200 OK", content);

        QVERIFY(retry.startsWith("GET "));
        QVERIFY(!retry.toLower().contains("\r\nrange:"));
        QVERIFY(finished.wait(5000));
        QVERIFY(store.isDownloaded(model));
    }

    void cancelDoesNotWaitForAnotherModelsHashCheck()
    {
        QTemporaryDir dir;
        QTcpServer server;
        LocalModelStore store(dir.path(), listenLocally(server));
        // A complete, sparse 512 MiB partial file goes straight to the hash
        // check. Its hash is wrong, which the check only learns at the end.
        LocalModel large = fakeModel({});
        large.id = QStringLiteral("large");
        large.fileName = QStringLiteral("large.gguf");
        large.sizeBytes = qint64(512) << 20;
        {
            QFile part(store.modelPath(large) + QStringLiteral(".part"));
            QVERIFY(part.open(QIODevice::WriteOnly));
            QVERIFY(part.resize(large.sizeBytes));
        }
        const LocalModel other = fakeModel("other");
        QSignalSpy failed(&store, &LocalModelStore::downloadFailed);

        QElapsedTimer timer;
        timer.start();
        store.download(large);
        store.download(other);
        store.cancel(other.id);
        const qint64 cancelMs = timer.elapsed();
        QVERIFY(failed.wait(60000));
        const qint64 hashMs = timer.elapsed();

        QCOMPARE(failed.first().at(0).toString(), large.id);
        QVERIFY2(cancelMs * 4 < hashMs, qPrintable(QStringLiteral("cancel took %1 ms of a %2 ms hash check")
                                                       .arg(cancelMs).arg(hashMs)));
    }

#ifdef SPEECHER_WITH_LOCAL_SPEECH
    // Runs a real model when SPEECHER_TEST_LOCAL_MODEL names a catalog id and
    // SPEECHER_TEST_LOCAL_MODEL_DIR holds its file.
    void localModelTranscribesTheSpeedTestClip()
    {
        const QString modelId = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL");
        const QString modelDir = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL_DIR");
        if (modelId.isEmpty() || modelDir.isEmpty()) {
            QSKIP("Set SPEECHER_TEST_LOCAL_MODEL and SPEECHER_TEST_LOCAL_MODEL_DIR to run a real model.");
        }
        const LocalModel *model = findLocalModel(modelId);
        QVERIFY(model);
        LocalModelStore store(modelDir, QUrl());
        QVERIFY2(store.isDownloaded(*model), qPrintable(store.modelPath(*model)));
        LocalSpeechTranscriber transcriber(store, std::make_shared<LocalSpeechWorker>());
        QSignalSpy partials(&transcriber, &SpeechTranscriber::partialTranscript);
        QSignalSpy transcripts(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        QSignalSpy runsOn(&transcriber, &LocalSpeechTranscriber::runsOnChanged);
        QSignalSpy timings(&transcriber, &SpeechTranscriber::attemptSegments);

        SpeechSettings settings;
        localModelId(settings) = modelId;
        // Local models get no key terms. Given to Whisper as a prompt, terms
        // like these replaced whole clips with other text.
        settings.vocabulary = {QStringLiteral("Speecher"), QStringLiteral("Kirigami")};
        QVERIFY(!transcriber.prepare(settings, {}));
        QFile clip(QStringLiteral(":/speedtest/librispeech-6930-75918-0018.s16le"));
        QVERIFY(clip.open(QIODevice::ReadOnly));
        const QByteArray pcm = clip.readAll();

        transcriber.startAttempt(1, settings, {});
        // 100 ms chunks at the pace the microphone delivers them.
        constexpr qsizetype chunkBytes = 3200;
        for (qsizetype offset = 0; offset < pcm.size(); offset += chunkBytes) {
            transcriber.sendAudio(1, pcm.mid(offset, chunkBytes));
            QTest::qWait(100);
        }
        QElapsedTimer afterStop;
        afterStop.start();
        transcriber.finishInput(1);
        QVERIFY(completed.wait(120000));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(transcripts.size(), 1);
        const QString text = transcripts.first().at(1).toString();
        qInfo().noquote() << modelId << "transcript:" << text;
        qInfo() << "partials" << partials.size() << "; transcript" << afterStop.elapsed()
                << "ms after the last of" << pcm.size() / 32 << "ms of audio";
        QVERIFY(text.contains(QStringLiteral("tempest"), Qt::CaseInsensitive));
        QVERIFY(text.contains(QStringLiteral("security everywhere"), Qt::CaseInsensitive));
        // Dictation never asks for timings, which slow a model and can change
        // its text.
        QVERIFY(timings.isEmpty());

        // Transcribe does; models that time their segments do so within the
        // clip, in order.
        transcriber.startAttempt(2, settings, {.timedSegments = true});
        transcriber.sendAudio(2, pcm);
        transcriber.finishInput(2);
        QVERIFY(completed.wait(120000));
        QCOMPARE(failed.size(), 0);
        if (!timings.isEmpty()) {
            const auto segments = timings.first().at(1).value<QList<TranscriptSegment>>();
            qInfo() << segments.size() << "timed segments";
            qint64 previousEndMs = 0;
            for (const TranscriptSegment &segment : segments) {
                qInfo() << segment.startMs << segment.endMs << segment.text;
                QVERIFY(segment.startMs >= previousEndMs - 1 && segment.startMs <= segment.endMs);
                previousEndMs = segment.endMs;
            }
            QVERIFY(previousEndMs <= pcm.size() / 32 + 1000);
        }

        QSignalSpy speed(&transcriber, &LocalSpeechTranscriber::speedTestFinished);
        // SPEECHER_TEST_LOCAL_RUNS_ON picks what the Speed Test runs on: a
        // backend ("auto", "cpu", "vulkan", "cuda"), or one with a device after
        // the first colon ("cuda:0000:01:00.0"; PCI ids have colons of their own).
        const QString runsOnId = qEnvironmentVariable("SPEECHER_TEST_LOCAL_RUNS_ON", QStringLiteral("auto"));
        const qsizetype colon = runsOnId.indexOf(QLatin1Char(':'));
        transcriber.runSpeedTest(modelId, colon < 0 ? LocalRunsOn{runsOnId, QString()}
                                                    : LocalRunsOn{runsOnId.left(colon), runsOnId.mid(colon + 1)});
        QVERIFY(speed.wait(120000));
        QVERIFY2(speed.first().at(2).toString().isEmpty(), qPrintable(speed.first().at(2).toString()));
        if (!runsOn.isEmpty()) {
            qInfo().noquote() << "runs on:" << runsOn.last().at(0).toString();
        }
        qInfo() << "speed test:" << speed.first().at(1).toDouble() << "s for 10 s of speech";
        QVERIFY(speed.first().at(1).toDouble() > 0);
    }

    // An explicit backend that is missing fails the load rather than running
    // somewhere else. No model file is needed to get that far.
    void anUnavailableBackendFailsTheLoad()
    {
        const QList<LocalSpeechEngine::Device> devices = LocalSpeechEngine::devices();
        if (std::any_of(devices.cbegin(), devices.cend(), [](const LocalSpeechEngine::Device &device) {
                return device.kind == QStringLiteral("rocm");
            })) {
            QSKIP("This computer runs ROCm.");
        }
        const std::unique_ptr<LocalSpeechEngine> engine = LocalSpeechEngine::create();
        QString error;
        QVERIFY(!engine->load(QStringLiteral("/nonexistent.gguf"), {QStringLiteral("rocm"), {}}, &error));
        QCOMPARE(error, QStringLiteral("ROCm is not available on this computer. Choose where the model "
                                       "runs in Services › This computer."));
        QVERIFY(engine->runsOnDescription().isEmpty());
    }

    // An attempt that starts while the worker is busy must still get every
    // chunk, including the ones sent before its begin() runs. The Speed Test
    // keeps the worker busy so the calls below all queue behind it.
    void attemptStartedWhileTheWorkerIsBusyKeepsItsAudio()
    {
        const QString modelId = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL");
        const QString modelDir = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL_DIR");
        if (modelId.isEmpty() || modelDir.isEmpty()) {
            QSKIP("Set SPEECHER_TEST_LOCAL_MODEL and SPEECHER_TEST_LOCAL_MODEL_DIR to run a real model.");
        }
        LocalModelStore store(modelDir, QUrl());
        LocalSpeechTranscriber transcriber(store, std::make_shared<LocalSpeechWorker>());
        QSignalSpy transcripts(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        SpeechSettings settings;
        localModelId(settings) = modelId;
        QFile clip(QStringLiteral(":/speedtest/librispeech-6930-75918-0018.s16le"));
        QVERIFY(clip.open(QIODevice::ReadOnly));
        const QByteArray pcm = clip.readAll();

        transcriber.runSpeedTest(modelId, {});
        transcriber.startAttempt(1, settings, {});
        transcriber.sendAudio(1, pcm.left(3200));
        transcriber.cancelAttempt(1);
        transcriber.startAttempt(2, settings, {});
        transcriber.sendAudio(2, pcm);
        transcriber.finishInput(2);

        QVERIFY(completed.wait(120000));
        QCOMPARE(completed.first().at(0).toULongLong(), quint64(2));
        QVERIFY2(transcripts.first().at(1).toString().contains(QStringLiteral("security everywhere"),
                                                                Qt::CaseInsensitive),
                 qPrintable(transcripts.first().at(1).toString()));
    }

    void streamSplitKeepsPartWordsTentative()
    {
        // Mid-word commit: "wor" is not a whole word yet.
        StreamSplit split = splitStreamText({QStringLiteral("hello wor"), QStringLiteral("ld again")}, 0);
        QCOMPARE(split.finalWords, QStringLiteral("hello"));
        QCOMPARE(split.partial, QStringLiteral(" world again"));
        QCOMPARE(split.finalChars, 5);

        // The committed word is complete once a space follows it.
        split = splitStreamText({QStringLiteral("hello world "), QStringLiteral("again")}, 5);
        QCOMPARE(split.finalWords, QStringLiteral(" world"));
        QCOMPARE(split.partial, QStringLiteral(" again"));
        QCOMPARE(split.finalChars, 11);

        // Committed text that grows only inside the last word adds no final.
        split = splitStreamText({QStringLiteral("hello world aga"), QStringLiteral("in")}, 11);
        QVERIFY(split.finalWords.isEmpty());
        QCOMPARE(split.partial, QStringLiteral(" again"));
        QCOMPARE(split.finalChars, 11);

        // Nothing committed yet: all of it is tentative.
        split = splitStreamText({{}, QStringLiteral("the night")}, 0);
        QVERIFY(split.finalWords.isEmpty());
        QCOMPARE(split.partial, QStringLiteral("the night"));
    }

    void shorterRawFinalDoesNotDiscardCommittedStreamText()
    {
        const QString committed = QStringLiteral(
            "The night was clear, starlit and splendid. The tempest had passed away, "
            "and the sweet influences of the evening had restored life, peace and security everywhere.");
        const QString truncated = QStringLiteral(
            "The night was clear, starlit and splendid. The tempest had passed away.");
        TranscriptState transcript;
        const StreamSplit split = splitStreamText({committed, {}}, 0);
        transcript.commitFinal(split.finalWords);
        transcript.setPartial(split.partial);
        QCOMPARE(transcript.text(), committed);

        // Finalization must not replace the already committed sentence with
        // a shorter raw model hypothesis.
        transcript.replaceFinals(finalStreamText(committed, truncated));
        QCOMPARE(transcript.text(), committed);

        const QString revised = QStringLiteral(
            "The night was clear, starlit, and splendid. The tempest had passed away, "
            "and the sweet influences of the evening had restored life, peace, and security everywhere.");
        QCOMPARE(finalStreamText(committed, revised), revised);
    }

    void recordingGetsOneFinalPerUtterance_data()
    {
        QTest::addColumn<bool>("streams");
        QTest::newRow("streaming model") << true;
        QTest::newRow("batch model") << false;
    }

    // Each utterance a recording marks is one final, the last one's at the
    // end, and no whole-attempt transcript replaces them.
    void recordingGetsOneFinalPerUtterance()
    {
        QFETCH(bool, streams);
        QTemporaryDir dir;
        LocalModelStore store(dir.path(), QUrl());
        QVERIFY(fakeDownload(store, QStringLiteral("moonshine-small")));
        FakeModelCounts counts;
        LocalSpeechTranscriber transcriber(
            store, std::make_shared<LocalSpeechWorker>(std::make_unique<FakeEngine>(counts, streams)));
        QSignalSpy connected(&transcriber, &SpeechTranscriber::attemptConnected);
        QSignalSpy finals(&transcriber, &SpeechTranscriber::finalTranscript);
        QSignalSpy transcripts(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        SpeechSettings settings;
        localModelId(settings) = QStringLiteral("moonshine-small");

        transcriber.startAttempt(1, settings, {.cutIntoUtterances = true});
        // A recording counts as started once the model is loaded.
        QVERIFY(connected.wait(2000));
        transcriber.sendAudio(1, quiet());
        transcriber.beginUtterance(1);
        transcriber.sendAudio(1, spoken(1));
        transcriber.sendAudio(1, spoken(2));
        transcriber.endUtterance(1);
        transcriber.sendAudio(1, quiet());
        QTRY_COMPARE(texts(finals), QStringList{QStringLiteral("w1 w2")});
        transcriber.beginUtterance(1);
        transcriber.sendAudio(1, spoken(3));
        transcriber.finishInput(1);
        QVERIFY(completed.wait(2000));
        QCOMPARE(texts(finals), (QStringList{QStringLiteral("w1 w2"), QStringLiteral("w3")}));
        QVERIFY(transcripts.isEmpty());
        QVERIFY(failed.isEmpty());

        // Dictation is as before: a streaming model's runs of words as they
        // are committed, then the whole text in their place, and a batch
        // model's text at the end.
        finals.clear();
        transcriber.startAttempt(2, settings, {});
        transcriber.sendAudio(2, spoken(1));
        transcriber.endUtterance(2);
        transcriber.sendAudio(2, spoken(2));
        transcriber.finishInput(2);
        QVERIFY(completed.wait(2000));
        QCOMPARE(texts(transcripts), QStringList{QStringLiteral("w1 w2")});
        QCOMPARE(finals.isEmpty(), !streams);
        if (streams) {
            QCOMPARE(texts(finals).join(QLatin1Char(' ')).simplified(), QStringLiteral("w1 w2"));
        }
        QVERIFY(connected.size() == 1 && failed.isEmpty());
    }

    // A recording's two sides and dictation load the model once, with a
    // session each, and only one model at a time.
    void transcribersShareOneLoadedModel()
    {
        QTemporaryDir dir;
        LocalModelStore store(dir.path(), QUrl());
        QVERIFY(fakeDownload(store, QStringLiteral("moonshine-small")));
        QVERIFY(fakeDownload(store, QStringLiteral("moonshine-medium")));
        FakeModelCounts counts;
        const auto worker = std::make_shared<LocalSpeechWorker>(std::make_unique<FakeEngine>(counts, true));
        LocalSpeechTranscriber me(store, worker);
        LocalSpeechTranscriber them(store, worker);
        LocalSpeechTranscriber dictation(store, worker);
        QSignalSpy myFinals(&me, &SpeechTranscriber::finalTranscript);
        QSignalSpy theirFinals(&them, &SpeechTranscriber::finalTranscript);
        QSignalSpy dictationFailed(&dictation, &SpeechTranscriber::failed);
        QSignalSpy dictationDone(&dictation, &SpeechTranscriber::attemptCompleted);
        QSignalSpy speedTest(&dictation, &LocalSpeechTranscriber::speedTestFinished);
        QSignalSpy runsOn(&dictation, &LocalSpeechTranscriber::runsOnChanged);
        SpeechSettings settings;
        localModelId(settings) = QStringLiteral("moonshine-small");

        me.startAttempt(1, settings, {.cutIntoUtterances = true});
        them.startAttempt(1, settings, {.cutIntoUtterances = true});
        me.beginUtterance(1);
        them.beginUtterance(1);
        me.sendAudio(1, spoken(1));
        them.sendAudio(1, spoken(2));
        me.sendAudio(1, spoken(3));
        me.endUtterance(1);
        them.endUtterance(1);
        QTRY_COMPARE(texts(myFinals), QStringList{QStringLiteral("w1 w3")});
        QTRY_COMPARE(texts(theirFinals), QStringList{QStringLiteral("w2")});
        QCOMPARE(counts.loads.load(), 1);
        QCOMPARE(counts.sessions.load(), 2);
        // Every transcriber hears where the model runs, whoever loaded it.
        QCOMPARE(runsOn.size(), 1);
        QCOMPARE(runsOn.first().at(0).toString(), QStringLiteral("Fake (CPU)"));

        // The Speed Test waits for the recording.
        dictation.runSpeedTest(QStringLiteral("moonshine-small"), {});
        QCOMPARE(speedTest.first().at(2).toString(),
                 QStringLiteral("Finish dictating or recording, then test the speed again."));

        // Another model can't load in place of the one the recording holds.
        SpeechSettings otherModel = settings;
        localModelId(otherModel) = QStringLiteral("moonshine-medium");
        dictation.startAttempt(1, otherModel, {});
        QVERIFY(dictationFailed.wait(2000));
        QCOMPARE(dictationFailed.first().at(0).value<SpeechFailure>().message,
                 QStringLiteral("This computer is already transcribing with another model, and runs one model "
                                "at a time."));
        QCOMPARE(counts.loads.load(), 1);

        // Once the recording ends, it can.
        me.finishInput(1);
        them.finishInput(1);
        QTRY_VERIFY(!worker->attemptRunning());
        dictation.startAttempt(2, otherModel, {});
        dictation.sendAudio(2, spoken(4));
        dictation.finishInput(2);
        QVERIFY(dictationDone.wait(2000));
        QCOMPARE(counts.loads.load(), 2);
    }

    // transcribe.cpp runs two streams on one loaded model, interleaved, each
    // its own; and the second takes far less memory than the model. Runs a
    // streaming model when SPEECHER_TEST_LOCAL_MODEL names one.
    void twoStreamsShareARealModel()
    {
        const QString modelId = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL");
        const QString modelDir = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL_DIR");
        if (modelId.isEmpty() || modelDir.isEmpty()) {
            QSKIP("Set SPEECHER_TEST_LOCAL_MODEL and SPEECHER_TEST_LOCAL_MODEL_DIR to run a real model.");
        }
        const LocalModel *model = findLocalModel(modelId);
        QVERIFY(model);
        LocalModelStore store(modelDir, QUrl());
        const auto worker = std::make_shared<LocalSpeechWorker>();
        LocalSpeechTranscriber me(store, worker);
        LocalSpeechTranscriber them(store, worker);
        QSignalSpy myFinals(&me, &SpeechTranscriber::finalTranscript);
        QSignalSpy theirFinals(&them, &SpeechTranscriber::finalTranscript);
        QSignalSpy myConnected(&me, &SpeechTranscriber::attemptConnected);
        QSignalSpy theirConnected(&them, &SpeechTranscriber::attemptConnected);
        QSignalSpy completed(&them, &SpeechTranscriber::attemptCompleted);
        SpeechSettings settings;
        localModelId(settings) = modelId;
        settings.local.runsOn = {QStringLiteral("cpu"), {}};
        QFile clip(QStringLiteral(":/speedtest/librispeech-6930-75918-0018.s16le"));
        QVERIFY(clip.open(QIODevice::ReadOnly));
        const QByteArray pcm = clip.readAll();

#ifdef Q_OS_LINUX
        const qint64 beforeLoad = residentBytes();
#endif
        me.startAttempt(1, settings, {.cutIntoUtterances = true});
        QVERIFY(myConnected.wait(60000));
#ifdef Q_OS_LINUX
        const qint64 oneSession = residentBytes();
#endif
        them.startAttempt(1, settings, {.cutIntoUtterances = true});
        QVERIFY(theirConnected.wait(60000));
#ifdef Q_OS_LINUX
        const qint64 twoSessions = residentBytes();
#endif
        me.beginUtterance(1);
        them.beginUtterance(1);
        // Both streams run at once: each side gets the whole clip, the two
        // fed in turn.
        for (qsizetype offset = 0; offset < pcm.size(); offset += kSpeechChunkBytes) {
            me.sendAudio(1, pcm.mid(offset, kSpeechChunkBytes));
            them.sendAudio(1, pcm.mid(offset, kSpeechChunkBytes));
            QTest::qWait(10);
        }
        me.endUtterance(1);
        them.endUtterance(1);
        QTRY_COMPARE_WITH_TIMEOUT(myFinals.size(), 1, 60000);
        QTRY_COMPARE_WITH_TIMEOUT(theirFinals.size(), 1, 60000);
        me.finishInput(1);
        them.finishInput(1);
        QVERIFY(completed.wait(60000));
        for (const QString &text : {texts(myFinals).first(), texts(theirFinals).first()}) {
            qInfo().noquote() << "final:" << text;
            QVERIFY2(text.contains(QStringLiteral("tempest"), Qt::CaseInsensitive), qPrintable(text));
            QVERIFY2(text.contains(QStringLiteral("security everywhere"), Qt::CaseInsensitive), qPrintable(text));
        }
#ifdef Q_OS_LINUX
        // Each stream's own working memory comes on top, as it runs.
        qInfo() << "resident MB: before" << beforeLoad / 1000000 << "one session" << oneSession / 1000000
                << "two sessions" << twoSessions / 1000000 << "both streamed" << residentBytes() / 1000000
                << "model file" << model->sizeBytes / 1000000;
        QVERIFY(oneSession - beforeLoad > model->sizeBytes / 2);
        QVERIFY(twoSessions - oneSession < model->sizeBytes / 10);
#endif
    }

    void missingModelFailsWithAnActionableMessage()
    {
        QTemporaryDir dir;
        LocalModelStore store(dir.path(), QUrl());
        LocalSpeechTranscriber transcriber(store, std::make_shared<LocalSpeechWorker>());
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        SpeechSettings settings;

        const std::optional<ProviderFailure> prepared = transcriber.prepare(settings, {});
        QVERIFY(prepared);
        QCOMPARE(prepared->kind, ProviderFailureKind::Unavailable);
        transcriber.startAttempt(7, settings, {});
        QVERIFY(failed.wait(2000));
        const SpeechFailure failure = failed.first().at(0).value<SpeechFailure>();
        QCOMPARE(failure.attemptId, quint64(7));
        QVERIFY(failure.message.contains(QStringLiteral("Services › This computer")));
        QCOMPARE(failure.kind, ProviderFailureKind::Unavailable);
    }
#endif
};

} // namespace

int runLocalModelsTests(int argc, char **argv)
{
    LocalModelsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_local_models.moc"
