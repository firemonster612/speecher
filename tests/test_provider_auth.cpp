#include "common/test_prelude.h"
#include <QScopeGuard>
#include "common/test_http.h"
#include "common/test_auth.h"
#include "frontend/ProviderOptions.h"
#include "providers/ClaudeCredentialStorage.h"

#ifdef Q_OS_MACOS
#include <Security/Security.h>
#include <QUuid>
#include <QProcess>
#endif

using namespace speecher::test;

#ifdef Q_OS_MACOS
// A unique account ensures these tests never query a user's Claude login.
class DummyClaudeKeychain {
public:
    DummyClaudeKeychain(QByteArray config = {}, QByteArray serviceName = "Claude Code-credentials")
        : previousUser(qgetenv("USER")), previousConfig(qgetenv("CLAUDE_CONFIG_DIR")),
          account("speecher-test-" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces)),
          service(std::move(serviceName))
    {
        qputenv("USER", account);
        config.isEmpty() ? qunsetenv("CLAUDE_CONFIG_DIR") : qputenv("CLAUDE_CONFIG_DIR", config);
    }
    ~DummyClaudeKeychain()
    {
        SecKeychainItemRef item = nullptr;
        if (SecKeychainFindGenericPassword(nullptr, service.size(), service.constData(),
                                          account.size(), account.constData(), nullptr, nullptr, &item) == errSecSuccess) {
            SecKeychainItemDelete(item);
            CFRelease(item);
        }
        previousUser.isNull() ? qunsetenv("USER") : qputenv("USER", previousUser);
        previousConfig.isNull() ? qunsetenv("CLAUDE_CONFIG_DIR") : qputenv("CLAUDE_CONFIG_DIR", previousConfig);
    }
    bool write(const QByteArray &bytes)
    {
        SecKeychainItemRef item = nullptr;
        const OSStatus found = SecKeychainFindGenericPassword(nullptr, service.size(), service.constData(),
                                                              account.size(), account.constData(), nullptr, nullptr, &item);
        if (found == errSecSuccess) {
            const OSStatus status = SecKeychainItemModifyAttributesAndData(item, nullptr, bytes.size(), bytes.constData());
            CFRelease(item);
            return status == errSecSuccess;
        }
        // Match Claude Code: only the Apple security tool is trusted to decrypt,
        // and its apple-tool partition owns the item. A native Speecher read
        // would prompt here even with an unchanged designated requirement.
        return QProcess::execute(QStringLiteral("/usr/bin/security"),
                                 {QStringLiteral("add-generic-password"), QStringLiteral("-s"),
                                  QString::fromUtf8(service), QStringLiteral("-a"), QString::fromUtf8(account),
                                  QStringLiteral("-w"), QString::fromUtf8(bytes), QStringLiteral("-T"),
                                  QStringLiteral("/usr/bin/security")}) == 0;
    }
    QByteArray read() const
    {
        QProcess process;
        process.start(QStringLiteral("/usr/bin/security"),
                      {QStringLiteral("find-generic-password"), QStringLiteral("-s"), QString::fromUtf8(service),
                       QStringLiteral("-a"), QString::fromUtf8(account), QStringLiteral("-w")});
        if (!process.waitForFinished(5000) || process.exitCode() != 0) return {};
        QByteArray bytes = process.readAllStandardOutput();
        if (bytes.endsWith('\n')) bytes.chop(1);
        const QByteArray decoded = QByteArray::fromHex(bytes);
        if (!bytes.isEmpty() && decoded.toHex() == bytes.toLower()) bytes = decoded;
        return bytes;
    }
    QString path() const { return QDir::homePath() + QStringLiteral("/.claude/.credentials.json"); }
    QByteArray previousUser, previousConfig, account;
    const QByteArray service;
};
#endif

