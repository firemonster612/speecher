#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>

namespace speecher {

// Wraps 16-bit little-endian mono PCM in a WAV header.
inline QByteArray wavFromPcm16Mono(const QByteArray &pcm, int rateHz)
{
    QByteArray wav;
    QDataStream stream(&wav, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + pcm.size());
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1)
           << quint32(rateHz) << quint32(rateHz * 2) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    stream.writeRawData(pcm.constData(), pcm.size());
    return wav;
}

} // namespace speecher
