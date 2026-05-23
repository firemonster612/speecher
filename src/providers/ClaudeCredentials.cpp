#include "providers/ClaudeCredentials.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace speecher {

namespace {

ClaudeCredentialResult readCredentials(const QString &path)
{
    ClaudeCredentialResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Claude credentials not found at %1; run `claude /login` or `claude auth login`").arg(path);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = QStringLiteral("Claude credentials file is not valid JSON");
        return result;
    }

    const QJsonObject oauth = doc.object().value(QStringLiteral("claudeAiOauth")).toObject();
    result.accessToken = oauth.value(QStringLiteral("accessToken")).toString();
    result.refreshToken = oauth.value(QStringLiteral("refreshToken")).toString();
    result.subscriptionType = oauth.value(QStringLiteral("subscriptionType")).toString();
    result.rateLimitTier = oauth.value(QStringLiteral("rateLimitTier")).toString();
    const qint64 expires = static_cast<qint64>(oauth.value(QStringLiteral("expiresAt")).toDouble());
    result.expiresAt = QDateTime::fromSecsSinceEpoch(expires / (expires > 9999999999LL ? 1000 : 1), Qt::UTC);
    for (const QJsonValue &scope : oauth.value(QStringLiteral("scopes")).toArray()) {
        result.scopes << scope.toString();
    }

    if (result.accessToken.isEmpty()) {
        result.error = QStringLiteral("Claude credentials do not contain claudeAiOauth.accessToken");
        return result;
    }
    if (result.expiresAt.isValid() && result.expiresAt <= QDateTime::currentDateTimeUtc()) {
        result.error = QStringLiteral("Claude login expired; run `claude /login` or `claude auth login`");
        return result;
    }

    result.ok = true;
    return result;
}

QString findClaudeExecutable()
{
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    const QStringList fixedCandidates{
        QDir::homePath() + QStringLiteral("/.local/bin/claude"),
        QStringLiteral("/usr/local/bin/claude"),
        QStringLiteral("/usr/bin/claude"),
    };
    for (const QString &candidate : fixedCandidates) {
        const QFileInfo file(candidate);
        if (file.isFile() && file.isExecutable()) {
            return candidate;
        }
    }

    QDir versions(QDir::homePath() + QStringLiteral("/.local/share/claude/versions"));
    const QFileInfoList entries = versions.entryInfoList(QDir::Files | QDir::Executable | QDir::NoDotAndDotDot,
                                                         QDir::Time);
    if (!entries.isEmpty()) {
        return entries.first().absoluteFilePath();
    }

    return {};
}

bool refreshClaudeAuth(QString *error)
{
    const QString executable = findClaudeExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Could not find Claude Code; install it and ensure `claude` is on PATH");
        }
        return false;
    }

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("auth"), QStringLiteral("status")});
    process.start();
    if (!process.waitForStarted(2000)) {
        if (error) {
            *error = QStringLiteral("Could not start `claude auth status` to refresh login");
        }
        return false;
    }
    if (!process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = QStringLiteral("Timed out refreshing Claude login with `claude auth status`");
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error) {
            *error = stderrText.isEmpty()
                ? QStringLiteral("Claude login refresh failed; run `claude auth login`")
                : QStringLiteral("Claude login refresh failed: %1").arg(stderrText.left(240));
        }
        return false;
    }
    return true;
}

} // namespace

QString ClaudeCredentials::installedVersion()
{
    const QString executable = findClaudeExecutable();
    if (executable.isEmpty()) {
        return {};
    }

    const QFileInfo executableInfo(executable);
    static const QRegularExpression versionPattern(QStringLiteral("\\b\\d+\\.\\d+\\.\\d+(?:[-+][A-Za-z0-9._-]+)?\\b"));
    const QRegularExpressionMatch pathMatch = versionPattern.match(executableInfo.fileName());
    if (pathMatch.hasMatch()) {
        return pathMatch.captured(0);
    }

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("--version")});
    process.start();
    if (!process.waitForStarted(1000)) {
        return {};
    }
    if (!process.waitForFinished(2000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
    const QRegularExpressionMatch outputMatch = versionPattern.match(output);
    return outputMatch.hasMatch() ? outputMatch.captured(0) : QString();
}

ClaudeCredentialResult ClaudeCredentials::load(const QString &path, bool refreshExpired)
{
    ClaudeCredentialResult result = readCredentials(path);
    if (result.ok || !refreshExpired || !result.expiresAt.isValid()
        || result.expiresAt > QDateTime::currentDateTimeUtc()) {
        return result;
    }

    QString refreshError;
    if (!refreshClaudeAuth(&refreshError)) {
        result.error = refreshError;
        return result;
    }

    ClaudeCredentialResult refreshed = readCredentials(path);
    if (!refreshed.ok) {
        refreshed.error = QStringLiteral("Claude login refresh did not produce valid credentials; %1").arg(refreshed.error);
    }
    return refreshed;
}

} // namespace speecher