class ProviderAuthTests : public QObject {
    Q_OBJECT

private slots:
#ifdef Q_OS_MACOS
    void claudeKeychainReadAndRefreshPreserveBytes()
    {
        Boolean interactionAllowed = true;
        QCOMPARE(SecKeychainGetUserInteractionAllowed(&interactionAllowed), errSecSuccess);
        QCOMPARE(SecKeychainSetUserInteractionAllowed(false), errSecSuccess);
        const auto restoreInteraction = qScopeGuard([&] {
            SecKeychainSetUserInteractionAllowed(interactionAllowed);
        });
        DummyClaudeKeychain keychain;
        const QByteArray initial = " {\"token\":\"dummy quoted \\\" token\"} \n\n";
        QVERIFY(keychain.write(initial));
        const auto nativeReadStatus = [&] {
            UInt32 length = 0;
            void *data = nullptr;
            const OSStatus status = SecKeychainFindGenericPassword(nullptr,
                keychain.service.size(), keychain.service.constData(),
                keychain.account.size(), keychain.account.constData(), &length, &data, nullptr);
            if (data) SecKeychainItemFreeContent(nullptr, data);
            return status;
        };
        const OSStatus denied = nativeReadStatus();
        QVERIFY(denied == errSecAuthFailed || denied == errSecInteractionNotAllowed);
        ClaudeCredentialStorage storage(keychain.path());
        QString error;
        QCOMPARE(storage.read(&error), initial);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QByteArray rotated = "{\"token\":\"dummy rotated token\"}";
        QVERIFY2(storage.write(rotated, &error), qPrintable(error));
        QCOMPARE(storage.read(&error), rotated);
        QCOMPARE(keychain.read(), rotated);
        // Refresh must not grant the calling build access or replace the item.
        QCOMPARE(nativeReadStatus(), denied);

        SecKeychainItemRef item = nullptr;
        QCOMPARE(SecKeychainFindGenericPassword(nullptr, keychain.service.size(), keychain.service.constData(),
                                                keychain.account.size(), keychain.account.constData(),
                                                nullptr, nullptr, &item), errSecSuccess);
        QCOMPARE(SecKeychainItemDelete(item), errSecSuccess);
        CFRelease(item);
        QVERIFY(storage.read(&error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!error.contains(QStringLiteral("dummy")));
        QVERIFY(!storage.write(rotated, &error));
    }

    void claudeCredentialsReadKeychainThroughSecurity_data()
    {
        QTest::addColumn<QByteArray>("config");
        QTest::addColumn<QByteArray>("service");
        QTest::newRow("default") << QByteArray{} << QByteArray("Claude Code-credentials");
        QTest::newRow("custom config") << QByteArray("/tmp/speecher-claude-test-config")
                                     << QByteArray("Claude Code-credentials-d27569d7");
    }

    void claudeCredentialsReadKeychainThroughSecurity()
    {
        QFETCH(QByteArray, config);
        QFETCH(QByteArray, service);
        DummyClaudeKeychain keychain(config, service);
        QVERIFY(keychain.write(QJsonDocument(QJsonObject{
            {QStringLiteral("claudeAiOauth"), QJsonObject{
                {QStringLiteral("accessToken"), QStringLiteral("dummy-keychain-token")},
                {QStringLiteral("expiresAt"), double(QDateTime::currentMSecsSinceEpoch() + 3600000)}
            }}
        }).toJson()));
        const auto result = ClaudeCredentials::load(keychain.path());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.accessToken, QStringLiteral("dummy-keychain-token"));
        QTemporaryDir directory;
        const QString explicitPath = directory.filePath(QStringLiteral("credentials.json"));
        QVERIFY(writeJsonCredentials(explicitPath, QStringLiteral("explicit-file-token"),
                                     QDateTime::currentDateTimeUtc().addSecs(3600)));
        QCOMPARE(ClaudeCredentials::load(explicitPath).accessToken, QStringLiteral("explicit-file-token"));
    }
#endif

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

    void claudeCredentialsOauthRefresh_data()
    {
        QTest::addColumn<bool>("nativeKeychain");
        QTest::newRow("file") << false;
#ifdef Q_OS_MACOS
        QTest::newRow("keychain") << true;
#endif
    }

