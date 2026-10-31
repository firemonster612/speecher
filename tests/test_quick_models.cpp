#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_local_setup.h"
#include "common/test_suites.h"
#include "common/test_system_singletons.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/NetworkReachability.h"
#include "app/ProviderAvailability.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/quick/host/QuickAccounts.h"
#include "frontend/quick/host/QuickAudio.h"
#include "frontend/quick/host/QuickLibrary.h"
#include "frontend/quick/host/QuickModels.h"
#include "frontend/quick/host/QuickNavigation.h"
#include "frontend/quick/host/QuickSettings.h"
#include "providers/LocalModelStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>

// The UI's QML modules are linked into the tests by test_quick_settings.cpp,
// which imports their plugins for the whole binary.

using namespace speecher;

namespace {

constexpr quint64 gb = 1000ull * 1000 * 1000;

// The entry with this id in a list of maps.
QVariantMap entry(const QVariantList &list, const QString &id)
{
    for (const QVariant &item : list) {
        if (item.toMap().value(QStringLiteral("id")).toString() == id) {
            return item.toMap();
        }
    }
    return {};
}

HardwareSummary laptop()
{
    HardwareSummary hardware;
    hardware.profile.accelerator = HardwareProfile::Accelerator::IntegratedGpu;
    hardware.profile.chipName = QStringLiteral("AMD Ryzen 7 PRO 4750U with Radeon Graphics");
    hardware.profile.systemRamBytes = 16 * gb;
    hardware.profile.availableRamBytes = 12 * gb;
    hardware.cpuThreads = 16;
    LocalSpeechEngine::Device card;
    card.id = QStringLiteral("0000:04:00.0");
    card.kind = QStringLiteral("vulkan");
    card.description = QStringLiteral("AMD Radeon Graphics");
    card.type = LocalSpeechEngine::Device::Type::IntegratedGpu;
    hardware.gpus = {card};
    return hardware;
}

// Answers one request on server with a JSON body.
void serveJson(QTcpServer &server, const QByteArray &body)
{
    QVERIFY(server.waitForNewConnection(5000));
    QTcpSocket *socket = server.nextPendingConnection();
    test::readHttpRequest(socket, 2000);
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
    socket->flush();
    socket->waitForBytesWritten(2000);
    socket->disconnectFromHost();
}

// Puts a file of the model's size where the store looks, which is all a
// finished download is to it.
void placeDownloaded(ApplicationController &controller, const QString &id)
{
    const LocalModel &model = *findLocalModel(id);
    const QString path = controller.localModelStore()->modelPath(model);
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(model.sizeBytes));
    file.close();
    emit controller.localSetup()->changed();
}

// The step in setup's choices with this id.
QVariantMap setupStep(QObject *choices, const QString &id)
{
    return entry(choices->property("steps").toList(), id);
}

} // namespace

