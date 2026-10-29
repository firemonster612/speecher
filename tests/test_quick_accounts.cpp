#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/ProviderAvailability.h"
#include "common/test_auth.h"
#include "common/test_http.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/quick/host/QuickAccounts.h"
#include "frontend/quick/host/QuickLibrary.h"
#include "frontend/quick/host/QuickModels.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>

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
    store.setCliproxyOauthDir(directory);
    store.setOpenAiAuthMode(QStringLiteral("cliproxy"));
    store.setOpenAiCliproxyAccount(fileName);
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
        store->setCliproxyOauthDir(dir.path());
        store->setOpenAiAuthMode(QStringLiteral("env"));
        store->setOpenAiCliproxyAccount(QStringLiteral("codex-gone@example.com.json"));
        store->setAnthropicAuthMode(QStringLiteral("cliproxy"));
        store->setAnthropicCliproxyAccount(QStringLiteral("claude-a@example.com.json"));

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

    // A service's card on Accounts follows its checks rather than keeping
    // what it showed when it was made.
    void aServiceCardFollowsItsChecks()
    {
        ApplicationController controller(true);
        controller.settings()->setOpenAiAuthMode(QStringLiteral("env"));
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

    // A card's state is toned only while something signs in through it, a
    // writing profile's own service included.
    void aServiceCardTonesOnlyWhatIsUsed()
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
        controller.settings()->setCliproxyOauthDir(dir.path());
        controller.settings()->setOpenAiAuthMode(QStringLiteral("env"));
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
        QCOMPARE(SettingsStore().cliproxyApiKey(), QStringLiteral("proxy-key"));
        QCOMPARE(accounts.savedSecrets().value(QStringLiteral("cliproxyApiKey")), QVariant(true));
        QVERIFY(accounts.secretErrors().isEmpty());

        // The OpenAI key never falls back to the settings file; why it was
        // not saved is the keyring's answer.
        accounts.saveSecret(QStringLiteral("openAiApiKey"), QStringLiteral("sk-test-key"));
        QCOMPARE(accounts.secretErrors().value(QStringLiteral("openAiApiKey")),
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
        QCOMPARE(SettingsStore().cliproxyApiKey(), QString());
        QCOMPARE(accounts.savedSecrets().value(QStringLiteral("cliproxyApiKey")), QVariant(false));
    }

    // SchemaRow's secret field hands what was typed to Accounts and lets go
    // of it; once saved it says so and offers to clear it.
    void aSecretRowIsWriteOnly()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QQmlEngine engine;
        const SecretRowWindow row = showSecretRow(engine);
        QVERIFY(row.field);
        const int password = 2; // TextInput.Password
        QCOMPARE(row.field->property("echoMode").toInt(), password);

        QTest::ignoreMessage(QtWarningMsg, proxyKeyFallbackWarning);
        row.field->setProperty("text", QStringLiteral("proxy-key"));
        QVERIFY(QMetaObject::invokeMethod(row.field, "editingFinished"));
        QCOMPARE(SettingsStore().cliproxyApiKey(), QStringLiteral("proxy-key"));
        QCOMPARE(row.field->property("text").toString(), QString());
        QCOMPARE(row.field->property("placeholderText").toString(), QStringLiteral("Saved"));
        // No property of the field keeps a copy of the saved key.
        const QMetaObject *meta = row.field->metaObject();
        for (int i = 0; i < meta->propertyCount(); ++i) {
            QVERIFY2(!meta->property(i).read(row.field).toString().contains(QStringLiteral("proxy-key")),
                     meta->property(i).name());
        }

        accounts.clearSecret(QStringLiteral("cliproxyApiKey"));
        QCOMPARE(row.field->property("placeholderText").toString(), QStringLiteral("A key the server accepts"));
    }

    // The keyring's wait runs the event loop, where typing goes on and the
    // field can save again. What was typed meanwhile stays in the field, and
    // the key saved last is the one kept, by the keyring and the daemon both.
    void savesDuringASaveEndWithTheNewest()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
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
        QCOMPARE(SettingsStore().cliproxyApiKey(), QStringLiteral("first-key"));
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
        QCOMPARE(controller.settings()->secrets()->secret(SecretStore::Secret::CliproxyApiKey),
                 QStringLiteral("inner-key"));
        QCOMPARE(SettingsStore().cliproxyApiKey(), QStringLiteral("inner-key"));
        QCOMPARE(field->property("text").toString(), QString());
    }

    // A save asked for during another waits its turn, and the field keeps
    // its key until that save goes through: refused, the key stays to retry.
    void aRefusedWaitingSaveKeepsItsKey()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
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
        QVERIFY(accounts.secretErrors().contains(QStringLiteral("openAiApiKey")));
        QCOMPARE(field->property("text").toString(), QStringLiteral("newer-key"));
    }

    // A keyring that does not answer leaves a sign-in that needs it unknown,
    // in the keyring's words, rather than "not found".
    void aKeyringTimeoutShowsTheKeyringsError()
    {
        ApplicationController controller(true);
        SettingsStore *store = controller.settings();
        store->setOpenAiAuthMode(QStringLiteral("settings"));
        store->setCliproxyBaseUrl(QStringLiteral("http://127.0.0.1:8317"));
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
        store->raw().setValue(SecretStore::settingsKey(SecretStore::Secret::CliproxyApiKey), QStringLiteral("proxy-key"));
        QuickAccounts accounts(&controller);
        QuickAccounts::create(nullptr, nullptr);
        QTRY_VERIFY(accounts.savedSecrets().contains(QStringLiteral("speechEndpointApiKey")));
        QVERIFY(!accounts.savedSecrets().contains(QStringLiteral("cliproxyApiKey")));

        // The keyring is out of the way now: Speecher no longer looks there.
        store->raw().remove(SettingsKeys::SecretsInKeyring);
        accounts.recheck(QStringLiteral("cliproxy"));
        QTRY_COMPARE(accounts.savedSecrets().value(QStringLiteral("cliproxyApiKey")), QVariant(true));
    }

    // Clearing a key the keyring refused to delete says so, for the OpenAI
    // key and for one Speecher only hides until the deletion goes through.
    void aRefusedDeletionReportsAnError()
    {
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE"); });
        ApplicationController controller(true);
        QuickAccounts accounts(&controller);
        const QVariant refused(QVariantMap{{QStringLiteral("action"), QStringLiteral("clear")},
                                           {QStringLiteral("reason"), QStringLiteral("Keyring deletion denied")}});

        accounts.clearSecret(QStringLiteral("openAiApiKey"));
        QCOMPARE(accounts.secretErrors().value(QStringLiteral("openAiApiKey")), refused);

        controller.settings()->raw().setValue(SettingsKeys::SecretsInKeyring,
                                              QStringList{QStringLiteral("cliproxy-api-key")});
        accounts.clearSecret(QStringLiteral("cliproxyApiKey"));
        QCOMPARE(accounts.secretErrors().value(QStringLiteral("cliproxyApiKey")), refused);
    }

    // A key the keyring refused to delete stays clearable after a check
    // reads it as hidden rather than saved, so the clear can be retried.
    void aRefusedClearCanBeRetried()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAccounts accounts(&controller);
        QQmlEngine engine;
        const SecretRowWindow row = showSecretRow(engine);
        QVERIFY(row.clear);
        QTest::ignoreMessage(QtWarningMsg, proxyKeyFallbackWarning);
        accounts.saveSecret(QStringLiteral("cliproxyApiKey"), QStringLiteral("proxy-key"));
        QVERIFY(row.clear->isVisible());

        controller.settings()->raw().setValue(SettingsKeys::SecretsInKeyring,
                                              QStringList{QStringLiteral("cliproxy-api-key")});
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE"); });
        accounts.clearSecret(QStringLiteral("cliproxyApiKey"));
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
