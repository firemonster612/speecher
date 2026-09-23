package app.speecher.android.dictation

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import kotlin.math.log10
import kotlin.math.sqrt

/** Captures 16 kHz mono PCM16 on the caller's worker thread. */
class Microphone(private val context: Context) {
    @Volatile private var recorder: AudioRecord? = null
    @Volatile private var active = false

    fun capture(shouldContinue: () -> Boolean, onAudio: (ByteArray, Float) -> Unit) {
        if (
            context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) !=
                PackageManager.PERMISSION_GRANTED
        )
            throw SecurityException("Microphone permission is missing")
        if (!shouldContinue()) return
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
                val ready = active && shouldContinue()
                if (ready) recorder = audio
                ready
            }
        if (!shouldRecord) {
            audio.release()
            return
        }
        try {
            audio.startRecording()
            val samples = ShortArray(1600)
            var smoothed = 0f
            while (recorder === audio && shouldContinue()) {
                val count = audio.read(samples, 0, samples.size)
                if (count < 0) error("Microphone read failed: $count")
                if (count == 0) continue
                val bytes = ByteArray(count * 2)
                var sumSquares = 0.0
                for (index in 0 until count) {
                    val sample = samples[index].toInt()
                    bytes[index * 2] = sample.toByte()
                    bytes[index * 2 + 1] = (sample shr 8).toByte()
                    sumSquares += (sample * sample).toDouble()
                }
                val rms = (sqrt(sumSquares / count) / 32768f).toFloat()
                val target = loudness(rms)
                // Rise fast so speech shows the instant it starts, fall slowly so the bars read as
                // a
                // voice settling rather than a strobe.
                smoothed += (target - smoothed) * (if (target > smoothed) ATTACK else DECAY)
                onAudio(bytes, smoothed)
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

    /**
     * Maps a 0..1 RMS amplitude onto 0..1 perceptually. VOICE_RECOGNITION applies AGC and noise
     * suppression, so ordinary speech sits well below full scale; a −50..−12 dBFS window puts it
     * across most of the range instead of hugging the bottom, and silence maps to 0.
     */
    private fun loudness(rms: Float): Float {
        if (rms <= 0f) return 0f
        val db = 20f * log10(rms)
        return ((db - FLOOR_DB) / (CEIL_DB - FLOOR_DB)).coerceIn(0f, 1f)
    }

    private companion object {
        const val FLOOR_DB = -50f
        const val CEIL_DB = -12f
        const val ATTACK = 0.6f
        const val DECAY = 0.18f
    }
}
