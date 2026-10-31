#include "common/test_secrets.h"
#include "common/test_suites.h"

#include "app/Accounts.h"
#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/ProviderAvailability.h"
#include "common/test_auth.h"
#include "common/test_http.h"
#include "common/test_local_setup.h"
#include "core/EndpointSettings.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/quick/host/QuickAccounts.h"
#include "frontend/quick/host/QuickLibrary.h"
#include "frontend/quick/host/QuickModels.h"
#include "frontend/quick/host/QuickNavigation.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

#include <memory>

// The UI's QML modules are linked into the tests by test_quick_settings.cpp,
// which imports their plugins for the whole binary.

using namespace speecher;
using namespace speecher::test;

namespace {

QVariantMap entry(const QVariantList &list, const QString &id)
{
    for (const QVariant &item : list) {
        if (item.toMap().value(QStringLiteral("id")) == id) {
            return item.toMap();
        }
    }
    return {};
}

QVariantMap service(const QuickAccounts &accounts, const QString &id)
{
    return entry(accounts.services(), id);
}

QString serviceState(const QuickAccounts &accounts, const QString &id)
{
    return service(accounts, id).value(QStringLiteral("state")).toString();
}

bool checked(const QuickAccounts &accounts)
{
    for (const QVariant &service : accounts.services()) {
        if (service.toMap().value(QStringLiteral("state")) == QLatin1String("checking")) {
            return false;
        }
    }
    return true;
}

QString accountState(const QuickAccounts &accounts, const QString &fileName)
{
    return entry(accounts.cliproxyAccounts(), fileName).value(QStringLiteral("state")).toString();
}

// The sign-ins the daemon's provider availability has noted.
QHash<QString, bool> notedSignIns(ApplicationController &controller)
{
    LiveFacts facts;
    controller.providerAvailability()->addTo(facts);
    return facts.signedIn;
}

// A secret's SchemaRow in a window, its text field and its Clear button.
struct SecretRowWindow {
    std::unique_ptr<QObject> window;
    QQuickItem *field = nullptr;
    QQuickItem *clear = nullptr;
};

SecretRowWindow showSecretRow(QQmlEngine &engine, const QString &rowId = QStringLiteral("cliproxyApiKey"))
{
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import Speecher.App
        Window {
            id: window
            // Bound rather than an alias: Qt 6.10 doesn't count an initial
            // property set through an alias as setting the required rowId.
            property string rowId
            width: 640; height: 120; visible: true
            SchemaRow { rowId: window.rowId; width: parent.width }
        }
    )", QUrl());
    SecretRowWindow shown{std::unique_ptr<QObject>(component.createWithInitialProperties({{QStringLiteral("rowId"), rowId}}))};
    if (!shown.window) {
        qWarning().noquote() << component.errorString();
        return shown;
    }
    for (QQuickItem *item : shown.window->findChildren<QQuickItem *>()) {
        const QByteArray type = item->metaObject()->className();
        if (type.startsWith("TextField_")) {
            shown.field = item;
        } else if (type.startsWith("IconButton_") && item->property("iconName") == QLatin1String("x")) {
            shown.clear = item;
        }
    }
    return shown;
}

// Signs OpenAI in with a CLI Proxy API Codex account whose token has run out
// but whose refresh token renews it. Returns the account's file name.
QString useExpiredCodexAccount(SettingsStore &store, const QString &directory)
{
    const QString fileName = QStringLiteral("codex-a@example.com.json");
    if (!writeCliProxyAccount(directory, fileName, QStringLiteral("codex"), QStringLiteral("token"),
                              QDateTime::currentDateTimeUtc().addSecs(-60))) {
        return {};
    }
    store.edit([&](AppSettings &draft) {
        draft.accounts.cliproxyOauthDirConfigured = directory;
        draft.accounts.openAiAuthMode = OpenAiAuthMode::Cliproxy;
        draft.accounts.openAiCliproxyAccount = fileName;
        return true;
    });
    return fileName;
}

const char *const proxyKeyFallbackWarning =
    "keyring unavailable, keeping cliproxy-api-key in the settings file: Keyring write denied";

} // namespace