    void claudeCredentialsOauthRefresh()
    {
        QFETCH(bool, nativeKeychain);
#ifdef Q_OS_MACOS
        std::unique_ptr<DummyClaudeKeychain> keychain;
        if (nativeKeychain) keychain = std::make_unique<DummyClaudeKeychain>();
#else
        Q_UNUSED(nativeKeychain);
#endif
        QTemporaryDir dir;
        const QString credentialsPath = dir.filePath(QStringLiteral("credentials.json"));
        QFile credentialsFile(credentialsPath);
        QVERIFY(credentialsFile.open(QIODevice::WriteOnly));
        credentialsFile.write(QJsonDocument(QJsonObject{
                                                {QStringLiteral("unrelated"), true},
                                                {QStringLiteral("claudeAiOauth"),
                                                 QJsonObject{
                                                     {QStringLiteral("accessToken"), QStringLiteral("expired-token")},
                                                     {QStringLiteral("refreshToken"), QStringLiteral("old-refresh-token")},
                                                     {QStringLiteral("expiresAt"),
                                                      double(QDateTime::currentDateTimeUtc().addSecs(-60).toMSecsSinceEpoch())},
                                                     {QStringLiteral("scopes"),
                                                      QJsonArray{
                                                          QStringLiteral("user:profile"),
                                                          QStringLiteral("user:inference"),
                                                      }},
                                                     {QStringLiteral("subscriptionType"), QStringLiteral("pro")},
                                                 }},
                                            })
                                  .toJson());
        credentialsFile.close();

        QString loadPath = credentialsPath;
#ifdef Q_OS_MACOS
        if (keychain) {
            QFile initial(credentialsPath);
            QVERIFY(initial.open(QIODevice::ReadOnly));
            QVERIFY(keychain->write(initial.readAll()));
            loadPath = keychain->path();
        }
#endif
        QVERIFY(ClaudeCredentials::requiresRefresh(loadPath));

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        qputenv("SPEECHER_TEST_CLAUDE_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/token").arg(server.serverPort()).toUtf8());
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CLAUDE_TOKEN_URL");
        });

        auto refresh = std::async(std::launch::async, [&] {
            return ClaudeCredentials::load(loadPath, true);
        });
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(socket);
        const QByteArray request = readHttpRequest(socket, 1000);
        const int headerEnd = request.indexOf("\r\n\r\n");
        QVERIFY2(headerEnd >= 0, request.constData());
        QCOMPARE(request.left(request.indexOf('\n')).trimmed(), QByteArrayLiteral("POST /token HTTP/1.1"));
        QVERIFY(request.left(headerEnd).toLower().contains(QByteArrayLiteral("content-type: application/json")));

        const int contentLength = httpContentLength(request.left(headerEnd));
        const QJsonObject body = QJsonDocument::fromJson(request.mid(headerEnd + 4, contentLength)).object();
        QCOMPARE(body.value(QStringLiteral("grant_type")).toString(), QStringLiteral("refresh_token"));
        QCOMPARE(body.value(QStringLiteral("refresh_token")).toString(), QStringLiteral("old-refresh-token"));
        QCOMPARE(body.value(QStringLiteral("client_id")).toString(),
                 QStringLiteral("9d1c250a-e61b-44d9-88ed-5944d1962f5e"));
        QCOMPARE(body.value(QStringLiteral("scope")).toString(),
                 QStringLiteral("user:profile user:inference"));

#ifdef Q_OS_MACOS
        // A changed environment during the HTTP round trip must not redirect
        // refreshed credentials into another account or service.
        if (keychain) {
            qputenv("USER", keychain->account + "-changed");
            qputenv("CLAUDE_CONFIG_DIR", "/dummy/changed-during-refresh");
        }
#endif
        const QByteArray responseBody = QJsonDocument(QJsonObject{
                                                          {QStringLiteral("access_token"), QStringLiteral("refreshed-token")},
                                                          {QStringLiteral("refresh_token"), QStringLiteral("rotated-refresh-token")},
                                                          {QStringLiteral("expires_in"), 3600},
                                                          {QStringLiteral("scope"),
                                                           QStringLiteral("user:profile user:inference")},
                                                      })
                                            .toJson(QJsonDocument::Compact);
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                      + QByteArray::number(responseBody.size())
                      + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                      + responseBody);
        QVERIFY(socket->waitForBytesWritten(1000));
        socket->disconnectFromHost();

        const ClaudeCredentialResult result = refresh.get();
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.accessToken, QStringLiteral("refreshed-token"));
        QCOMPARE(result.refreshToken, QStringLiteral("rotated-refresh-token"));
        QVERIFY(result.expiresAt > QDateTime::currentDateTimeUtc().addSecs(3500));

        QVERIFY(credentialsFile.open(QIODevice::ReadOnly));
        QByteArray savedBytes = credentialsFile.readAll();
#ifdef Q_OS_MACOS
        if (keychain) savedBytes = keychain->read();