// The UI's Models singleton: core's providers, the Local Model catalog, the
// hardware, runners and endpoint checks as plain values, and its operations
// through LocalSetup and the store.
class QuickModelsTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
        // Nothing here reads this user's Claude Code sign-in.
        settings.raw().setValue(SettingsKeys::ClaudeCredentialsPath, m_home.filePath(QStringLiteral("claude.json")));
        failOnUnexpectedWarnings();
    }

    // Each provider says where it stands as core judges it: a sign-in nobody
    // has seen is unknown, one seen missing is signed out with core's
    // sentence, and a Custom Endpoint with no server says so.
    void providersCarryTheirStateAndCoresProblem()
    {
        ApplicationController controller(true);
        // Online, as the machine running the tests may not be.
        controller.networkReachability()->setReachability(Reachability::Online);
        QuickModels models(&controller);
        QSignalSpy changed(&models, &QuickModels::changed);

        QVariantMap codex = entry(models.speechServices(), QStringLiteral("codex"));
        QCOMPARE(codex.value(QStringLiteral("label")).toString(), QStringLiteral("ChatGPT"));
        QCOMPARE(codex.value(QStringLiteral("state")).toString(), QStringLiteral("unknown"));
        QVERIFY(codex.value(QStringLiteral("problem")).toString().isEmpty());
        const QVariantMap endpoint = entry(models.speechServices(), QStringLiteral("endpoint"));
        QCOMPARE(endpoint.value(QStringLiteral("state")).toString(), QStringLiteral("noServer"));
        QCOMPARE(endpoint.value(QStringLiteral("problem")).toString(), QStringLiteral("No server URL is set."));

        controller.providerAvailability()->noteSignIn(QStringLiteral("codex"), false);
        controller.providerAvailability()->noteSignIn(QStringLiteral("claude"), true);
        QVERIFY(changed.count() >= 1);
        codex = entry(models.speechServices(), QStringLiteral("codex"));
        QCOMPARE(codex.value(QStringLiteral("state")).toString(), QStringLiteral("signedOut"));
        QCOMPARE(codex.value(QStringLiteral("problem")).toString(), QStringLiteral("Not signed in to ChatGPT."));
        QCOMPARE(entry(models.speechServices(), QStringLiteral("claude")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("ready"));

        const QVariantMap openAi = entry(models.refinementProviders(), QStringLiteral("openai"));
        QCOMPARE(openAi.value(QStringLiteral("label")).toString(), QStringLiteral("ChatGPT"));
        QVERIFY(openAi.value(QStringLiteral("readsScreenshots")).toBool());
        QVERIFY(!entry(models.refinementProviders(), QStringLiteral("local")).value(QStringLiteral("readsScreenshots")).toBool());
        // The services' bars, numbers only.
        const QVariantList bars = models.speechRatings().value(QStringLiteral("codex")).toMap().value(QStringLiteral("bars")).toList();
        QCOMPARE(bars.size(), 2);
        QCOMPARE(bars.at(0).toMap().value(QStringLiteral("measure")).toString(), QStringLiteral("accuracy"));
        QCOMPARE(bars.at(0).toMap().value(QStringLiteral("value")).toDouble(), 8.5);
    }

    // Each speech service lists the languages core says it listens for, so
    // the UI can show them; Automatic is not one of them.
    void speechServicesCarryTheirLanguages()
    {
        ApplicationController controller(true);
        QuickModels models(&controller);
        const auto languages = [&models](const QString &id) {
            return entry(models.speechServices(), id).value(QStringLiteral("languages")).toStringList();
        };

        QCOMPARE(languages(QStringLiteral("codex")).size(), 67);
        QVERIFY(languages(QStringLiteral("codex")).contains(QStringLiteral("cy")));
        QCOMPARE(languages(QStringLiteral("claude")).size(), 52);
        QVERIFY(!languages(QStringLiteral("claude")).contains(QStringLiteral("cy")));
        QVERIFY(!languages(QStringLiteral("claude")).contains(QStringLiteral("auto")));

        // What the Speech page's fallback pickers show: whether a service
        // offers Automatic, and the language it listens for in the chain.
        AppSettings draft = controller.settings()->dictationSnapshot();
        draft.speech.fallbackLanguages = {{QStringLiteral("codex"), QStringLiteral("cy")}};
        controller.settings()->applySnapshot(draft);
        const QVariantMap codex = entry(models.speechServices(), QStringLiteral("codex"));
        QVERIFY(codex.value(QStringLiteral("detects")).toBool());
        QCOMPARE(codex.value(QStringLiteral("spokenLanguage")).toString(), QStringLiteral("cy"));
        QCOMPARE(entry(models.speechServices(), QStringLiteral("claude")).value(QStringLiteral("spokenLanguage")).toString(),
                 QStringLiteral("en"));
    }

    // Before the probe answers nothing fits, is suggested, estimated or
    // rated on this computer; its answer brings all four and the hardware.
    void theHardwareProbeBringsFitsSuggestionsAndRatings()
    {
        ApplicationController controller(true);
        QuickModels models(&controller);
        const QString parakeet = QStringLiteral("parakeet");

        QVERIFY(!models.hardwareKnown());
        QVERIFY(models.computer().isEmpty());
        QVERIFY(models.suggestedModel().isEmpty());
        QVERIFY(!models.speechRatings().contains(QStringLiteral("local")));
        QVariantMap model = entry(models.catalog(), parakeet);
        QCOMPARE(model.value(QStringLiteral("name")).toString(), QStringLiteral("Parakeet 0.6B"));
        QCOMPARE(model.value(QStringLiteral("rating")).toString(), QStringLiteral("recommended"));
        QCOMPARE(model.value(QStringLiteral("fit")).toString(), QString());
        QVERIFY(model.value(QStringLiteral("estimatedSeconds")).isNull());
        QCOMPARE(model.value(QStringLiteral("languages")).toStringList(), QStringList{QStringLiteral("en")});

        QSignalSpy changed(&models, &QuickModels::changed);
        LocalSetupTestAccess::setHardware(*controller.localSetup(), laptop());
        QCOMPARE(changed.count(), 1);
        QVERIFY(models.hardwareKnown());
        QCOMPARE(models.computer().value(QStringLiteral("threads")).toInt(), 16);
        QCOMPARE(models.computer().value(QStringLiteral("gpu")).toString(), QStringLiteral("AMD Radeon Graphics"));
        QCOMPARE(models.computer().value(QStringLiteral("gpuMemoryBytes")).toDouble(), 0.0);
        QCOMPARE(models.suggestedModel(), parakeet);
        model = entry(models.catalog(), parakeet);
        QCOMPARE(model.value(QStringLiteral("fit")).toString(), QStringLiteral("fits"));
        QVERIFY(model.value(QStringLiteral("estimatedSeconds")).toDouble() > 0);
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        // Only a build that runs models here offers Local Model to rate.
        QCOMPARE(models.speechRatings().value(QStringLiteral("local")).toMap().value(QStringLiteral("ratedModel")).toString(),
                 QStringLiteral("Parakeet 0.6B"));
#endif
        // The card's backend is a choice, and the integrated card runs it.
        QCOMPARE(entry(models.accelerationOptions(), QStringLiteral("vulkan")).value(QStringLiteral("label")).toString(),
                 QStringLiteral("Vulkan"));
        QVERIFY(entry(models.accelerationOptions(), QStringLiteral("cuda")).isEmpty());

        // Nothing changed, nothing announced.
        controller.localSetup()->changed();
        QCOMPARE(changed.count(), 1);
    }

    // Use makes Local Model the service with that model chosen, in one save,
    // as the Settings singleton then reads it; a name that is no model
    // changes nothing.
    void useChoosesTheModelAndTheService()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickModels models(&controller);

        QSignalSpy saved(controller.settings(), &SettingsStore::snapshotApplied);
        models.use(QStringLiteral("whisper-turbo"));
        QCOMPARE(saved.count(), 1);
        const AppSettings stored = SettingsStore().dictationSnapshot();
        QCOMPARE(stored.speech.providerId, QStringLiteral("local"));
        QCOMPARE(stored.speech.local.modelId, QStringLiteral("whisper-turbo"));
        QVERIFY(stored.speech.local.modelChosen);
        QCOMPARE(settings.values().value(QStringLiteral("speechLocalModel")).toString(), QStringLiteral("whisper-turbo"));
        QCOMPARE(models.localModelChoice(), QStringLiteral("whisper-turbo"));
        // Not downloaded, so dictation has no model to use yet.
        QVERIFY(models.inUse().isEmpty());

        QTest::ignoreMessage(QtWarningMsg, "Models.use: made-up is not a Local Model");
        models.use(QStringLiteral("made-up"));
        QCOMPARE(SettingsStore().dictationSnapshot().speech.local.modelId, QStringLiteral("whisper-turbo"));
    }

    // A role setup doesn't offer a fallback for changes nothing.
    void anUnknownFallbackRoleIsRefused()
    {
        ApplicationController controller(true);
        QuickModels models(&controller);
        QSignalSpy saved(controller.settings(), &SettingsStore::snapshotApplied);

        QTest::ignoreMessage(QtWarningMsg, "Models.acceptFallbackOffer: no role transcription");
        models.acceptFallbackOffer(QStringLiteral("transcription"), QStringLiteral("openai"));

        QCOMPARE(saved.count(), 0);
    }

    // A failed download is the model's problem until the next try, and a
    // running one reports its share.
    void downloadsReportProgressAndFailure()
    {
        ApplicationController controller(true);
        QuickModels models(&controller);
        LocalModelStore *store = controller.localModelStore();
        const QString moonshine = QStringLiteral("moonshine-small");

        emit store->downloadFailed(moonshine, QStringLiteral("The disk is full."));
        QCOMPARE(entry(models.catalog(), moonshine).value(QStringLiteral("problem")).toString(),
                 QStringLiteral("The disk is full."));
        QVERIFY(models.downloads().isEmpty());
        QVERIFY(!models.downloaded().contains(moonshine));
    }

    // A connection check runs, then reports core's verdict and the server's
    // models; one that can't connect says why.
    void endpointChecksReportTheVerdict()
    {
        ApplicationController controller(true);
        QuickModels models(&controller);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        AppSettings draft = controller.settings()->dictationSnapshot();
        draft.speech.endpoint.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        controller.settings()->applySnapshot(draft);
        QVERIFY(!models.speechEndpoint().value(QStringLiteral("checked")).toBool());

        models.testSpeechEndpoint();
        QVERIFY(models.speechEndpoint().value(QStringLiteral("checking")).toBool());
        serveJson(server, R"({"data":[{"id":"whisper-1"},{"id":"distil"}]})");
        QTRY_VERIFY(!models.speechEndpoint().value(QStringLiteral("checking")).toBool());
        QVariantMap check = models.speechEndpoint();
        QVERIFY(check.value(QStringLiteral("checked")).toBool());
        QVERIFY(check.value(QStringLiteral("ok")).toBool());
        QCOMPARE(check.value(QStringLiteral("models")).toStringList(), QStringList({"whisper-1", "distil"}));
        QVERIFY(!check.value(QStringLiteral("status")).toString().isEmpty());

        // Port 1, where nothing listens: a port let go could be taken by a
        // server in another test process.
        draft.speech.endpoint.baseUrl = QStringLiteral("http://127.0.0.1:1");
        controller.settings()->applySnapshot(draft);
        models.testSpeechEndpoint();
        QTRY_VERIFY(!models.speechEndpoint().value(QStringLiteral("checking")).toBool());
        check = models.speechEndpoint();
        QVERIFY(check.value(QStringLiteral("checked")).toBool());
        QVERIFY(!check.value(QStringLiteral("ok")).toBool());
        QVERIFY(!check.value(QStringLiteral("status")).toString().isEmpty());
        QVERIFY(check.value(QStringLiteral("models")).toStringList().isEmpty());
    }

    // Setup's Transcription step is done once the chosen service is ready,
    // or while Local Model's download runs. A sign-in nobody has checked, a
    // server never tested and one whose test failed are not; the failure is
    // the endpoint's problem, in core's words.
    void setupTranscriptionIsDoneOnlyOnceTheServiceIsReady()
    {
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        // A Codex whose sign-in check finds one.
        controller.providerRegistry()->registerSpeechProvider(
            {QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")}, [](QObject *parent) {
                auto *codex = new test::FakeSpeechTranscriber(parent);
                codex->providerId = QStringLiteral("codex");
                return codex;
            });
        QuickSettings settings(&controller);
        QuickModels models(&controller);
        // Setup and the pages read System and Audio, as the host always has
        // them.
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(component.create());
        QVERIFY2(choices, qPrintable(component.errorString()));
        const auto transcription = [&] { return setupStep(choices.get(), QStringLiteral("transcription")); };

        // As a step's choice does, which leaves Settings behind.
        const auto choose = [&](const QString &id) { QQmlProperty::write(choices.get(), QStringLiteral("speech"), id); };
        choose(QStringLiteral("codex"));
        QVERIFY(!transcription().value(QStringLiteral("done")).toBool());
        QCOMPARE(transcription().value(QStringLiteral("verdict")).toString(), QStringLiteral("Checking…"));
        models.checkSignIns();
        QTRY_VERIFY(transcription().value(QStringLiteral("done")).toBool());

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        settings.set(QStringLiteral("speechEndpointUrl"), QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        choose(QStringLiteral("endpoint"));
        QVERIFY(!transcription().value(QStringLiteral("done")).toBool());
        QCOMPARE(entry(models.speechServices(), QStringLiteral("endpoint")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("unknown"));
        models.testSpeechEndpoint();
        serveJson(server, R"({"data":[{"id":"whisper-1"}]})");
        QTRY_COMPARE(entry(models.speechServices(), QStringLiteral("endpoint")).value(QStringLiteral("state")).toString(),
                     QStringLiteral("ready"));
        QVERIFY(transcription().value(QStringLiteral("done")).toBool());

        // Port 1, where nothing listens, as above.
        settings.set(QStringLiteral("speechEndpointUrl"), QStringLiteral("http://127.0.0.1:1"));
        models.testSpeechEndpoint();
        QTRY_VERIFY(!models.speechEndpoint().value(QStringLiteral("checking")).toBool());
        QVERIFY(!transcription().value(QStringLiteral("done")).toBool());
        const QVariantMap endpoint = entry(models.speechServices(), QStringLiteral("endpoint"));
        QCOMPARE(endpoint.value(QStringLiteral("state")).toString(), QStringLiteral("checkFailed"));
        QCOMPARE(endpoint.value(QStringLiteral("problem")).toString(),
                 models.speechEndpoint().value(QStringLiteral("status")).toString());
    }

    // A sign-in check that can't reach the service ends, leaves the sign-in
    // unknown and says why, and checking again can still find it.
    void setupSaysWhyASignInCheckFailedAndChecksAgain()
    {
        std::optional<ProviderFailure> answer =
            ProviderFailure{ProviderFailureKind::Timeout, QStringLiteral("ChatGPT didn't answer in time.")};
        const QString unanswered = answer->message;
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        controller.providerRegistry()->registerSpeechProvider(
            {QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")}, [&answer](QObject *parent) {
                auto *codex = new test::FakeSpeechTranscriber(parent);
                codex->providerId = QStringLiteral("codex");
                codex->backgroundPrepare = true;
                codex->backgroundPrepareDelayMs = 50;
                codex->prepareFailure = answer;
                return codex;
            });
        QuickSettings settings(&controller);
        QuickModels models(&controller);
        // Setup and the pages read System and Audio, as the host always has
        // them.
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(component.create());
        QVERIFY2(choices, qPrintable(component.errorString()));
        QQmlProperty::write(choices.get(), QStringLiteral("speech"), QStringLiteral("codex"));
        const auto codex = [&] { return entry(models.speechServices(), QStringLiteral("codex")); };
        const auto transcription = [&] { return setupStep(choices.get(), QStringLiteral("transcription")); };

        models.checkSignIns();
        QVERIFY(codex().value(QStringLiteral("signInChecking")).toBool());
        QTRY_VERIFY(!codex().value(QStringLiteral("signInChecking")).toBool());
        QCOMPARE(codex().value(QStringLiteral("state")).toString(), QStringLiteral("unknown"));
        QCOMPARE(codex().value(QStringLiteral("signInCheckFailure")).toString(), unanswered);
        QVERIFY(!transcription().value(QStringLiteral("done")).toBool());
        QCOMPARE(transcription().value(QStringLiteral("verdict")).toString(), unanswered);

        answer.reset();
        models.checkSignIns();
        QVERIFY(codex().value(QStringLiteral("signInCheckFailure")).toString().isEmpty());
        QTRY_VERIFY(transcription().value(QStringLiteral("done")).toBool());
        QVERIFY(!codex().value(QStringLiteral("signInChecking")).toBool());
    }

    // Setup's Refinement step offers the runner as a fallback only behind a
    // choice whose chain would take it: not behind a full one, judged against
    // the step's choice rather than the saved primary.
    void setupOffersAFallbackOnlyWhereTheChainTakesIt()
    {
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        QuickModels models(&controller);
        // Setup and the pages read System and Audio, as the host always has
        // them.
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        // Their key rows read Accounts, which the host always makes. Its
        // keyring reads fail at once, so none is left waiting on this thread
        // when it goes.
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });
        QuickAccounts accounts(&controller);
        LocalSetupTestAccess::setRunners(*controller.localSetup(),
                                         {{QStringLiteral("ollama"), QStringLiteral("Ollama"), QStringLiteral("0.34.4"),
                                           QStringLiteral("http://127.0.0.1:11434/v1"), {QStringLiteral("gemma4:e4b")}}});
        settings.set(QStringLiteral("refinementProvider"), QStringLiteral("anthropic"));
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        QQmlComponent choicesComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(choicesComponent.create());
        QVERIFY2(choices, qPrintable(choicesComponent.errorString()));
        QQmlComponent stepComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/RefinementStep.qml")));
        const std::unique_ptr<QObject> step(stepComponent.beginCreate(engine.rootContext()));
        QVERIFY2(step, qPrintable(stepComponent.errorString()));
        step->setProperty("choices", QVariant::fromValue(choices.get()));
        stepComponent.completeCreate();
        QObject *offer = step->findChild<QObject *>(QStringLiteral("fallbackOffer"));
        QVERIFY(offer);

        QVERIFY(offer->property("open").toBool());
        settings.set(QStringLiteral("refinementFallbacks"), QStringList{QStringLiteral("openai"), QStringLiteral("endpoint")});
        QVERIFY(!offer->property("open").toBool());
        // Behind OpenAI the chain keeps only the endpoint, so it has room.
        QQmlProperty::write(choices.get(), QStringLiteral("cleanup"), QStringLiteral("openai"));
        QVERIFY(offer->property("open").toBool());
        QQmlProperty::write(choices.get(), QStringLiteral("cleanup"), QStringLiteral("local"));
        QVERIFY(!offer->property("open").toBool());
    }

    // Setup's Refinement step says what Local Runner's rating was taken
    // with, under its ratings and to assistive tech, as the settings page
    // does.
    void setupSaysWhatARatingWasTakenWith()
    {
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        QuickModels models(&controller);
        // Setup and the pages read System and Audio, as the host always has
        // them.
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        // Their key rows read Accounts, which the host always makes. Its
        // keyring reads fail at once, so none is left waiting on this thread
        // when it goes.
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });
        QuickAccounts accounts(&controller);
        LocalSetupTestAccess::setHardware(*controller.localSetup(), laptop());
        const QString ratedModel =
            models.refinementRatings().value(QStringLiteral("local")).toMap().value(QStringLiteral("ratedModel")).toString();
        QVERIFY(!ratedModel.isEmpty());
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        QQmlComponent choicesComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(choicesComponent.create());
        QVERIFY2(choices, qPrintable(choicesComponent.errorString()));
        QQmlComponent stepComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/RefinementStep.qml")));
        const std::unique_ptr<QObject> step(stepComponent.beginCreate(engine.rootContext()));
        QVERIFY2(step, qPrintable(stepComponent.errorString()));
        step->setProperty("choices", QVariant::fromValue(choices.get()));
        stepComponent.completeCreate();
        const QList<QObject *> found = step->findChildren<QObject *>();
        const auto cards = std::find_if(found.begin(), found.end(), [](QObject *object) {
            return QByteArray(object->metaObject()->className()).startsWith("ServiceCards");
        });
        QVERIFY(cards != found.end());

        const QVariantMap local = entry((*cards)->property("options").toList(), QStringLiteral("local"));
        QCOMPARE(local.value(QStringLiteral("note")).toString(), QStringLiteral("Rated as ") + ratedModel);
    }

    // Setup's Transcription step offers the chosen Local Model's download
    // until that model is here, whichever others are.
    void setupOffersTheChosenModelsDownload()
    {
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        QuickModels models(&controller);
        // Setup and the pages read System and Audio, as the host always has
        // them.
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        // Their key rows read Accounts, which the host always makes. Its
        // keyring reads fail at once, so none is left waiting on this thread
        // when it goes.
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });
        QuickAccounts accounts(&controller);
        // The data folder outlives this process, and a model left in it
        // would count as downloaded on the next run.
        const auto removeModels = qScopeGuard([&] {
            for (const QString &id : {QStringLiteral("moonshine-small"), QStringLiteral("parakeet")}) {
                QFile::remove(controller.localModelStore()->modelPath(*findLocalModel(id)));
            }
        });
        placeDownloaded(controller, QStringLiteral("moonshine-small"));
        settings.set(QStringLiteral("speechLocalModel"), QStringLiteral("parakeet"));
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        QQmlComponent choicesComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(choicesComponent.create());
        QVERIFY2(choices, qPrintable(choicesComponent.errorString()));
        QQmlProperty::write(choices.get(), QStringLiteral("speech"), QStringLiteral("local"));
        QQmlComponent stepComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/TranscriptionStep.qml")));
        const std::unique_ptr<QObject> step(stepComponent.beginCreate(engine.rootContext()));
        QVERIFY2(step, qPrintable(stepComponent.errorString()));
        step->setProperty("choices", QVariant::fromValue(choices.get()));
        stepComponent.completeCreate();
        QObject *download = step->findChild<QObject *>(QStringLiteral("localModelDownload"));
        QVERIFY(download);

        QVERIFY(download->property("shown").toBool());
        placeDownloaded(controller, QStringLiteral("parakeet"));
        QVERIFY(!download->property("shown").toBool());
    }

    // Every runner is listed, those found running with their version and
    // models; Ollama without the suggested cleanup model offers to pull it.
    void runnersAndTheCleanupPull()
    {
        ApplicationController controller(true);
        QuickModels models(&controller);
        LocalSetup &local = *controller.localSetup();
        LocalSetupTestAccess::setHardware(local, laptop());
        LocalSetupTestAccess::setRunners(local, {{QStringLiteral("ollama"), QStringLiteral("Ollama"),
                                                 QStringLiteral("0.34.4"), QStringLiteral("http://127.0.0.1:11434/v1"),
                                                 {QStringLiteral("qwen3:8b")}}});

        QCOMPARE(models.runners().size(), 3);
        const QVariantMap ollama = entry(models.runners(), QStringLiteral("ollama"));
        QVERIFY(ollama.value(QStringLiteral("running")).toBool());
        QCOMPARE(ollama.value(QStringLiteral("label")).toString(), QStringLiteral("Ollama 0.34.4"));
        QCOMPARE(ollama.value(QStringLiteral("models")).toStringList(), QStringList{QStringLiteral("qwen3:8b")});
        const QVariantMap lmStudio = entry(models.runners(), QStringLiteral("lmstudio"));
        QVERIFY(!lmStudio.value(QStringLiteral("running")).toBool());
        QCOMPARE(lmStudio.value(QStringLiteral("label")).toString(), QStringLiteral("LM Studio"));
        QVERIFY(models.runnersChecked());
        QVERIFY(!models.cleanupSuggestion().isEmpty());
        QVERIFY(models.offerPull());

        QSignalSpy pulled(&models, &QuickModels::pullChanged);
        models.pullCleanupModel();
        QVERIFY(models.pull().value(QStringLiteral("running")).toBool());
        QCOMPARE(models.pull().value(QStringLiteral("tag")).toString(),
                 models.cleanupSuggestion().value(QStringLiteral("tag")).toString());
        models.cancelPull();
        QVERIFY(!models.pull().value(QStringLiteral("running")).toBool());
        QVERIFY(models.pull().value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(pulled.count() >= 2);
    }

    // The pages and setup steps that read Models load over the C++ one
    // without a warning, so every property they read exists. On a page that
    // chooses a provider, choosing its fallback is one save, which takes it
    // out of the fallbacks.
    void thePagesLoadOverIt_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("fallback");
        QTest::addColumn<QString>("fallbacksRow");
        QTest::newRow("pages/SpeechPage") << QStringLiteral("pages/SpeechPage") << QStringLiteral("endpoint")
                                          << QStringLiteral("speechFallbackList");
        QTest::newRow("pages/RefinementPage") << QStringLiteral("pages/RefinementPage") << QStringLiteral("local")
                                              << QStringLiteral("refinementFallbacks");
        for (const char *page : {"pages/ModelsPage", "setup/TranscriptionStep", "setup/RefinementStep"}) {
            QTest::newRow(page) << QString::fromLatin1(page) << QString() << QString();
        }
    }

    void thePagesLoadOverIt()
    {
        QFETCH(QString, page);
        QFETCH(QString, fallback);
        QFETCH(QString, fallbacksRow);
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        QuickModels models(&controller);
        // Setup and the pages read System and Audio, as the host always has
        // them.
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        // Their key rows read Accounts, which the host always makes. Its
        // keyring reads fail at once, so none is left waiting on this thread
        // when it goes.
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });
        QuickAccounts accounts(&controller);
        // Every section the page has, open.
        settings.set(QStringLiteral("speechFallbackList"), QStringList{QStringLiteral("endpoint")});
        settings.set(QStringLiteral("refinementFallbacks"), QStringList{QStringLiteral("local")});
        LocalSetupTestAccess::setHardware(*controller.localSetup(), laptop());
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        QQmlComponent choicesComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(choicesComponent.create());
        QVERIFY2(choices, qPrintable(choicesComponent.errorString()));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/%1.qml").arg(page)));
        const std::unique_ptr<QObject> loaded(component.beginCreate(engine.rootContext()));
        QVERIFY2(loaded, qPrintable(component.errorString()));
        // A setup step reads the choices the assistant hands it.
        if (page.startsWith(QLatin1String("setup/"))) {
            loaded->setProperty("choices", QVariant::fromValue(choices.get()));
        }
        component.completeCreate();
        if (fallback.isEmpty()) {
            return;
        }

        QSignalSpy saved(controller.settings(), &SettingsStore::snapshotApplied);
        QVERIFY(QMetaObject::invokeMethod(loaded.get(), "choose", Q_ARG(QVariant, fallback)));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(settings.values().value(fallbacksRow).toStringList(), QStringList());
    }

private:
    QTemporaryDir m_home;
};

int runQuickModelsTests(int argc, char **argv)
{
    QuickModelsTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_models.moc"
