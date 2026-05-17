#pragma once

#include <QAudioSource>
#include <QIODevice>
#include <QObject>
#include <QScopedPointer>

namespace speecher {

class AudioCapture : public QObject {
    Q_OBJECT

public:
    explicit AudioCapture(QObject *parent = nullptr);
    bool start(QString *error = nullptr);
    void stop();
    bool isActive() const;

signals:
    void audioChunk(const QByteArray &pcm);
    void levelChanged(float level);
    void failed(const QString &message);

private:
    void onReadyRead();

    QScopedPointer<QAudioSource> m_source;
    QIODevice *m_device = nullptr;
};

} // namespace speecher