#endif
        const QJsonObject saved = QJsonDocument::fromJson(savedBytes).object();
        QVERIFY(saved.value(QStringLiteral("unrelated")).toBool());
        const QJsonObject savedOauth = saved.value(QStringLiteral("claudeAiOauth")).toObject();
        QCOMPARE(savedOauth.value(QStringLiteral("accessToken")).toString(), QStringLiteral("refreshed-token"));
        QCOMPARE(savedOauth.value(QStringLiteral("refreshToken")).toString(), QStringLiteral("rotated-refresh-token"));
        QCOMPARE(savedOauth.value(QStringLiteral("subscriptionType")).toString(), QStringLiteral("pro"));
    }

    void claudeCredentialsOauthRefreshFailureIsSanitized()
    {
        QTemporaryDir dir;
        const QString credentialsPath = dir.filePath(QStringLiteral("credentials.json"));
        QFile credentialsFile(credentialsPath);
        QVERIFY(credentialsFile.open(QIODevice::WriteOnly));
        credentialsFile.write(QJsonDocument(QJsonObject{
                                                {QStringLiteral("claudeAiOauth"),
                                                 QJsonObject{
                                                     {QStringLiteral("accessToken"), QStringLiteral("expired-token")},
                                                     {QStringLiteral("refreshToken"), QStringLiteral("secret-refresh-token")},
                                                     {QStringLiteral("expiresAt"),
                                                      double(QDateTime::currentDateTimeUtc().addSecs(-60).toMSecsSinceEpoch())},
                                                 }},
                                            })
                                  .toJson());
        credentialsFile.close();

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        qputenv("SPEECHER_TEST_CLAUDE_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/token").arg(server.serverPort()).toUtf8());
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CLAUDE_TOKEN_URL");
        });

        auto refresh = std::async(std::launch::async, [&] {
            return ClaudeCredentials::load(credentialsPath, true);
        });
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(socket);
        readHttpRequest(socket, 1000);
        const QByteArray responseBody = QByteArrayLiteral(
            "{\"error\":\"invalid_grant\",\"error_description\":\"secret-refresh-token was rejected\"}");
        socket->write(QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: ")
                      + QByteArray::number(responseBody.size())
                      + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                      + responseBody);
        QVERIFY(socket->waitForBytesWritten(1000));
        socket->disconnectFromHost();

        const ClaudeCredentialResult result = refresh.get();
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("/login")));
        QVERIFY(!result.error.contains(QStringLiteral("secret-refresh-token")));
    }

    void claudeCredentialsRefreshDoesNotOverwriteNewerLogin_data()
    {
        QTest::addColumn<bool>("nativeKeychain");
        QTest::newRow("file") << false;
#ifdef Q_OS_MACOS
        QTest::newRow("keychain") << true;
#endif
    }

    void claudeCredentialsRefreshDoesNotOverwriteNewerLogin()
    {
        QFETCH(bool, nativeKeychain);
#ifdef Q_OS_MACOS
        std::unique_ptr<DummyClaudeKeychain> keychain;
        if (nativeKeychain) keychain = std::make_unique<DummyClaudeKeychain>();
#else
        Q_UNUSED(nativeKeychain);
#endif
        QTemporaryDir dir;
        const QString credentialsPath = dir.filePath(QStringLiteral("credentials.json"));
        QVERIFY(writeJsonCredentials(credentialsPath,
                                     QStringLiteral("expired-token"),
                                     QDateTime::currentDateTimeUtc().addSecs(-60),
                                     QStringLiteral("old-refresh-token")));

        QString loadPath = credentialsPath;
#ifdef Q_OS_MACOS
        if (keychain) {
            QFile initial(credentialsPath);
            QVERIFY(initial.open(QIODevice::ReadOnly));
            QVERIFY(keychain->write(initial.readAll()));
            loadPath = keychain->path();
        }
#endif
        QVERIFY(ClaudeCredentials::requiresRefresh(loadPath));

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        qputenv("SPEECHER_TEST_CLAUDE_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/token").arg(server.serverPort()).toUtf8());
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CLAUDE_TOKEN_URL");
        });

        auto refresh = std::async(std::launch::async, [&] {
            return ClaudeCredentials::load(loadPath, true);
        });
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(socket);
        readHttpRequest(socket, 1000);
        const bool lockWasHeld = QFileInfo::exists(credentialsPath + QStringLiteral(".lock"));

        QVERIFY(writeJsonCredentials(credentialsPath,
                                     QStringLiteral("newer-token"),
                                     QDateTime::currentDateTimeUtc().addSecs(3600),
                                     QStringLiteral("newer-refresh-token")));
#ifdef Q_OS_MACOS
        if (keychain) {
            QFile newer(credentialsPath);
            QVERIFY(newer.open(QIODevice::ReadOnly));
            QVERIFY(keychain->write(newer.readAll()));
        }
