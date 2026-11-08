#pragma once

// Audio written to a file as it is heard, a dictation's microphone in
// History's audio folder or a Recording's two sides beside its transcript:
// AAC in M4A (about 0.25 MB a minute a channel) where Qt's media backend
// encodes it, else 16 kHz WAV.

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
    // A dictation's: a mono file in folder, which is made the owner's alone,
    // under a name of its own ending in .part until it is finished.
    explicit HistoryAudioWriter(const QString &folder, QObject *parent = nullptr);
    // stem.m4a, or stem.wav, in folder, as stem.<ext>.part until finished,
    // with channels interleaved in each write.
    HistoryAudioWriter(const QString &folder, const QString &stem, int channels, QObject *parent = nullptr);
    ~HistoryAudioWriter() override;

    // 16 kHz 16-bit PCM, its channels interleaved; mono as the microphone
    // delivers it.
    void write(const QByteArray &pcm);
    // Ends the file and returns its name, relative to the folder; empty when
    // nothing was heard or it couldn't be written. An encoder may still be
    // writing: the file takes the name once it has, and the writer deletes
    // itself then.
    QString finish();
    // Throws the audio away, and the writer with it.
    void discard();

    // The format write() takes.
    static QAudioFormat pcmFormat(int channels = 1);

private:
    QString partPath() const;
    void sendPending();
    void finishEncoding(bool ok);

    QString m_folder;
    QString m_name;
    int m_channels = 1;
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