// The UI's Accounts singleton: each sign-in as core finds it on a worker, CLI
// Proxy API's account files from disk, and secrets saved through the daemon's
// SecretStore without ever reaching QML.
class QuickAccountsTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
        // Nothing here reads this user's Claude Code or Codex sign-ins.
        settings.raw().setValue(SettingsKeys::ClaudeCredentialsPath, m_home.filePath(QStringLiteral("claude.json")));
        qputenv("SPEECHER_TEST_CODEX_AUTH_PATH", QFile::encodeName(m_home.filePath(QStringLiteral("codex.json"))));
        // The keyring stays out of these tests: a write fails as a missing
        // one would, and the settings file holds what may fall back there.
        // Reads fail at once, so none is left waiting on this thread when an
        // Accounts goes.
        qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        m_openAiKey = qgetenv("OPENAI_API_KEY");
        qunsetenv("OPENAI_API_KEY");
    }

    void cleanup()
    {
        qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE");
        qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT");
        qunsetenv("SPEECHER_TEST_CODEX_AUTH_PATH");
        if (!m_openAiKey.isEmpty()) {
            qputenv("OPENAI_API_KEY", m_openAiKey);
        }
    }

    void servicesAndAccountFilesComeFromCore()
    {
        ApplicationController controller(true);
        QTemporaryDir dir;
        const QDateTime valid = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-a@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("token"), valid));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-b@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("token"), valid, true));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"), QStringLiteral("claude"),
                                     QStringLiteral("token"), QDateTime::currentDateTimeUtc().addSecs(-60)));
        // A file dictation would refuse: it holds no token.
        QFile tokenless(dir.filePath(QStringLiteral("codex-c@example.com.json")));
        QVERIFY(tokenless.open(QIODevice::WriteOnly));
        tokenless.write(R"({"type": "codex"})");
        tokenless.close();
        SettingsStore *store = controller.settings();
        store->edit([&](AppSettings &draft) {
            draft.accounts.cliproxyOauthDirConfigured = dir.path();
            draft.accounts.openAiAuthMode = OpenAiAuthMode::Env;
            draft.accounts.openAiCliproxyAccount = QStringLiteral("codex-gone@example.com.json");
            draft.accounts.anthropicAuthMode = AnthropicAuthMode::Cliproxy;
            draft.accounts.anthropicCliproxyAccount = QStringLiteral("claude-a@example.com.json");
            return true;
        });

        QuickAccounts accounts(&controller);
        // Nothing is read until a page asks.
        QCOMPARE(serviceState(accounts, QStringLiteral("openai")), QStringLiteral("checking"));
        QVERIFY(accounts.cliproxyAccounts().isEmpty());
        QCOMPARE(QuickAccounts::create(nullptr, nullptr), &accounts);
        QTRY_VERIFY(checked(accounts));

        const QVariantMap openAi = service(accounts, QStringLiteral("openai"));
        QCOMPARE(openAi.value(QStringLiteral("state")).toString(), QStringLiteral("missing"));
        QCOMPARE(openAi.value(QStringLiteral("detail")).toString(), QStringLiteral("OPENAI_API_KEY not found"));
        const QVariantMap anthropic = service(accounts, QStringLiteral("anthropic"));
        QCOMPARE(anthropic.value(QStringLiteral("state")).toString(), QStringLiteral("expired"));
        QCOMPARE(anthropic.value(QStringLiteral("directory")).toString(), dir.path());
        const QVariantMap cliproxy = service(accounts, QStringLiteral("cliproxy"));
        QCOMPARE(cliproxy.value(QStringLiteral("state")).toString(), QStringLiteral("signedIn"));
        QCOMPARE(cliproxy.value(QStringLiteral("found")).toString(), QStringLiteral("signedIn"));
        QCOMPARE(cliproxy.value(QStringLiteral("detail")).toString(), QString());
        QCOMPARE(cliproxy.value(QStringLiteral("directory")).toString(), dir.path());

        // What was found definitely reaches fallbacks and `speecher
        // providers`; an expired sign-in, which dictation may renew, does
        // not. Checking renews nothing, so the speech sign-ins wait for
        // setup, a renewal or a dictation.
        const QHash<QString, bool> noted = notedSignIns(controller);
        QCOMPARE(noted.value(QStringLiteral("openai"), true), false);
        QVERIFY(!noted.contains(QStringLiteral("anthropic")));
        QVERIFY(!noted.contains(QStringLiteral("codex")));
        QVERIFY(!noted.contains(QStringLiteral("claude")));

        const QVariantList files = accounts.cliproxyAccounts();
        QCOMPARE(files.size(), 5);
        QCOMPARE(accountState(accounts, QStringLiteral("codex-c@example.com.json")), QStringLiteral("unusable"));
        const QVariantMap first = entry(files, QStringLiteral("codex-a@example.com.json"));
        QCOMPARE(first.value(QStringLiteral("type")).toString(), QStringLiteral("codex"));
        QCOMPARE(first.value(QStringLiteral("label")).toString(), QStringLiteral("a@example.com"));
        QCOMPARE(first.value(QStringLiteral("state")).toString(), QStringLiteral("signedIn"));
        QCOMPARE(entry(files, QStringLiteral("codex-b@example.com.json")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("disabled"));
        QCOMPARE(entry(files, QStringLiteral("codex-gone@example.com.json")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("missing"));
        QCOMPARE(entry(files, QStringLiteral("claude-a@example.com.json")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("expired"));
    }

    // The daemon checks sign-ins and notes them without any window, so
    // nothing but the asking needs the UI.
    void theDaemonChecksSignInsWithoutTheUi()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.accounts.openAiAuthMode = OpenAiAuthMode::Env;
            return true;
        });
        Accounts *accounts = controller.accounts();
        accounts->start();
        QTRY_VERIFY(accounts->signIn(SignInService::OpenAi));
        QCOMPARE(accounts->signIn(SignInService::OpenAi)->state, SignInState::Missing);
        QCOMPARE(notedSignIns(controller).value(QStringLiteral("openai"), true), false);
    }

    // A service's card on Accounts follows its checks rather than keeping
    // what it showed when it was made.
    void aServiceCardFollowsItsChecks()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.accounts.openAiAuthMode = OpenAiAuthMode::Env;
            return true;
        });
        QuickSettings settings(&controller);
        // The card asks Library which services the profiles use.
        const QuickLibrary::ControllerScope library(&controller);
        QuickModels models(&controller);
        QuickAccounts accounts(&controller);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/app/ServiceCard.qml")));
        const std::unique_ptr<QObject> card(
            component.createWithInitialProperties({{QStringLiteral("serviceId"), QStringLiteral("openai")}}));
        QVERIFY2(card, qPrintable(component.errorString()));
        const auto shown = [&card] { return card->property("service").toMap().value(QStringLiteral("state")); };

        QCOMPARE(shown(), QVariant(QStringLiteral("checking")));
        QTRY_COMPARE(shown(), QVariant(QStringLiteral("missing")));
    }

    // A missing sign-in through the Codex app gets the card's own fix; an
    // API key from the environment keeps core's line, which names the key.
    void aServiceCardWordsItsSignInSource()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("openAiAuthMode"), QStringLiteral("env"));
        // The card asks Library which services the profiles use.
        const QuickLibrary::ControllerScope library(&controller);
        QuickModels models(&controller);
        QuickAccounts accounts(&controller);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/app/ServiceCard.qml")));
        const std::unique_ptr<QObject> card(
            component.createWithInitialProperties({{QStringLiteral("serviceId"), QStringLiteral("openai")}}));
        QVERIFY2(card, qPrintable(component.errorString()));
        const auto state = [&card] { return card->property("service").toMap().value(QStringLiteral("state")).toString(); };
        const QString codexFix = QStringLiteral("No ChatGPT sign-in found. Sign in with the Codex app, or choose another sign-in below.");

        QTRY_COMPARE(state(), QStringLiteral("missing"));
        QCOMPARE(card->property("detail").toString(),
                 card->property("service").toMap().value(QStringLiteral("detail")).toString());
        QVERIFY(card->property("detail").toString() != codexFix);

        settings.set(QStringLiteral("openAiAuthMode"), QStringLiteral("codex_oauth"));
        QTRY_COMPARE(state(), QStringLiteral("missing"));
        QCOMPARE(card->property("detail").toString(), codexFix);
    }

    // A service is in use while the settings sign in through it, a writing
    // profile's own service included, and Accounts says when that changes.
    // The card's lookup starts Accounts, which then follows saves.
    void aServiceIsInUseWhileTheSettingsSignInThroughIt()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("speechProvider"), QStringLiteral("local"));
        settings.set(QStringLiteral("speechFallbackList"), QStringList());
        settings.set(QStringLiteral("refinementProvider"), QStringLiteral("none"));
        settings.set(QStringLiteral("refinementFallbacks"), QStringList());
        const QuickLibrary::ControllerScope library(&controller);
        QuickModels models(&controller);
        QuickAccounts accounts(&controller);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/app/ServiceCard.qml")));
        const std::unique_ptr<QObject> card(
            component.createWithInitialProperties({{QStringLiteral("serviceId"), QStringLiteral("anthropic")}}));
        QVERIFY2(card, qPrintable(component.errorString()));
        const auto inUse = [&accounts] {
            return service(accounts, QStringLiteral("anthropic")).value(QStringLiteral("inUse")).toBool();
        };
        QVERIFY(!inUse());

        QSignalSpy changed(&accounts, &QuickAccounts::servicesChanged);
        AppSettings snapshot = controller.settings()->snapshot();
        QVERIFY(!snapshot.refinement.writingProfiles.isEmpty());
        snapshot.refinement.writingProfiles.first().speechProvider = QStringLiteral("claude");
        controller.settings()->applySnapshot(snapshot);
        QVERIFY(changed.count() > 0);
        QVERIFY(inUse());
    }

    // Setup's fallback offer saves a provider without a snapshot; Accounts
    // still says the sign-in it uses is now in use.
    void aSetupFallbackOfferChangesWhatIsInUse()
    {
        ApplicationController controller(true);
        SettingsStore *store = controller.settings();
        store->edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("local");
            draft.speech.fallbackProviderIds = {};
            draft.refinement.providerId = QStringLiteral("none");
            draft.refinement.fallbackProviderIds = {};
            return true;
        });
        QuickAccounts accounts(&controller);
        const auto inUse = [&accounts] {
            return service(accounts, QStringLiteral("anthropic")).value(QStringLiteral("inUse")).toBool();
        };
        QVERIFY(!inUse());
        LocalSetupTestAccess::setRunners(*controller.localSetup(),
                                         {{QStringLiteral("ollama"), QStringLiteral("Ollama"), QStringLiteral("0.34.4"),
                                           QStringLiteral("http://127.0.0.1:11434/v1"), {QStringLiteral("gemma4:e4b")}}});

        QSignalSpy changed(&accounts, &QuickAccounts::servicesChanged);
        controller.localSetup()->acceptSetupFallbackOffer(ProviderRole::Refinement, QStringLiteral("anthropic"));
        QCOMPARE(store->dictationSnapshot().refinement.providerId, QStringLiteral("anthropic"));
        QVERIFY(changed.count() > 0);
        QVERIFY(inUse());
    }

    // Refinement's sign-in row waits for Accounts' first answer, then says
    // how to sign in.
    void aRefinerSignInRowWaitsForTheCheck()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("openAiAuthMode"), QStringLiteral("env"));
        const QuickLibrary::ControllerScope library(&controller);
        QuickModels models(&controller);
        QuickAccounts accounts(&controller);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/RefinerSignInRow.qml")));
        const std::unique_ptr<QObject> row(
            component.createWithInitialProperties({{QStringLiteral("providerId"), QStringLiteral("openai")}}));
        QVERIFY2(row, qPrintable(component.errorString()));

        QVERIFY(!row->property("shown").toBool());
        QTRY_VERIFY(row->property("shown").toBool());
        QVERIFY(!row->property("unknown").toBool());
    }

    // An expired sign-in, which core leaves unknown, still gets the row, and
    // Check again keeps it up while Accounts checks.
    void aRefinerSignInRowShowsAnExpiredSignIn()
    {
        ApplicationController controller(true);
        QTemporaryDir dir;
        QVERIFY(!useExpiredCodexAccount(*controller.settings(), dir.path()).isEmpty());
        QuickSettings settings(&controller);
        const QuickLibrary::ControllerScope library(&controller);
        QuickModels models(&controller);
        QuickAccounts accounts(&controller);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/RefinerSignInRow.qml")));
        const std::unique_ptr<QObject> row(
            component.createWithInitialProperties({{QStringLiteral("providerId"), QStringLiteral("openai")}}));
        QVERIFY2(row, qPrintable(component.errorString()));

        QTRY_VERIFY(row->property("shown").toBool());
        QCOMPARE(serviceState(accounts, QStringLiteral("openai")), QStringLiteral("expired"));
        accounts.recheck(QStringLiteral("openai"));
        QCOMPARE(serviceState(accounts, QStringLiteral("openai")), QStringLiteral("checking"));
        QVERIFY(row->property("shown").toBool());
    }

    // A sign-in setting saved from any page checks the sign-ins again; any
    // other setting leaves them be.
    void aSignInSettingChecksAgain()
    {
        ApplicationController controller(true);
        QTemporaryDir dir;
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-a@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("token"), QDateTime::currentDateTimeUtc().addSecs(3600)));
        controller.settings()->edit([&](AppSettings &draft) {
            draft.accounts.cliproxyOauthDirConfigured = dir.path();
            draft.accounts.openAiAuthMode = OpenAiAuthMode::Env;
            return true;
        });
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QuickAccounts::create(nullptr, nullptr);
        QTRY_VERIFY(checked(accounts));
        QCOMPARE(serviceState(accounts, QStringLiteral("openai")), QStringLiteral("missing"));

        QSignalSpy changed(&accounts, &QuickAccounts::servicesChanged);
        settings.set(QStringLiteral("themeControl"), QStringLiteral("dark"));
        QCOMPARE(changed.count(), 0);

        settings.set(QStringLiteral("openAiAuthMode"), QStringLiteral("cliproxy"));
        settings.set(QStringLiteral("openAiCliproxyAccount"), QStringLiteral("codex-a@example.com.json"));
        QCOMPARE(serviceState(accounts, QStringLiteral("openai")), QStringLiteral("checking"));
        QTRY_VERIFY(checked(accounts));
        const QVariantMap openAi = service(accounts, QStringLiteral("openai"));
        QCOMPARE(openAi.value(QStringLiteral("state")).toString(), QStringLiteral("signedIn"));
        QCOMPARE(openAi.value(QStringLiteral("detail")).toString(), QStringLiteral("CLI Proxy API Codex token found"));
    }

    void secretsAreSavedButNeverShown()
    {
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);

        QTest::ignoreMessage(QtWarningMsg,
                             "keyring unavailable, keeping cliproxy-api-key in the settings file: Keyring write denied");
        accounts.saveSecret(QStringLiteral("cliproxyApiKey"), QStringLiteral("  proxy-key  "));
        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("cliproxyApiKey")), QVariant(true));
        QCOMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QStringLiteral("proxy-key"));
        QVERIFY(accounts.secretErrors().isEmpty());

        // The OpenAI key never falls back to the settings file; why it was
        // not saved is the keyring's answer.
        accounts.saveSecret(QStringLiteral("openAiApiKey"), QStringLiteral("sk-test-key"));
        QTRY_COMPARE(accounts.secretErrors().value(QStringLiteral("openAiApiKey")),
                 QVariant(QVariantMap{{QStringLiteral("action"), QStringLiteral("save")},
                                      {QStringLiteral("reason"), QStringLiteral("Keyring write denied")}}));
        QVERIFY(!accounts.savedSecrets().contains(QStringLiteral("openAiApiKey")));
        QCOMPARE(SettingsStore().secrets()->apiKey(), QString());

        const QByteArray visible = QJsonDocument::fromVariant(QVariantList{accounts.services(),
                                                                           accounts.cliproxyAccounts(),
                                                                           accounts.savedSecrets(),
                                                                           accounts.secretErrors()})
                                       .toJson();
        QVERIFY(!visible.contains("proxy-key"));
        QVERIFY(!visible.contains("sk-test-key"));

        accounts.clearSecret(QStringLiteral("cliproxyApiKey"));
        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("cliproxyApiKey")), QVariant(false));
        QCOMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QString());
    }

    // The CLI Proxy preset shows, at proxy-b, over the Custom server-a whose
    // key the keyring holds, so dropping that key waits on the keyring. Set
    // before the app starts, which then can't know the key yet.
    static void showTheProxyPresetOverAKeyringKey()
    {
        SettingsStore settings;
        settings.raw().setValue(SettingsKeys::CliproxyBaseUrl, QStringLiteral("http://proxy-b.example:8317"));
        settings.raw().setValue(SettingsKeys::RefinementEndpointPreset, QStringLiteral("cliproxy"));
        settings.raw().setValue(SettingsKeys::RefinementEndpointBaseUrl, QStringLiteral("http://server-a.example/v1"));
        settings.raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("refinement-endpoint-key")});
        settings.raw().sync();
    }

    // A server change made while the keyring makes an entered key wait comes
    // after the key, so the endpoint reaches the new server without it.
    void aServerChangeDuringAKeySaveLeavesTheKeyBehind()
    {
        showTheProxyPresetOverAKeyringKey();
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);
        SettingsStore &settings = *controller.settings();
        bool changedDuringTheSave = false;
        QTimer::singleShot(0, &accounts, [&] {
            changedDuringTheSave = true;
            settings.edit([](AppSettings &draft) {
                editRefinementEndpoint(draft, {.baseUrl = QStringLiteral("http://server-c.example/v1")});
                return true;
            });
        });

        accounts.saveSecret(QStringLiteral("refinementEndpointApiKey"), QStringLiteral("key-b"));

        QVERIFY(!changedDuringTheSave);
        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("refinementEndpointApiKey")), QVariant(false));
        QVERIFY(changedDuringTheSave);
        const RefinementEndpoint sent = resolvedRefinementEndpoint(SettingsStore().snapshot().refinement);
        QCOMPARE(sent.apiBase, QStringLiteral("http://server-c.example/v1"));
        QCOMPARE(sent.apiKey, QString());
    }

    // A key moving the endpoint drops shows as not saved even when the
    // keyring refuses to delete it: the key is hidden, its clear waits to be
    // retried, and the row says why.
    void anEndpointKeyWhoseDropTheKeyringRefusesShowsItsPendingClear()
    {
        {
            SettingsStore saved;
            saved.raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("refinement-endpoint-key")});
            saved.raw().sync();
        }
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("refinementEndpointUrl"), QStringLiteral("http://server-a.example/v1"));
        accounts.saveSecret(QStringLiteral("refinementEndpointApiKey"), QStringLiteral("key-a"));
        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("refinementEndpointApiKey")), QVariant(true));

        settings.set(QStringLiteral("refinementEndpointUrl"), QStringLiteral("http://server-c.example/v1"));

        const QString row = QStringLiteral("refinementEndpointApiKey");
        QTRY_COMPARE(accounts.savedSecrets().value(row), QVariant(false));
        QCOMPARE(accounts.pendingClears().value(row), QVariant(true));
        const QVariantMap error = accounts.secretErrors().value(row).toMap();
        QCOMPARE(error.value(QStringLiteral("action")).toString(), QStringLiteral("clear"));
        QVERIFY2(error.value(QStringLiteral("reason")).toString().contains(QStringLiteral("deletion denied")),
                 qPrintable(error.value(QStringLiteral("reason")).toString()));
    }

