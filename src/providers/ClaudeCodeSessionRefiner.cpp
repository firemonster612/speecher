#include "providers/ClaudeCodeSessionRefiner.h"

#include "core/CliToolDiscovery.h"
#include "providers/TranscriptRefinementPrompt.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace speecher {
namespace {

QString processErrorMessage(const QProcess &process)
{
    const QString error = process.errorString();
    return error.isEmpty() ? QStringLiteral("Claude Code session failed") : error;
}

} // namespace

ClaudeCodeSessionRefiner::ClaudeCodeSessionRefiner(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &ClaudeCodeSessionRefiner::handleStdout);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_turnState == TurnState::Refining && !m_failedCurrentTurn) {
            m_failedCurrentTurn = true;
            emit failed(QStringLiteral("Claude Code refinement failed: %1").arg(processErrorMessage(m_process)));
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const bool unexpected = m_turnState == TurnState::Refining
            && (exitStatus != QProcess::NormalExit || exitCode != 0);
        m_turnState = TurnState::Idle;
        m_model.clear();
        m_effort.clear();
        m_refinementStyle.clear();
        if (unexpected && !m_failedCurrentTurn) {
            m_failedCurrentTurn = true;
            emit failed(QStringLiteral("Claude Code refinement exited unexpectedly"));
        }
    });
}

ClaudeCodeSessionRefiner::~ClaudeCodeSessionRefiner()
{
    stopProcess();
}

bool ClaudeCodeSessionRefiner::prepare(const QString &model, const QString &effort, const QString &refinementStyle, QString *error)
{
    const QString executable = claudeExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Could not find Claude Code; install it and ensure `claude` is on PATH");
        }
        return false;
    }

    if (m_process.state() != QProcess::NotRunning
        && (m_model != model || m_effort != effort || m_refinementStyle != refinementStyle)) {
        stopProcess();
    }

    if (m_process.state() == QProcess::NotRunning) {
        m_model = model;
        m_effort = effort;
        m_refinementStyle = refinementStyle;
        startProcess(model, effort, refinementStyle);
        if (!m_process.waitForStarted(3000)) {
            if (error) {
                *error = QStringLiteral("Could not start Claude Code refinement session: %1").arg(processErrorMessage(m_process));
            }
            m_model.clear();
            m_effort.clear();
            m_refinementStyle.clear();
            return false;
        }
    }

    if (m_turnState == TurnState::Clearing) {
        if (error) {
            *error = QStringLiteral("Claude Code refinement session is clearing the previous turn");
        }
        return false;
    }

    return true;
}

void ClaudeCodeSessionRefiner::refine(const QString &rawTranscript,
                                      const QStringList &vocabulary,
                                      const QStringList &bindingVocabulary)
{
    if (m_process.state() == QProcess::NotRunning) {
        QString error;
        if (!prepare(m_model.isEmpty() ? QStringLiteral("claude-sonnet-4-6") : m_model,
                     m_effort.isEmpty() ? QStringLiteral("high") : m_effort,
                     m_refinementStyle.isEmpty() ? QStringLiteral("balanced") : m_refinementStyle,
                     &error)) {
            emit failed(error);
            return;
        }
    }
    if (m_turnState != TurnState::Idle) {
        emit failed(QStringLiteral("Claude Code refinement session is busy"));
        return;
    }

    m_accumulated.clear();
    m_failedCurrentTurn = false;
    m_turnState = TurnState::Refining;
    sendUserMessage(transcriptRefinementUserMessage(rawTranscript, vocabulary, bindingVocabulary));
}

void ClaudeCodeSessionRefiner::cancel()
{
    if (m_turnState == TurnState::Refining) {
        m_turnState = TurnState::Idle;
        stopProcess();
    }
    m_accumulated.clear();
    m_turnState = TurnState::Idle;
}

QString ClaudeCodeSessionRefiner::claudeExecutable()
{
    return CliToolDiscovery::claudeCodeExecutable();
}

