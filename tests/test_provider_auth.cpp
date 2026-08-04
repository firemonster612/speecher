#include "common/test_doubles.h"
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
        QVERIFY(result.error.contains(QStringLiteral("re-authenticate"), Qt::CaseInsensitive));
        QVERIFY(!result.error.contains(QStringLiteral("secret-refresh-token")));
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

    void codexOauthRefreshClosesChildStdin()
    {
        QTemporaryDir dir;
        QVERIFY(writeCodexAuth(dir.path(), jwtWithExpiry(QDateTime::currentDateTimeUtc().addSecs(-60))));
        const QString stdinCapture = dir.filePath(QStringLiteral("codex-stdin.txt"));
        const QString fakeCodex = writeFakeClaudeScript(dir.filePath(QStringLiteral("codex-fake")), QStringLiteral(R"(
test "$1" = "exec" || exit 10
cat > "$SPEECHER_TEST_CODEX_STDIN_CAPTURE"
cat > "$HOME/.codex/auth.json" <<'JSON'
{"auth_mode":"chatgpt","tokens":{"access_token":"REFRESHED_AFTER_STDIN_EOF","account_id":"acct"}}
JSON
exit 0
)"));
        QVERIFY(!fakeCodex.isEmpty());

        const QByteArray oldHome = qgetenv("HOME");
        qputenv("HOME", QFile::encodeName(dir.path()));
        qputenv("SPEECHER_TEST_CODEX_EXECUTABLE", QFile::encodeName(fakeCodex));
        qputenv("SPEECHER_TEST_CODEX_STDIN_CAPTURE", QFile::encodeName(stdinCapture));
        qputenv("SPEECHER_CODEX_REFRESH_TIMEOUT_MS", "500");
        const auto cleanup = qScopeGuard([oldHome] {
            if (oldHome.isEmpty()) {
                qunsetenv("HOME");
            } else {
                qputenv("HOME", oldHome);
            }
            qunsetenv("SPEECHER_TEST_CODEX_EXECUTABLE");
            qunsetenv("SPEECHER_TEST_CODEX_STDIN_CAPTURE");
            qunsetenv("SPEECHER_CODEX_REFRESH_TIMEOUT_MS");
        });

        QElapsedTimer timer;
        timer.start();
        OpenAiAuthProvider provider(nullptr, QStringLiteral("codex_oauth"));
        const OpenAiAuth auth = provider.resolve();
        QVERIFY2(auth.ok, qPrintable(auth.status));
        QVERIFY(timer.elapsed() < 1500);
        QCOMPARE(auth.bearerToken, QStringLiteral("REFRESHED_AFTER_STDIN_EOF"));

        QFile file(stdinCapture);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray());
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

    void codexOauthAutoModeDoesNotRetryFailedChatGptRefresh()
    {
        QTemporaryDir dir;
        QVERIFY(writeCodexAuth(dir.path(), jwtWithExpiry(QDateTime::currentDateTimeUtc().addSecs(-60))));
        const QString countPath = dir.filePath(QStringLiteral("codex-count"));
        const QString fakeCodex = writeFakeClaudeScript(dir.filePath(QStringLiteral("codex-fake")), QStringLiteral(R"SH(
count=0
if test -f "$SPEECHER_TEST_CODEX_COUNT"; then
  count="$(cat "$SPEECHER_TEST_CODEX_COUNT")"
fi
count=$((count + 1))
printf '%s\n' "$count" > "$SPEECHER_TEST_CODEX_COUNT"
echo failed >&2
exit 12
)SH"));
        QVERIFY(!fakeCodex.isEmpty());

        const QByteArray oldHome = qgetenv("HOME");
        const bool hadOpenAiKey = qEnvironmentVariableIsSet("OPENAI_API_KEY");
        const QByteArray oldOpenAiKey = qgetenv("OPENAI_API_KEY");
        qputenv("HOME", QFile::encodeName(dir.path()));
        qputenv("SPEECHER_TEST_CODEX_EXECUTABLE", QFile::encodeName(fakeCodex));
        qputenv("SPEECHER_TEST_CODEX_COUNT", QFile::encodeName(countPath));
        qunsetenv("OPENAI_API_KEY");
        const auto cleanup = qScopeGuard([oldHome, hadOpenAiKey, oldOpenAiKey] {
            if (oldHome.isEmpty()) {
                qunsetenv("HOME");
            } else {
                qputenv("HOME", oldHome);
            }
            qunsetenv("SPEECHER_TEST_CODEX_EXECUTABLE");
            qunsetenv("SPEECHER_TEST_CODEX_COUNT");
            if (hadOpenAiKey) {
                qputenv("OPENAI_API_KEY", oldOpenAiKey);
            } else {
                qunsetenv("OPENAI_API_KEY");
            }
        });

        OpenAiAuthProvider provider(nullptr, QStringLiteral("auto"));
        const OpenAiAuth auth = provider.resolve();
        QVERIFY(!auth.ok);

        QFile file(countPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()).trimmed(), QStringLiteral("1"));
    }
};

int runProviderAuthTests(int argc, char **argv)
{
    ProviderAuthTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_provider_auth.moc"
