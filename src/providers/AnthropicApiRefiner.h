#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

class QNetworkReply;

namespace speecher {

class AnthropicApiRefiner final : public QObject {
    Q_OBJECT

public:
    explicit AnthropicApiRefiner(QObject *parent = nullptr);

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const QStringList &bindingVocabulary,
                const QString &bearerToken,
                const QString &endpointBase,
                const QString &model,
                const QString &effort,
                const QString &refinementStyle);
    void cancel();

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const QString &message);

private:
    void parseSseChunk(const QByteArray &chunk);
    void completeIfReady();

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    QString m_accumulated;
    bool m_failed = false;
    bool m_completed = false;
};

} // namespace speecher
