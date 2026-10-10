#pragma once

#include "platform/win/WinCaptureInput.h"

#include <QList>

#include <windows.h>
#include <audioclient.h>

namespace speecher {

// Why a communications capture records without Windows' echo cancellation,
// or nothing when it has it, from how setting the stream's communications
// category went, how asking for the stream's effects went, and the effects
// it has. A Windows without the effects manager fails that ask.
QString communicationsEchoCancellationWarning(HRESULT category,
                                              HRESULT effectsQuery,
                                              const QList<AUDIO_EFFECT> &effects);

// The microphone for a Recording, captured with WASAPI as a communications
// stream, which brings in the echo cancellation of a microphone whose driver
// has it, with the default output as what it cancels where Windows lets
// Speecher say so. Delivered as 16 kHz mono s16. deviceId names the
// microphone as the audio settings do, and an empty or missing one means the
// default microphone. Qt Multimedia cannot set a stream's category, which is
// why dictation's microphone, which needs no echo cancellation, is Qt's and
// this one is not.
class WinCommunicationsAudioInput final : public WinCaptureInput {
public:
    explicit WinCommunicationsAudioInput(const QString &deviceId, QObject *parent = nullptr);
};

} // namespace speecher
