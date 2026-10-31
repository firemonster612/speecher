#include "output/ProcessRunner.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace speecher {

namespace {

// One command from its start to its answer. It deletes itself once its process
// has exited, and never waits for that.
class RunningCommand final : public QObject {
public:
    // The application's, so no command in flight outlives it.
    RunningCommand(ProcessCommand command, QObject *context, std::function<void(const ProcessResult &)> done)
        : QObject(QCoreApplication::instance())
        , m_command(std::move(command))
        , m_context(context)
        , m_done(std::move(done))
    {
        if (!m_command.collectsOutput) {
            m_process.setStandardOutputFile(QProcess::nullDevice());
        }
        if (m_command.environment) {
            m_process.setProcessEnvironment(*m_command.environment);
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
        m_running.start();
        m_process.start(m_command.executable, m_command.arguments);
    }

    // Only the application ending destroys one still running. Its process
    // dies with it and says finished, which must not reach the members gone
    // before it; one that finishes at shutdown gets the rest of its time.
    ~RunningCommand() override
    {
        m_process.disconnect(this);
        if (m_command.finishesAtShutdown && m_process.state() != QProcess::NotRunning) {
            m_process.waitForFinished(std::max(0, m_command.timeoutMs - int(m_running.elapsed())));
        }
    }

    void stop() { answer({false, {}, QStringLiteral("%1 was stopped").arg(m_command.tool)}); }

private:
    void readOutput()
    {
        if (!m_done) {
            return;
        }
        m_output += m_process.readAllStandardOutput();
        if (m_output.size() > m_command.maximumOutputBytes) {
            answer({false, {}, QStringLiteral("%1 returned too much data").arg(m_command.tool)});
        }
    }

    // The first answer counts. A process still running is killed, and this
    // goes once it has exited.
    void answer(const ProcessResult &result)
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
    ProcessCommand m_command;
    QPointer<QObject> m_context;
    std::function<void(const ProcessResult &)> m_done;
    QByteArray m_output;
    QElapsedTimer m_running;
};

} // namespace

std::function<void()> runProcess(const ProcessCommand &command,
                                 QObject *context,
                                 std::function<void(const ProcessResult &)> done)
{
    const QPointer<RunningCommand> running = new RunningCommand(command, context, std::move(done));
    return [running] {
        if (running) {
            running->stop();
        }
    };
}

} // namespace speecher
