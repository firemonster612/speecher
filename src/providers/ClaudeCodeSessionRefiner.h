#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

namespace speecher {

class ClaudeCodeSessionRefiner final : public QObject {
    Q_OBJECT

public:
    explicit ClaudeCodeSessionRefiner(QObject *parent = nullptr);
    ~ClaudeCodeSessionRefiner() override;

    bool prepare(const QString &model, const QString &effort, const QString &refinementStyle, QString *error = nullptr);
    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const QStringList &bindingVocabulary);
    void cancel();

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const QString &message);

private:
    enum class TurnState {
        Idle,
        Refining,
        Clearing,
    };

    static QString claudeExecutable();

    void startProcess(const QString &model, const QString &effort, const QString &refinementStyle);
    void stopProcess();
    void sendUserMessage(const QString &content);
    void sendClear();
    void handleStdout();
    void handleJsonLine(const QByteArray &line);
    void handleResult(const QJsonObject &object);

    QProcess m_process;
    QByteArray m_stdoutBuffer;
    QString m_model;
    QString m_effort;
    QString m_refinementStyle;
    QString m_accumulated;
    TurnState m_turnState = TurnState::Idle;
    bool m_failedCurrentTurn = false;
};

} // namespace speecher