#ifndef Q_OS_WIN
    // A save refused before the key went in, as when the settings file can't
    // be made private, keeps its reason on the row through a later settings
    // edit, which stores the key's old state again and changes nothing. A
    // key a settings edit replaces is a save that went through, and ends it.
    void aRefusedSaveKeepsItsReasonUntilASaveGoesThrough_data()
    {
        QTest::addColumn<QString>("replacement");
        QTest::addColumn<bool>("kept");
        QTest::newRow("unrelated edit") << QString() << true;
        QTest::newRow("key replaced by an edit") << QStringLiteral("replacement-key") << false;
    }

    void aRefusedSaveKeepsItsReasonUntilASaveGoesThrough()
    {
        QFETCH(QString, replacement);
        QFETCH(bool, kept);
        if (getuid() == 0) {
            QSKIP("Root writes to a read-only folder");
        }
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);
        QuickSettings settings(&controller);
        const QString file = controller.settings()->raw().fileName();
        const QString folder = QFileInfo(file).absolutePath();
        const auto writable = qScopeGuard([folder] {
            QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        });
        // No file, and a folder it can't be made in.
        QFile::remove(file);
        QVERIFY(QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner));
        accounts.saveSecret(QStringLiteral("cliproxyApiKey"), QStringLiteral("new-key"));
        QTRY_VERIFY(accounts.secretErrors().contains(QStringLiteral("cliproxyApiKey")));
        QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QVariant refusal = accounts.secretErrors().value(QStringLiteral("cliproxyApiKey"));
        const QString reason = refusal.toMap().value(QStringLiteral("reason")).toString();
        QVERIFY2(reason.contains(QStringLiteral("can't be kept from other users")), qPrintable(reason));

        controller.settings()->edit([&replacement](AppSettings &draft) {
            draft.ui.previewWords = 13;
            if (!replacement.isEmpty()) {
                draft.refinement.cliproxyApiKey = replacement;
            }
            return true;
        });

        QTRY_COMPARE(accounts.secretErrors().value(QStringLiteral("cliproxyApiKey")), kept ? refusal : QVariant());
    }

    // A save whose settings-file commit failed leaves the key in place but
    // unsaved. A retry that commits it, made directly or by a settings edit
    // that saves the file, is a save that went through, and ends the
    // failure, though it finds the key already in place. So is one after
    // another key was refused before it changed anything (r376-sol9).
    void aRetryThatCommitsARefusedSaveEndsItsFailure_data()
    {
        QTest::addColumn<bool>("throughAnEdit");
        QTest::addColumn<bool>("anotherRefusedFirst");
        QTest::newRow("saved again") << false << false;
        QTest::newRow("a settings edit") << true << false;
        QTest::newRow("saved again after another refusal") << false << true;
    }

    void aRetryThatCommitsARefusedSaveEndsItsFailure()
    {
        QFETCH(bool, throughAnEdit);
        QFETCH(bool, anotherRefusedFirst);
        if (getuid() == 0) {
            QSKIP("Root writes to a read-only folder");
        }
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);
        SettingsStore &settings = *controller.settings();
        settings.edit([](AppSettings &draft) {
            draft.ui.previewWords = 12;
            return true;
        });
        const QString folder = QFileInfo(settings.raw().fileName()).absolutePath();
        const auto writable = qScopeGuard([folder] {
            QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        });
        // Qt writes the file through a temporary one beside it.
        QVERIFY(QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner));
        accounts.saveSecret(QStringLiteral("cliproxyApiKey"), QStringLiteral("new-key"));
        QTRY_VERIFY(accounts.secretErrors().contains(QStringLiteral("cliproxyApiKey")));
        QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QString reason = accounts.secretErrors()
                                   .value(QStringLiteral("cliproxyApiKey"))
                                   .toMap()
                                   .value(QStringLiteral("reason"))
                                   .toString();
        QVERIFY2(reason.contains(QStringLiteral("Could not save the settings file")), qPrintable(reason));
        if (anotherRefusedFirst) {
            // No file, and a folder it can't be made in: the key can't go in.
            QFile::remove(settings.raw().fileName());
            QVERIFY(QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner));
            accounts.saveSecret(QStringLiteral("cliproxyApiKey"), QStringLiteral("other-key"));
            QTRY_VERIFY(accounts.secretErrors().value(QStringLiteral("cliproxyApiKey")).toMap()
                            .value(QStringLiteral("reason")).toString()
                            .contains(QStringLiteral("can't be kept from other users")));
            QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }

        if (throughAnEdit) {
            settings.edit([](AppSettings &draft) {
                draft.ui.previewWords = 13;
                return true;
            });
        } else {
            QVERIFY(savedSecret(settings.secrets(), SecretStore::Secret::CliproxyApiKey, QStringLiteral("new-key")));
        }

        QTRY_VERIFY(!accounts.secretErrors().contains(QStringLiteral("cliproxyApiKey")));
        QCOMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QStringLiteral("new-key"));
        QCOMPARE(accounts.savedSecrets().value(QStringLiteral("cliproxyApiKey")), QVariant(true));
    }