#endif
        const QByteArray responseBody = QByteArrayLiteral(
            R"({"access_token":"stale-refreshed-token","refresh_token":"stale-rotated-token","expires_in":3600})");
        socket->write(QByteArrayLiteral(
                          "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                      + QByteArray::number(responseBody.size())
                      + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                      + responseBody);
        QVERIFY(socket->waitForBytesWritten(1000));
        socket->disconnectFromHost();

        const ClaudeCredentialResult result = refresh.get();
        if (!nativeKeychain) QVERIFY(lockWasHeld);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("changed during refresh")));

        const ClaudeCredentialResult saved = ClaudeCredentials::load(loadPath);
        QVERIFY(saved.ok);
        QCOMPARE(saved.accessToken, QStringLiteral("newer-token"));
        QCOMPARE(saved.refreshToken, QStringLiteral("newer-refresh-token"));
    }

    void claudeInstalledVersion()
    {
        const QString version = ClaudeCredentials::installedVersion();
        if (!version.isEmpty()) {
            QVERIFY(QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+")).match(version).hasMatch());
        }
    }

    void claudeInstalledVersionIsCachedUntilExecutableChanges()
    {
#ifdef Q_OS_WIN
        QSKIP("This test uses a POSIX shell script as the fake Claude executable");
#else
        QTemporaryDir dir;
        const QString countPath = dir.filePath(QStringLiteral("version-count"));
        const QString fakeClaude = writeFakeClaudeScript(
            dir.filePath(QStringLiteral("claude-fake")),
            QStringLiteral("printf x >> \"$SPEECHER_TEST_VERSION_COUNT\"\nprintf '1.2.3\\n'\n"));
        QVERIFY(!fakeClaude.isEmpty());

        qputenv("SPEECHER_TEST_CLAUDE_EXECUTABLE", QFile::encodeName(fakeClaude));
        qputenv("SPEECHER_TEST_VERSION_COUNT", QFile::encodeName(countPath));
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CLAUDE_EXECUTABLE");
            qunsetenv("SPEECHER_TEST_VERSION_COUNT");
        });

        QCOMPARE(ClaudeCredentials::installedVersion(), QStringLiteral("1.2.3"));
        QCOMPARE(ClaudeCredentials::installedVersion(), QStringLiteral("1.2.3"));
        QFile count(countPath);
        QVERIFY(count.open(QIODevice::ReadOnly));
        QCOMPARE(count.readAll(), QByteArrayLiteral("x"));
        count.close();

        QFile executable(fakeClaude);
        QVERIFY(executable.open(QIODevice::ReadWrite));
        QVERIFY(executable.setFileTime(QDateTime::currentDateTimeUtc().addSecs(5),
                                       QFileDevice::FileModificationTime));
        executable.close();

        QCOMPARE(ClaudeCredentials::installedVersion(), QStringLiteral("1.2.3"));
        QVERIFY(count.open(QIODevice::ReadOnly));
        QCOMPARE(count.readAll(), QByteArrayLiteral("xx"));