void ClaudeCodeSessionRefiner::startProcess(const QString &model, const QString &effort, const QString &refinementStyle)
{
    m_stdoutBuffer.clear();
    m_accumulated.clear();
    m_failedCurrentTurn = false;
    m_turnState = TurnState::Idle;
    m_process.setProgram(claudeExecutable());
    m_process.setArguments({
        QStringLiteral("--input-format"),
        QStringLiteral("stream-json"),
        QStringLiteral("--output-format"),
        QStringLiteral("stream-json"),
        QStringLiteral("--verbose"),
        QStringLiteral("--include-partial-messages"),
        QStringLiteral("--model"),
        model,
        QStringLiteral("--effort"),
        effort.isEmpty() ? QStringLiteral("high") : effort,
        QStringLiteral("--tools"),
        QString(),
        QStringLiteral("--permission-mode"),
        QStringLiteral("dontAsk"),
        QStringLiteral("--no-chrome"),
        QStringLiteral("--safe-mode"),
        QStringLiteral("--system-prompt"),
        transcriptRefinementInstructions(refinementStyle),
    });
    m_process.start();
}

void ClaudeCodeSessionRefiner::stopProcess()
{
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }
    m_process.closeWriteChannel();
    m_process.terminate();
    if (!m_process.waitForFinished(1500)) {
        m_process.kill();
        m_process.waitForFinished(1500);
    }
}

void ClaudeCodeSessionRefiner::sendUserMessage(const QString &content)
{
    const QJsonObject object{
        {QStringLiteral("type"), QStringLiteral("user")},
        {QStringLiteral("message"), QJsonObject{
             {QStringLiteral("role"), QStringLiteral("user")},
             {QStringLiteral("content"), content},
         }},
    };
    m_process.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    m_process.write("\n");
}

void ClaudeCodeSessionRefiner::sendClear()
{
    if (m_process.state() == QProcess::NotRunning) {
        m_turnState = TurnState::Idle;
        return;
    }
    m_turnState = TurnState::Clearing;
    sendUserMessage(QStringLiteral("/clear"));
}

void ClaudeCodeSessionRefiner::handleStdout()
{
    m_stdoutBuffer += m_process.readAllStandardOutput();
    while (true) {
        const int newline = m_stdoutBuffer.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) {
            handleJsonLine(line);
        }
    }
}

void ClaudeCodeSessionRefiner::handleJsonLine(const QByteArray &line)
{
    const QJsonObject object = QJsonDocument::fromJson(line).object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("stream_event") && m_turnState == TurnState::Refining) {
        const QJsonObject event = object.value(QStringLiteral("event")).toObject();
        if (event.value(QStringLiteral("type")).toString() == QStringLiteral("content_block_delta")) {
            const QJsonObject deltaObject = event.value(QStringLiteral("delta")).toObject();
            if (deltaObject.value(QStringLiteral("type")).toString() == QStringLiteral("text_delta")) {
                const QString text = deltaObject.value(QStringLiteral("text")).toString();
                m_accumulated += text;
                emit delta(text);
            }
        }
    } else if (type == QStringLiteral("result")) {
        handleResult(object);
    }
}

void ClaudeCodeSessionRefiner::handleResult(const QJsonObject &object)
{
    if (m_turnState == TurnState::Clearing) {
        m_turnState = TurnState::Idle;
        return;
    }
    if (m_turnState != TurnState::Refining) {
        return;
    }

    const bool isError = object.value(QStringLiteral("is_error")).toBool(false);
    const QString result = object.value(QStringLiteral("result")).toString();
    m_turnState = TurnState::Idle;
    if (isError) {
        m_failedCurrentTurn = true;
        emit failed(result.isEmpty() ? QStringLiteral("Claude Code refinement failed") : result);
        sendClear();
        return;
    }
    const QString finalText = m_accumulated.isEmpty() ? result : m_accumulated;
    emit completed(finalText);
    sendClear();
}

} // namespace speecher