#endif

    // The key field still holds what was submitted for server A while the
    // keyring makes it wait. Moving the endpoint to B meanwhile drops that
    // draft, so leaving the field again saves nothing for B; text typed
    // after submitting stays, being the person's for B.
    void aKeyFieldDropsItsDraftWhenTheServerMovesDuringTheSave_data()
    {
        QTest::addColumn<QString>("typedMeanwhile");
        QTest::newRow("unchanged draft") << QString();
        QTest::newRow("typed after submitting") << QStringLiteral("typed-for-b");
    }

    void aKeyFieldDropsItsDraftWhenTheServerMovesDuringTheSave()
    {
        QFETCH(QString, typedMeanwhile);
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        settings.set(QStringLiteral("refinementEndpointUrl"), QStringLiteral("http://server-a.example/v1"));
        const SecretRowWindow row = showSecretRow(engine, QStringLiteral("refinementEndpointApiKey"));
        QVERIFY(row.field);
        QQuickItem *field = row.field;
        bool movedDuringTheSave = false;
        QMetaObject::invokeMethod(
            field,
            [&] {
                movedDuringTheSave = true;
                if (!typedMeanwhile.isEmpty()) {
                    field->setProperty("text", typedMeanwhile);
                }
                settings.set(QStringLiteral("refinementEndpointUrl"), QStringLiteral("http://server-b.example/v1"));
            },
            Qt::QueuedConnection);

        field->setProperty("text", QStringLiteral("entered-for-a"));
        QVERIFY(QMetaObject::invokeMethod(field, "editingFinished"));

        QVERIFY(!movedDuringTheSave);
        QTRY_VERIFY(movedDuringTheSave);
        QTRY_COMPARE(field->property("text").toString(), typedMeanwhile);
        if (typedMeanwhile.isEmpty()) {
            QVERIFY(QMetaObject::invokeMethod(field, "editingFinished"));
            const RefinementEndpoint sent = resolvedRefinementEndpoint(SettingsStore().snapshot().refinement);
            QCOMPARE(sent.apiBase, QStringLiteral("http://server-b.example/v1"));
            QCOMPARE(sent.apiKey, QString());
        }
    }

    // An edit of other settings while the keyring makes a key's save wait
    // leaves that save alone, so the field still gets its answer
    // (r376-opus4).
    void anEditDuringAKeySaveLeavesTheFieldItsAnswer()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const SecretRowWindow row = showSecretRow(engine);
        QVERIFY(row.field);
        QTest::ignoreMessage(QtWarningMsg, proxyKeyFallbackWarning);
        row.field->setProperty("text", QStringLiteral("proxy-key"));
        QVERIFY(QMetaObject::invokeMethod(row.field, "editingFinished"));

        controller.settings()->edit([](AppSettings &draft) {
            draft.ui.previewWords = 13;
            return true;
        });

        QTRY_COMPARE(row.field->property("text").toString(), QString());
        QCOMPARE(accounts.savedSecrets().value(QStringLiteral("cliproxyApiKey")), QVariant(true));
    }

    // A key entered while the keyring makes an older one wait stands.
    void aKeyEnteredDuringAKeySaveStands()
    {
        showTheProxyPresetOverAKeyringKey();
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);
        bool enteredDuringTheSave = false;
        QTimer::singleShot(0, &accounts, [&] {
            enteredDuringTheSave = true;
            accounts.saveSecret(QStringLiteral("refinementEndpointApiKey"), QStringLiteral("key-new"));
        });

        accounts.saveSecret(QStringLiteral("refinementEndpointApiKey"), QStringLiteral("key-old"));

        QVERIFY(!enteredDuringTheSave);
        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("refinementEndpointApiKey")), QVariant(true));
        QVERIFY(enteredDuringTheSave);
        QCOMPARE(resolvedRefinementEndpoint(SettingsStore().snapshot().refinement).apiKey, QStringLiteral("key-new"));
    }

    // Moving the endpoint to another server drops its key, and its row says
    // so, though no field asked for the change.
    void anEndpointKeyDroppedByAServerChangeShowsAsNotSaved()
    {
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("refinementEndpointUrl"), QStringLiteral("http://server-a.example/v1"));
        accounts.saveSecret(QStringLiteral("refinementEndpointApiKey"), QStringLiteral("key-a"));
        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("refinementEndpointApiKey")), QVariant(true));

        settings.set(QStringLiteral("refinementEndpointUrl"), QStringLiteral("http://server-c.example/v1"));

        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("refinementEndpointApiKey")), QVariant(false));
    }

    // A key entered while the CLI Proxy preset shows belongs to the proxy's
    // server: the endpoint turns Custom there, and the Custom server saved
    // behind the preset never gets that key. Cleared, the endpoint sends the
    // proxy's key again, being on the proxy's server.
    void anEndpointKeyEnteredOnThePresetBelongsToTheProxysServer()
    {
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);
        SettingsStore &settings = *controller.settings();
        const auto sent = [] { return resolvedRefinementEndpoint(SettingsStore().snapshot().refinement); };
        QTest::ignoreMessage(QtWarningMsg,
                             "keyring unavailable, keeping cliproxy-api-key in the settings file: Keyring write denied");
        settings.edit([](AppSettings &draft) {
            draft.refinement.cliproxyBaseUrl = QStringLiteral("http://proxy-b.example:8317");
            draft.refinement.cliproxyApiKey = QStringLiteral("proxy-key");
            draft.refinement.endpoint = {.baseUrl = QStringLiteral("http://server-a.example/v1")};
            return true;
        });
        const QString fallback =
            "keyring unavailable, keeping refinement-endpoint-key in the settings file: Keyring write denied";
        QTest::ignoreMessage(QtWarningMsg, fallback.toUtf8());
        accounts.saveSecret(QStringLiteral("refinementEndpointApiKey"), QStringLiteral("key-a"));
        settings.edit([](AppSettings &draft) {
            editRefinementEndpoint(draft, {.preset = QStringLiteral("cliproxy")});
            return true;
        });

        QTest::ignoreMessage(QtWarningMsg, fallback.toUtf8());
        accounts.saveSecret(QStringLiteral("refinementEndpointApiKey"), QStringLiteral("key-b"));

        QTRY_COMPARE(sent().apiKey, QStringLiteral("key-b"));
        QCOMPARE(sent().apiBase, QStringLiteral("http://proxy-b.example:8317/v1"));
        settings.edit([](AppSettings &draft) {
            editRefinementEndpoint(draft, {.preset = QString()});
            return true;
        });
        QCOMPARE(sent().apiBase, QStringLiteral("http://proxy-b.example:8317/v1"));

        accounts.clearSecret(QStringLiteral("refinementEndpointApiKey"));
        QTRY_COMPARE(sent().apiKey, QStringLiteral("proxy-key"));
    }

    // SchemaRow's secret field hands what was typed to Accounts and lets go
    // of it; once saved it says so and offers to clear it.
    void aSecretRowIsWriteOnly()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const SecretRowWindow row = showSecretRow(engine);
        QVERIFY(row.field);
        const int password = 2; // TextInput.Password
        QCOMPARE(row.field->property("echoMode").toInt(), password);

        QTest::ignoreMessage(QtWarningMsg, proxyKeyFallbackWarning);
        row.field->setProperty("text", QStringLiteral("proxy-key"));
        QVERIFY(QMetaObject::invokeMethod(row.field, "editingFinished"));
        QTRY_COMPARE(row.field->property("text").toString(), QString());
        QCOMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QStringLiteral("proxy-key"));
        QCOMPARE(row.field->property("placeholderText").toString(), QStringLiteral("Saved"));
        // No property of the field keeps a copy of the saved key.
        const QMetaObject *meta = row.field->metaObject();
        for (int i = 0; i < meta->propertyCount(); ++i) {
            QVERIFY2(!meta->property(i).read(row.field).toString().contains(QStringLiteral("proxy-key")),
                     meta->property(i).name());
        }

        accounts.clearSecret(QStringLiteral("cliproxyApiKey"));
        QTRY_COMPARE(row.field->property("placeholderText").toString(), QStringLiteral("A key the server accepts"));
    }

    // Typing goes on while the keyring makes a save wait, and the field can
    // save again. What was typed meanwhile stays in the field, and
    // the key saved last is the one kept, by the keyring and the daemon both.
    void savesDuringASaveEndWithTheNewest()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const SecretRowWindow row = showSecretRow(engine);
        QVERIFY(row.field);
        QQuickItem *field = row.field;
        for (int save = 0; save < 3; ++save) {
            QTest::ignoreMessage(QtWarningMsg, proxyKeyFallbackWarning);
        }

        field->setProperty("text", QStringLiteral("first-key"));
        QMetaObject::invokeMethod(
            field, [field] { field->setProperty("text", QStringLiteral("typed-meanwhile")); }, Qt::QueuedConnection);
        QVERIFY(QMetaObject::invokeMethod(field, "editingFinished"));
        QTRY_COMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QStringLiteral("first-key"));
        QCOMPARE(field->property("text").toString(), QStringLiteral("typed-meanwhile"));

        field->setProperty("text", QStringLiteral("outer-key"));
        QMetaObject::invokeMethod(
            field,
            [field] {
                field->setProperty("text", QStringLiteral("inner-key"));
                QMetaObject::invokeMethod(field, "editingFinished");
            },
            Qt::QueuedConnection);
        QVERIFY(QMetaObject::invokeMethod(field, "editingFinished"));
        QTRY_COMPARE(field->property("text").toString(), QString());
        QCOMPARE(controller.settings()->secrets()->secret(SecretStore::Secret::CliproxyApiKey),
                 QStringLiteral("inner-key"));
        QCOMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QStringLiteral("inner-key"));
    }

    // A save asked for during another waits its turn, and the field keeps
    // its key until that save goes through: refused, the key stays to retry.
    void aRefusedWaitingSaveKeepsItsKey()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const SecretRowWindow row = showSecretRow(engine, QStringLiteral("openAiApiKey"));
        QVERIFY(row.field);
        QQuickItem *field = row.field;

        field->setProperty("text", QStringLiteral("outer-key"));
        QMetaObject::invokeMethod(
            field,
            [field] {
                field->setProperty("text", QStringLiteral("newer-key"));
                QMetaObject::invokeMethod(field, "editingFinished");
            },
            Qt::QueuedConnection);
        QVERIFY(QMetaObject::invokeMethod(field, "editingFinished"));
        QTRY_VERIFY(accounts.secretErrors().contains(QStringLiteral("openAiApiKey")));
        QCOMPARE(field->property("text").toString(), QStringLiteral("newer-key"));
    }

    // A row's save asked for while a write of the same key from elsewhere
    // waits on the keyring is answered once the keyring answers it: refused,
    // it shows why and the field keeps its key.
    void aRowSaveQueuedBehindAnotherWriteWaitsForItsAnswer()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const SecretRowWindow row = showSecretRow(engine, QStringLiteral("openAiApiKey"));
        QVERIFY(row.field);
        row.field->setProperty("text", QStringLiteral("row-key"));
        QMetaObject::invokeMethod(row.field, "editingFinished", Qt::QueuedConnection);

        controller.settings()->secrets()->saveApiKey(QStringLiteral("other-key"));

        QTRY_COMPARE(accounts.secretErrors().value(QStringLiteral("openAiApiKey")),
                 QVariant(QVariantMap{{QStringLiteral("action"), QStringLiteral("save")},
                                      {QStringLiteral("reason"), QStringLiteral("Keyring write denied")}}));
        QCOMPARE(row.field->property("text").toString(), QStringLiteral("row-key"));
    }

    // A row's save that a save from elsewhere replaced before it was written
    // was never the row's: the keyring's answer to the newer one neither
    // lets go of the field's key nor shows an error on it.
    void aRowSaveReplacedFromElsewhereKeepsItsKey()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const SecretRowWindow row = showSecretRow(engine);
        QVERIFY(row.field);
        SecretStore *secrets = controller.settings()->secrets();
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        for (int save = 0; save < 2; ++save) {
            QTest::ignoreMessage(QtWarningMsg, proxyKeyFallbackWarning);
        }
        row.field->setProperty("text", QStringLiteral("row-key"));
        QMetaObject::invokeMethod(row.field, "editingFinished", Qt::QueuedConnection);
        QMetaObject::invokeMethod(
            secrets, [secrets, proxy] { secrets->saveSecret(proxy, QStringLiteral("other-key")); },
            Qt::QueuedConnection);

        secrets->saveSecret(proxy, QStringLiteral("outer-key"));

        QTRY_COMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QStringLiteral("other-key"));
        QCOMPARE(row.field->property("text").toString(), QStringLiteral("row-key"));
        QVERIFY(!accounts.secretErrors().contains(QStringLiteral("cliproxyApiKey")));
    }

    // A keyring that does not answer leaves a sign-in that needs it unknown,
    // in the keyring's words, rather than "not found".
    void aKeyringTimeoutShowsTheKeyringsError()
    {
        ApplicationController controller(true);
        SettingsStore *store = controller.settings();
        store->edit([](AppSettings &draft) {
            draft.accounts.openAiAuthMode = OpenAiAuthMode::Settings;
            draft.refinement.cliproxyBaseUrl = QStringLiteral("http://127.0.0.1:8317");
            return true;
        });
        store->raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("cliproxy-api-key")});
        QuickAccounts accounts(&controller);
        QuickAccounts::create(nullptr, nullptr);
        QTRY_VERIFY(checked(accounts));

        const QVariantMap openAi = service(accounts, QStringLiteral("openai"));
        QCOMPARE(openAi.value(QStringLiteral("state")).toString(), QStringLiteral("unknown"));
        QCOMPARE(openAi.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("Desktop keyring unavailable for openai/apiKey: Desktop keyring request timed out"));
        const QVariantMap cliproxy = service(accounts, QStringLiteral("cliproxy"));
        QCOMPARE(cliproxy.value(QStringLiteral("state")).toString(), QStringLiteral("unknown"));
        QCOMPARE(cliproxy.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("Desktop keyring unavailable for cliproxy/apiKey: Desktop keyring request timed out"));
        QVERIFY(!notedSignIns(controller).contains(QStringLiteral("openai")));
    }

    // A secret the keyring could not answer for at first shows as saved once
    // a later check finds it.
    void aRecheckReadsWhichSecretsAreSaved()
    {
        ApplicationController controller(true);
        SettingsStore *store = controller.settings();
        store->raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("cliproxy-api-key")});
        QuickAccounts accounts(&controller);
        QuickAccounts::create(nullptr, nullptr);
        QTRY_VERIFY(accounts.savedSecrets().contains(QStringLiteral("speechEndpointApiKey")));
        QVERIFY(!accounts.savedSecrets().contains(QStringLiteral("cliproxyApiKey")));

        // The keyring is out of the way now: Speecher no longer looks there,
        // and the key is in the settings file.
        store->raw().remove(SettingsKeys::SecretsInKeyring);
        store->raw().setValue(SecretStore::settingsKey(SecretStore::Secret::CliproxyApiKey), QStringLiteral("proxy-key"));
        accounts.recheck(QStringLiteral("cliproxy"));
        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("cliproxyApiKey")), QVariant(true));
    }

    // Clearing a key the keyring refused to delete says so, for the OpenAI
    // key and for one Speecher only hides until the deletion goes through.
    void aRefusedDeletionReportsAnError()
    {
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE"); });
        // Before the controller's first read of the settings, which keeps
        // what it finds for each key.
        SettingsStore().raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("cliproxy-api-key")});
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);
        const QVariant refused(QVariantMap{{QStringLiteral("action"), QStringLiteral("clear")},
                                           {QStringLiteral("reason"), QStringLiteral("Keyring deletion denied")}});

        accounts.clearSecret(QStringLiteral("openAiApiKey"));
        QTRY_COMPARE(accounts.secretErrors().value(QStringLiteral("openAiApiKey")), refused);

        accounts.clearSecret(QStringLiteral("cliproxyApiKey"));
        QTRY_COMPARE(accounts.secretErrors().value(QStringLiteral("cliproxyApiKey")), refused);
    }

    // A key the keyring refused to delete stays clearable after a check
    // reads it as hidden rather than saved, so the clear can be retried.
    void aRefusedClearCanBeRetried()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const SecretRowWindow row = showSecretRow(engine);
        QVERIFY(row.clear);
        QTest::ignoreMessage(QtWarningMsg, proxyKeyFallbackWarning);
        accounts.saveSecret(QStringLiteral("cliproxyApiKey"), QStringLiteral("proxy-key"));
        QTRY_VERIFY(row.clear->isVisible());

        controller.settings()->raw().setValue(SettingsKeys::SecretsInKeyring,
                                              QStringList{QStringLiteral("cliproxy-api-key")});
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE"); });
        accounts.clearSecret(QStringLiteral("cliproxyApiKey"));
        QTRY_VERIFY(accounts.secretErrors().contains(QStringLiteral("cliproxyApiKey")));
        accounts.recheck(QStringLiteral("cliproxy"));
        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("cliproxyApiKey")), QVariant(false));
        QVERIFY(row.clear->isVisible());
    }

    // Renewing a sign-in shows everything it rewrote: OpenAI's renewed CLI
    // Proxy API account is current in the account list as well.
    void aRenewalRefreshesTheAccountFiles()
    {
        FakeServer server;
        server.route("POST /token", httpResponse("200 OK", "application/json",
                                                 R"({"access_token": "rotated", "expires_in": 3600})"));
        qputenv("SPEECHER_CLIPROXY_CODEX_TOKEN_URL", (server.origin() + QStringLiteral("/token")).toUtf8());
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_CLIPROXY_CODEX_TOKEN_URL"); });
        ApplicationController controller(true);
        QTemporaryDir dir;
        const QString fileName = useExpiredCodexAccount(*controller.settings(), dir.path());
        QVERIFY(!fileName.isEmpty());
        QuickAccounts accounts(&controller);
        QuickAccounts::create(nullptr, nullptr);
        QTRY_VERIFY(checked(accounts));
        QCOMPARE(serviceState(accounts, QStringLiteral("openai")), QStringLiteral("expired"));
        QCOMPARE(accountState(accounts, fileName), QStringLiteral("expired"));

        accounts.renew(QStringLiteral("openai"));
        QTRY_VERIFY(checked(accounts));
        QCOMPARE(serviceState(accounts, QStringLiteral("openai")), QStringLiteral("signedIn"));
        QCOMPARE(serviceState(accounts, QStringLiteral("cliproxy")), QStringLiteral("signedIn"));
        QCOMPARE(accountState(accounts, fileName), QStringLiteral("signedIn"));
        QCOMPARE(notedSignIns(controller).value(QStringLiteral("openai")), true);
        // Codex dictates with the account renewed, and is checked again.
        QTRY_COMPARE(notedSignIns(controller).value(QStringLiteral("codex")), true);
    }

    // Checking everything while a renewal waits on the network shows what
    // the files held then; once the renewal rewrites them, they are checked
    // again rather than left showing the token expired.
    void aCheckDuringARenewalIsFollowedByAFreshOne()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QTcpSocket *request = nullptr;
        connect(&server, &QTcpServer::newConnection, this, [&] { request = server.nextPendingConnection(); });
        qputenv("SPEECHER_CLIPROXY_CODEX_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/token").arg(server.serverPort()).toUtf8());
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_CLIPROXY_CODEX_TOKEN_URL"); });
        ApplicationController controller(true);
        QTemporaryDir dir;
        const QString fileName = useExpiredCodexAccount(*controller.settings(), dir.path());
        QVERIFY(!fileName.isEmpty());
        QuickAccounts accounts(&controller);
        QuickAccounts::create(nullptr, nullptr);
        QTRY_VERIFY(checked(accounts));

        accounts.renew(QStringLiteral("openai"));
        QTRY_VERIFY(request && hasWholeRequest(request));
        for (const QVariant &service : accounts.services()) {
            accounts.recheck(service.toMap().value(QStringLiteral("id")).toString());
        }
        QTRY_VERIFY(checked(accounts));
        QCOMPARE(serviceState(accounts, QStringLiteral("openai")), QStringLiteral("expired"));

        request->write(httpResponse("200 OK", "application/json", R"({"access_token": "rotated", "expires_in": 3600})"));
        request->disconnectFromHost();
        QTRY_COMPARE(serviceState(accounts, QStringLiteral("openai")), QStringLiteral("signedIn"));
        QTRY_COMPARE(accountState(accounts, fileName), QStringLiteral("signedIn"));
    }

private:
    QTemporaryDir m_home;
    QByteArray m_openAiKey;
};

int runQuickAccountsTests(int argc, char **argv)
{
    QuickAccountsTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_accounts.moc"
