package app.speecher.android.dictation

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import kotlin.math.abs

/** Captures 16 kHz mono PCM16 on the caller's worker thread. */
class Microphone(private val context: Context) {
    @Volatile private var recorder: AudioRecord? = null
    @Volatile private var active = false

    fun capture(onAudio: (ByteArray, Float) -> Unit) {
        if (
            context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) !=
                PackageManager.PERMISSION_GRANTED
        )
            throw SecurityException("Microphone permission is missing")
        active = true
        val rate = 16000
        val size =
            maxOf(
                AudioRecord.getMinBufferSize(
                    rate,
                    AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,
                ),
                rate,
            )
        val audio =
            AudioRecord(
                MediaRecorder.AudioSource.VOICE_RECOGNITION,
                rate,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                size,
            )
        if (audio.state != AudioRecord.STATE_INITIALIZED) {
            audio.release()
            error("Microphone unavailable")
        }
        val shouldRecord =
            synchronized(this) {
                if (active) recorder = audio
                active
            }
        if (!shouldRecord) {
            audio.release()
            return
        }
        try {
            audio.startRecording()
            val samples = ShortArray(1600)
            while (recorder === audio) {
                val count = audio.read(samples, 0, samples.size)
                if (count < 0) error("Microphone read failed: $count")
                if (count == 0) continue
                val bytes = ByteArray(count * 2)
                var peak = 0
                for (index in 0 until count) {
                    val sample = samples[index].toInt()
                    bytes[index * 2] = sample.toByte()
                    bytes[index * 2 + 1] = (sample shr 8).toByte()
                    peak = maxOf(peak, abs(sample))
                }
                onAudio(bytes, peak / 32768f)
            }
        } finally {
            if (audio.recordingState == AudioRecord.RECORDSTATE_RECORDING) audio.stop()
            audio.release()
            if (recorder === audio) recorder = null
        }
    }

    @Synchronized
    fun stop() {
        active = false
        recorder = null
    }
}
