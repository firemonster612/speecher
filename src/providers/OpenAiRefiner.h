#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QStringList>

namespace speecher {

QString openAiRefinementInstructions(const QString &style, const QString &format);

class OpenAiRefiner : public QObject {
    Q_OBJECT

public:
    explicit OpenAiRefiner(QObject *parent = nullptr);

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const QString &bearerToken,
                const QString &organization,
                const QString &project,
                const QString &endpointBase,
                const QString &accountId,
                bool chatgptBackend,
                const QString &model,
                const QString &refinementStyle,
                const QString &outputFormat);
    void cancel();

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const QString &message);

private:
    void parseSseChunk(const QByteArray &chunk);

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    QString m_accumulated;
    bool m_failed = false;
};

} // namespace speecher
