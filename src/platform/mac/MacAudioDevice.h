#pragma once

#include <QByteArray>

#include <optional>

namespace speecher::mac {

// The sample rate the CoreAudio device runs at now, found by the UID Qt uses
// as its QAudioDevice id; nothing when no device has that UID.
std::optional<int> nominalSampleRate(const QByteArray &deviceUid);

} // namespace speecher::mac