#endif
    }

    void codexOauthRefreshesExpiredToken()
    {
        QTemporaryDir dir;
        QVERIFY(writeCodexAuth(dir.path(), jwtWithExpiry(QDateTime::currentDateTimeUtc().addSecs(-60))));

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray requestBody;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            const QByteArray request = readHttpRequest(socket, 1000);
            requestBody = request.mid(request.indexOf("\r\n\r\n") + 4);
            const QByteArray payload = QJsonDocument(QJsonObject{
                {QStringLiteral("access_token"), QStringLiteral("REFRESHED_TOKEN")},
                {QStringLiteral("refresh_token"), QStringLiteral("rotated-codex-refresh")},
                {QStringLiteral("id_token"), QStringLiteral("new-id-token")},
                {QStringLiteral("expires_in"), QStringLiteral("7200")},
            }).toJson(QJsonDocument::Compact);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
            socket->flush();
        });

        qputenv("SPEECHER_TEST_CODEX_AUTH_PATH",
                QFile::encodeName(dir.filePath(QStringLiteral(".codex/auth.json"))));
        qputenv("SPEECHER_CODEX_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/oauth/token").arg(server.serverPort()).toUtf8());
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CODEX_AUTH_PATH");
            qunsetenv("SPEECHER_CODEX_TOKEN_URL");
        });

        OpenAiAuthProvider provider(nullptr, QStringLiteral("codex_oauth"));
        QVERIFY(provider.requiresCodexOauthRefresh());
        const OpenAiAuth auth = provider.resolve();
        QVERIFY2(auth.ok, qPrintable(auth.status));
        QCOMPARE(auth.bearerToken, QStringLiteral("REFRESHED_TOKEN"));
        QVERIFY(!provider.requiresCodexOauthRefresh());

        const QJsonObject body = QJsonDocument::fromJson(requestBody).object();
        QCOMPARE(body.value(QStringLiteral("grant_type")).toString(), QStringLiteral("refresh_token"));
        QCOMPARE(body.value(QStringLiteral("refresh_token")).toString(), QStringLiteral("codex-refresh-token"));
        QCOMPARE(body.value(QStringLiteral("client_id")).toString(), CliProxyCredentials::codexClientId());
        QCOMPARE(body.value(QStringLiteral("scope")).toString(), QStringLiteral("openid profile email"));

        // Rotated tokens land back in ~/.codex/auth.json.
        QFile file(dir.filePath(QStringLiteral(".codex/auth.json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject tokens =
            QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("tokens")).toObject();
        QCOMPARE(tokens.value(QStringLiteral("refresh_token")).toString(), QStringLiteral("rotated-codex-refresh"));
        QCOMPARE(tokens.value(QStringLiteral("id_token")).toString(), QStringLiteral("new-id-token"));
    }

    void codexOauthRefreshFailure()
    {
        QTemporaryDir dir;
        QVERIFY(writeCodexAuth(dir.path(), jwtWithExpiry(QDateTime::currentDateTimeUtc().addSecs(-60))));

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            readHttpRequest(socket, 1000);
            const QByteArray payload = QByteArrayLiteral("{\"error\":{\"message\":\"invalid_grant\"}}");
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
            socket->flush();
        });

        qputenv("SPEECHER_TEST_CODEX_AUTH_PATH",
                QFile::encodeName(dir.filePath(QStringLiteral(".codex/auth.json"))));
        qputenv("SPEECHER_CODEX_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/oauth/token").arg(server.serverPort()).toUtf8());
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CODEX_AUTH_PATH");
            qunsetenv("SPEECHER_CODEX_TOKEN_URL");
        });

        OpenAiAuthProvider provider(nullptr, QStringLiteral("codex_oauth"));
        const OpenAiAuth auth = provider.resolve();
        QVERIFY(!auth.ok);
        QVERIFY2(auth.status.contains(QStringLiteral("invalid_grant")), qPrintable(auth.status));
    }

    void codexOauthAutoModeDoesNotRetryFailedChatGptRefresh()
    {
        QTemporaryDir dir;
        QVERIFY(writeCodexAuth(dir.path(), jwtWithExpiry(QDateTime::currentDateTimeUtc().addSecs(-60))));

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int refreshRequests = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            readHttpRequest(socket, 1000);
            ++refreshRequests;
            const QByteArray payload = QByteArrayLiteral("{\"error\":\"invalid_grant\"}");
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
            socket->flush();
        });

        const bool hadOpenAiKey = qEnvironmentVariableIsSet("OPENAI_API_KEY");
        const QByteArray oldOpenAiKey = qgetenv("OPENAI_API_KEY");
        qputenv("SPEECHER_TEST_CODEX_AUTH_PATH",
                QFile::encodeName(dir.filePath(QStringLiteral(".codex/auth.json"))));
        qputenv("SPEECHER_CODEX_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/oauth/token").arg(server.serverPort()).toUtf8());
        qunsetenv("OPENAI_API_KEY");
        const auto cleanup = qScopeGuard([hadOpenAiKey, oldOpenAiKey] {
            qunsetenv("SPEECHER_TEST_CODEX_AUTH_PATH");
            qunsetenv("SPEECHER_CODEX_TOKEN_URL");
            if (hadOpenAiKey) {
                qputenv("OPENAI_API_KEY", oldOpenAiKey);
            } else {
                qunsetenv("OPENAI_API_KEY");
            }
        });

        OpenAiAuthProvider provider(nullptr, QStringLiteral("auto"));
        const OpenAiAuth auth = provider.resolve();
        QVERIFY(!auth.ok);
        QCOMPARE(refreshRequests, 1);
    }

    void cliproxyExpiredAccountRefreshesAndRewritesFile()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray requestBody;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            const QByteArray request = readHttpRequest(socket, 1000);
            requestBody = request.mid(request.indexOf("\r\n\r\n") + 4);
            const QByteArray payload = QJsonDocument(QJsonObject{
                {QStringLiteral("access_token"), QStringLiteral("fresh-token")},
                {QStringLiteral("refresh_token"), QStringLiteral("rotated-refresh")},
                {QStringLiteral("expires_in"), 3600},
            }).toJson(QJsonDocument::Compact);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
            socket->flush();
        });
        qputenv("SPEECHER_CLIPROXY_CLAUDE_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/v1/oauth/token").arg(server.serverPort()).toUtf8());
        const auto restoreEnv = qScopeGuard([] { qunsetenv("SPEECHER_CLIPROXY_CLAUDE_TOKEN_URL"); });

        QTemporaryDir dir;
        const QDateTime expired = QDateTime::currentDateTimeUtc().addSecs(-60);
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"),
                                     QStringLiteral("claude"), QStringLiteral("stale-token"), expired));
        QVERIFY(CliProxyCredentials::accountNeedsRefresh(dir.path(), QStringLiteral("claude"), {}));

        const CliProxyCredentialResult result =
            CliProxyCredentials::loadWithRefresh(dir.path(), QStringLiteral("claude"), {});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.accessToken, QStringLiteral("fresh-token"));
        QVERIFY(requestBody.contains(QByteArrayLiteral("\"grant_type\":\"refresh_token\"")));

        // Rotated tokens must be written back: refresh tokens rotate, and a
        // stale file would strand CLI Proxy API's copy of the account.
        QFile file(dir.path() + QStringLiteral("/claude-a@example.com.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject updated = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(updated.value(QStringLiteral("access_token")).toString(), QStringLiteral("fresh-token"));
        QCOMPARE(updated.value(QStringLiteral("refresh_token")).toString(), QStringLiteral("rotated-refresh"));
        for (const QString &field : {QStringLiteral("expired"), QStringLiteral("last_refresh")}) {
            const QString timestamp = updated.value(field).toString();
            QVERIFY2(QDateTime::fromString(timestamp, Qt::ISODate).isValid(), qPrintable(timestamp));
            QVERIFY2(QRegularExpression(QStringLiteral("(Z|[+-]\\d{2}:\\d{2})$"))
                         .match(timestamp)
                         .hasMatch(),
                     qPrintable(timestamp));
        }
        QVERIFY(!CliProxyCredentials::accountNeedsRefresh(dir.path(), QStringLiteral("claude"), {}));
    }

    void cliproxyAccountListing()
    {
        QTemporaryDir dir;
        const QDateTime valid = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"), QStringLiteral("claude"),
                                     QStringLiteral("claude-token-a"), valid));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-b@example.com.json"), QStringLiteral("claude"),
                                     QStringLiteral("claude-token-b"), valid, true));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-c@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("codex-token-c"), valid));

        const QList<CliProxyAccount> accounts = CliProxyCredentials::listAccounts(dir.path(), QStringLiteral("claude"));
        QCOMPARE(accounts.size(), 2);
        QCOMPARE(accounts.first().fileName, QStringLiteral("claude-a@example.com.json"));
        QCOMPARE(accounts.first().label, QStringLiteral("a@example.com"));
        QVERIFY(!accounts.first().disabled);
        QVERIFY(accounts.last().disabled);
        const auto options = cliproxyAccountOptions("claude", {}, dir.path());
        QCOMPARE(options.size(), 3);
        QCOMPARE(options.first().label, QStringLiteral("Choose an account…"));
        QCOMPARE(options[1].label, QStringLiteral("a@example.com"));
        QVERIFY(!options.last().enabled);
        const auto missing = cliproxyAccountOptions("claude", "missing.json", dir.path());
        QCOMPARE(missing.last().label, QStringLiteral("missing.json (missing)"));
    }

    void cliproxyLoadResolvesAccounts()
    {
        QTemporaryDir dir;
        const QDateTime valid = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"), QStringLiteral("claude"),
                                     QStringLiteral("claude-token-a"), valid));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-b@example.com.json"), QStringLiteral("claude"),
                                     QStringLiteral("claude-token-b"), valid));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-c@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("codex-token-c"), valid));

        const CliProxyCredentialResult ambiguous = CliProxyCredentials::load(dir.path(), QStringLiteral("claude"), QString());
        QVERIFY(!ambiguous.ok);
        QVERIFY(ambiguous.error.contains(QStringLiteral("Multiple")));

        const CliProxyCredentialResult selected =
            CliProxyCredentials::load(dir.path(), QStringLiteral("claude"), QStringLiteral("claude-b@example.com.json"));
        QVERIFY2(selected.ok, qPrintable(selected.error));
        QCOMPARE(selected.accessToken, QStringLiteral("claude-token-b"));

        const CliProxyCredentialResult single = CliProxyCredentials::load(dir.path(), QStringLiteral("codex"), QString());
        QVERIFY2(single.ok, qPrintable(single.error));
        QCOMPARE(single.accessToken, QStringLiteral("codex-token-c"));
        QCOMPARE(single.accountId, QStringLiteral("acct"));
    }

    void cliproxyLoadRejectsExpiredAndDisabled()
    {
        QTemporaryDir dir;
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"), QStringLiteral("claude"),
                                     QStringLiteral("old-token"), QDateTime::currentDateTimeUtc().addSecs(-60)));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-c@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("codex-token-c"), QDateTime::currentDateTimeUtc().addSecs(3600), true));

        const CliProxyCredentialResult expired = CliProxyCredentials::load(dir.path(), QStringLiteral("claude"), QString());
        QVERIFY(!expired.ok);
        QVERIFY(expired.error.contains(QStringLiteral("expired")));
        QVERIFY(!expired.error.contains(QStringLiteral("old-token")));

        const CliProxyCredentialResult disabled = CliProxyCredentials::load(dir.path(), QStringLiteral("codex"), QString());
        QVERIFY(!disabled.ok);
        QVERIFY(disabled.error.contains(QStringLiteral("disabled")));

        const CliProxyCredentialResult wrongType =
            CliProxyCredentials::load(dir.path(), QStringLiteral("codex"), QStringLiteral("claude-a@example.com.json"));
        QVERIFY(!wrongType.ok);
        QVERIFY(wrongType.error.contains(QStringLiteral("not a codex account")));

        QFile badExpiry(QDir(dir.path()).filePath(QStringLiteral("claude-bad@example.com.json")));
        QVERIFY(badExpiry.open(QIODevice::WriteOnly));
        badExpiry.write(QJsonDocument(QJsonObject{
                                          {QStringLiteral("type"), QStringLiteral("claude")},
                                          {QStringLiteral("access_token"), QStringLiteral("token")},
                                          {QStringLiteral("expired"), QStringLiteral("not-a-date")},
                                      })
                            .toJson());
        badExpiry.close();
        const CliProxyCredentialResult unparsable =
            CliProxyCredentials::load(dir.path(), QStringLiteral("claude"), QStringLiteral("claude-bad@example.com.json"));
        QVERIFY(!unparsable.ok);
        QVERIFY(unparsable.error.contains(QStringLiteral("expiry")));
    }

    void openAiAuthProviderCliproxyMode()
    {
        QTemporaryDir dir;
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-c@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("codex-token-c"), QDateTime::currentDateTimeUtc().addSecs(3600)));

        OpenAiAuthProvider provider(nullptr, QStringLiteral("cliproxy"), QString(), dir.path());
        QVERIFY(!provider.requiresCodexOauthRefresh());
        const OpenAiAuth auth = provider.resolve();
        QVERIFY2(auth.ok, qPrintable(auth.status));
        QCOMPARE(auth.bearerToken, QStringLiteral("codex-token-c"));
        QVERIFY(auth.chatgptBackend);
        QCOMPARE(auth.endpointBase, QStringLiteral("https://chatgpt.com/backend-api/codex"));
        QCOMPARE(auth.accountId, QStringLiteral("acct"));
    }

    void anthropicRefinerCliproxyPrepare()
    {
        QTemporaryDir dir;
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"), QStringLiteral("claude"),
                                     QStringLiteral("claude-token-a"), QDateTime::currentDateTimeUtc().addSecs(3600)));

        AnthropicTranscriptRefiner refiner;
        RefinementSettings settings;
        settings.anthropicAuthMode = QStringLiteral("cliproxy");
        settings.cliproxyOauthDir = dir.path();
        QVERIFY(!refiner.requiresRefresh(settings));
        const RefinementPrepareResult prepared = refiner.prepare(settings);
        QVERIFY2(prepared.ok, qPrintable(prepared.message));
    }
};

int runProviderAuthTests(int argc, char **argv)
{
    ProviderAuthTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_provider_auth.moc"
