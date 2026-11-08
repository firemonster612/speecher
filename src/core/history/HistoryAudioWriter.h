#pragma once

// One dictation's microphone audio, written to a file in History's audio
// folder as it is heard: AAC in M4A (about 0.25 MB a minute) where Qt's media
// backend encodes it, else 16 kHz WAV.

#include <QAudioFormat>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

class QAudioBufferInput;
class QMediaCaptureSession;
class QMediaRecorder;
class QFile;

namespace speecher {

class HistoryAudioWriter : public QObject {
    Q_OBJECT

public:
    // Starts a file in folder, under a name of its own ending in .part until
    // it is finished.
    explicit HistoryAudioWriter(const QString &folder, QObject *parent = nullptr);
    ~HistoryAudioWriter() override;

    // 16 kHz mono 16-bit PCM, as the microphone delivers it.
    void write(const QByteArray &pcm);
    // Ends the file and returns its name, relative to the folder; empty when
    // nothing was heard or it couldn't be written. An encoder may still be
    // writing: the file takes the name once it has, and the writer deletes
    // itself then.
    QString finish();
    // Throws the audio away, and the writer with it.
    void discard();

    // The format write() takes.
    static QAudioFormat pcmFormat();

private:
    QString partPath() const;
    void sendPending();
    void finishEncoding(bool ok);

    QString m_folder;
    QString m_name;
    bool m_encoded = false;
    bool m_finishing = false;
    bool m_discarded = false;
    bool m_endSent = false;
    qint64 m_samples = 0;
    qint64 m_samplesSent = 0;
    QFile *m_wav = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QAudioBufferInput *m_input = nullptr;
    QMediaRecorder *m_recorder = nullptr;
    QList<QByteArray> m_pending;
};

} // namespace speecher
