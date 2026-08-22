#include "common/test_doubles.h"
#include <QScopeGuard>
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;


class ProviderAuthTests : public QObject {
    Q_OBJECT

private slots:
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

    void claudeCredentialsOauthRefresh()
    {
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
        const QJsonObject saved = QJsonDocument::fromJson(credentialsFile.readAll()).object();
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

    void claudeInstalledVersion()
    {
        const QString version = ClaudeCredentials::installedVersion();
        if (!version.isEmpty()) {
            QVERIFY(QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+")).match(version).hasMatch());
        }
    }

    void claudeInstalledVersionIsCachedUntilExecutableChanges()
    {
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
                {QStringLiteral("expires_in"), 3600},
            }).toJson(QJsonDocument::Compact);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
            socket->flush();
        });

        const QByteArray oldHome = qgetenv("HOME");
        qputenv("HOME", QFile::encodeName(dir.path()));
        qputenv("SPEECHER_CODEX_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/oauth/token").arg(server.serverPort()).toUtf8());
        const auto cleanup = qScopeGuard([oldHome] {
            if (oldHome.isEmpty()) {
                qunsetenv("HOME");
            } else {
                qputenv("HOME", oldHome);
            }
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
            const QByteArray payload = QByteArrayLiteral("{\"error\":\"invalid_grant\"}");
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
            socket->flush();
        });

        const QByteArray oldHome = qgetenv("HOME");
        qputenv("HOME", QFile::encodeName(dir.path()));
        qputenv("SPEECHER_CODEX_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/oauth/token").arg(server.serverPort()).toUtf8());
        const auto cleanup = qScopeGuard([oldHome] {
            if (oldHome.isEmpty()) {
                qunsetenv("HOME");
            } else {
                qputenv("HOME", oldHome);
            }
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

        const QByteArray oldHome = qgetenv("HOME");
        const bool hadOpenAiKey = qEnvironmentVariableIsSet("OPENAI_API_KEY");
        const QByteArray oldOpenAiKey = qgetenv("OPENAI_API_KEY");
        qputenv("HOME", QFile::encodeName(dir.path()));
        qputenv("SPEECHER_CODEX_TOKEN_URL",
                QStringLiteral("http://127.0.0.1:%1/oauth/token").arg(server.serverPort()).toUtf8());
        qunsetenv("OPENAI_API_KEY");
        const auto cleanup = qScopeGuard([oldHome, hadOpenAiKey, oldOpenAiKey] {
            if (oldHome.isEmpty()) {
                qunsetenv("HOME");
            } else {
                qputenv("HOME", oldHome);
            }
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
