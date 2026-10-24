#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "common/test_auth.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickAccounts.h"
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
        QCOMPARE(serviceState(accounts, QStringLiteral("anthropic")), QStringLiteral("expired"));
        const QVariantMap cliproxy = service(accounts, QStringLiteral("cliproxy"));
        QCOMPARE(cliproxy.value(QStringLiteral("state")).toString(), QStringLiteral("signedIn"));
        QVERIFY(cliproxy.value(QStringLiteral("detail")).toString().contains(dir.path()));

        const QVariantList files = accounts.cliproxyAccounts();
        QCOMPARE(files.size(), 4);
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
        QCOMPARE(accounts.secretErrors().value(QStringLiteral("openAiApiKey")).toString(),
                 QStringLiteral("Keyring write denied"));
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
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Speecher.App
            Window {
                width: 640; height: 120; visible: true
                SchemaRow { rowId: "cliproxyApiKey"; width: parent.width }
            }
        )", QUrl());
        std::unique_ptr<QObject> created(component.create());
        auto *window = qobject_cast<QQuickWindow *>(created.get());
        QVERIFY2(window, qPrintable(component.errorString()));
        QQuickItem *field = nullptr;
        for (QQuickItem *item : window->findChildren<QQuickItem *>()) {
            if (QByteArray(item->metaObject()->className()).startsWith("TextField_")) {
                field = item;
            }
        }
        QVERIFY(field);
        const int password = 2; // TextInput.Password
        QCOMPARE(field->property("echoMode").toInt(), password);

        QTest::ignoreMessage(QtWarningMsg,
                             "keyring unavailable, keeping cliproxy-api-key in the settings file: Keyring write denied");
        field->setProperty("text", QStringLiteral("proxy-key"));
        QVERIFY(QMetaObject::invokeMethod(field, "editingFinished"));
        QCOMPARE(SettingsStore().cliproxyApiKey(), QStringLiteral("proxy-key"));
        QCOMPARE(field->property("text").toString(), QString());
        QCOMPARE(field->property("placeholderText").toString(), QStringLiteral("Saved"));

        accounts.clearSecret(QStringLiteral("cliproxyApiKey"));
        QCOMPARE(field->property("placeholderText").toString(), QStringLiteral("A key the server accepts"));
    }

private:
    QByteArray m_openAiKey;
};

int runQuickAccountsTests(int argc, char **argv)
{
    QuickAccountsTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_accounts.moc"
