#include "core/CliToolDiscovery.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <optional>

namespace speecher {
namespace {

QString executableFromCandidates(const QString &name,
                                 const QString &overrideVariable,
                                 const QStringList &fixedCandidates,
                                 const QString &versionDirectory = QString())
{
    const QString overridePath = qEnvironmentVariable(overrideVariable.toUtf8().constData());
    if (!overridePath.isEmpty()) {
        return overridePath;
    }

    const QString fromPath = QStandardPaths::findExecutable(name);
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    for (const QString &candidate : fixedCandidates) {
        const QFileInfo file(candidate);
        if (file.isFile() && file.isExecutable()) {
            return candidate;
        }
    }

    if (!versionDirectory.isEmpty()) {
        QDir versions(versionDirectory);
        const QFileInfoList entries = versions.entryInfoList(QDir::Files | QDir::Executable | QDir::NoDotAndDotDot,
                                                             QDir::Time);
        if (!entries.isEmpty()) {
            return entries.first().absoluteFilePath();
        }
    }

    return {};
}

std::optional<bool> forcedAvailability(const char *name)
{
    if (!qEnvironmentVariableIsSet(name)) {
        return std::nullopt;
    }

    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    return value == QStringLiteral("1") || value == QStringLiteral("true") || value == QStringLiteral("yes");
}

} // namespace

QString CliToolDiscovery::codexExecutable()
{
    return executableFromCandidates(QStringLiteral("codex"),
                                    QStringLiteral("SPEECHER_TEST_CODEX_EXECUTABLE"),
                                    {
                                        QDir::homePath() + QStringLiteral("/.local/bin/codex"),
                                        QStringLiteral("/usr/local/bin/codex"),
                                        QStringLiteral("/usr/bin/codex"),
                                    });
}

QString CliToolDiscovery::claudeCodeExecutable()
{
    return executableFromCandidates(QStringLiteral("claude"),
                                    QStringLiteral("SPEECHER_TEST_CLAUDE_EXECUTABLE"),
                                    {
                                        QDir::homePath() + QStringLiteral("/.local/bin/claude"),
                                        QStringLiteral("/usr/local/bin/claude"),
                                        QStringLiteral("/usr/bin/claude"),
                                    },
                                    QDir::homePath() + QStringLiteral("/.local/share/claude/versions"));
}

bool CliToolDiscovery::isCodexInstalled()
{
    if (const std::optional<bool> forced = forcedAvailability("SPEECHER_TEST_CODEX_INSTALLED")) {
        return *forced;
    }
    return !codexExecutable().isEmpty();
}

bool CliToolDiscovery::isClaudeCodeInstalled()
{
    if (const std::optional<bool> forced = forcedAvailability("SPEECHER_TEST_CLAUDE_INSTALLED")) {
        return *forced;
    }
    return !claudeCodeExecutable().isEmpty();
}

} // namespace speecher
