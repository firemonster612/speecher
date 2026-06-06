#pragma once

#include <QAudioSource>
#include <QIODevice>
#include <QScopedPointer>

#include "dictation/DictationInterfaces.h"

namespace speecher {

class AudioCapture : public AudioInput {
    Q_OBJECT

public:
    explicit AudioCapture(QObject *parent = nullptr);
    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;

private:
    void onReadyRead();

    QScopedPointer<QAudioSource> m_source;
    QIODevice *m_device = nullptr;
};

} // namespace speecher
