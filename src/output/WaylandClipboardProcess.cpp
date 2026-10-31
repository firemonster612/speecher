#include "output/WaylandClipboardProcess.h"

#include "output/HelperPath.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <utility>

namespace speecher::WaylandClipboardProcess {

namespace {

// One command from its start to its answer. It deletes itself once its process
// has exited, and never waits for that.
class RunningCommand final : public QObject {
public:
    // The application's, so no command in flight outlives it.
    RunningCommand(Command command, QObject *context, std::function<void(const CommandResult &)> done)
        : QObject(QCoreApplication::instance())
        , m_command(std::move(command))
        , m_context(context)
        , m_done(std::move(done))
    {
        if (!m_command.collectsOutput) {
            m_process.setStandardOutputFile(QProcess::nullDevice());
        }
        connect(&m_process, &QProcess::started, this, [this] {
            if (m_command.input) {
                m_process.write(*m_command.input);
            }
            m_process.closeWriteChannel();
        });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                answer({false, {}, QStringLiteral("Could not start %1").arg(m_command.tool)});
            }
        });
        connect(&m_process, &QProcess::readyReadStandardOutput, this, &RunningCommand::readOutput);
        connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
            readOutput();
            if (status == QProcess::NormalExit && exitCode == 0) {
                answer({true, m_output, {}});
            } else {
                const QString stderrText = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
                answer({false, {}, stderrText.isEmpty()
                                       ? QStringLiteral("%1 failed").arg(m_command.tool)
                                       : QStringLiteral("%1 failed: %2").arg(m_command.tool, stderrText)});
            }
            deleteLater();
        });
        QTimer::singleShot(m_command.timeoutMs, this, [this] {
            answer({false, {}, QStringLiteral("%1 timed out").arg(m_command.tool)});
        });
        m_process.start(m_command.executable, m_command.arguments);
    }

    // The application ends one still running: its process dies with it and
    // says finished, which must not reach the members gone before it.
    ~RunningCommand() override { m_process.disconnect(this); }

private:
    void readOutput()
    {
        if (!m_done) {
            return;
        }
        m_output += m_process.readAllStandardOutput();
        if (m_output.size() > m_command.maximumOutputBytes) {
            answer({false, {}, QStringLiteral("%1 returned too much clipboard data").arg(m_command.tool)});
        }
    }

    // The first answer counts. A process still running is killed, and this
    // goes once it has exited.
    void answer(const CommandResult &result)
    {
        if (!m_done) {
            return;
        }
        const auto done = std::exchange(m_done, {});
        if (m_process.state() == QProcess::NotRunning) {
            deleteLater();
        } else {
            m_process.kill();
        }
        if (m_context) {
            done(result);
        }
    }

    QProcess m_process;
    Command m_command;
    QPointer<QObject> m_context;
    std::function<void(const CommandResult &)> m_done;
    QByteArray m_output;
};

} // namespace

QString wlCopyExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
}

QString wlPasteExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-paste"));
}

QString helperExecutable()
{
#ifdef SPEECHER_WAYLAND_CLIPBOARD_HELPER_PATH
    const QString installed = resolvedHelperPath(SPEECHER_WAYLAND_CLIPBOARD_HELPER_PATH);
    if (QFileInfo::exists(installed) && QFileInfo(installed).isExecutable()) {
        return installed;
    }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("speecher-wayland-clipboard"));
}

void run(const Command &command, QObject *context, std::function<void(const CommandResult &)> done)
{
    new RunningCommand(command, context, std::move(done));
}

bool looksLikeEmptyClipboardError(const QString &message)
{
    const QString lower = message.toLower();
    return lower.contains(QStringLiteral("nothing is copied"))
        || lower.contains(QStringLiteral("clipboard is empty"))
        || lower.contains(QStringLiteral("no selection"))
        || lower.contains(QStringLiteral("no data"));
}

} // namespace speecher::WaylandClipboardProcess
