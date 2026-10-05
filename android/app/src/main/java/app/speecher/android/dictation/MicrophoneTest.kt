package app.speecher.android.dictation

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.util.concurrent.Executor

/**
 * The Test microphone row's test: a live level from the microphone, captured here and sent nowhere,
 * so it tells a phone microphone problem apart from an account or network one. It does not start
 * while a dictation is listening, and one starting ends it.
 */
class MicrophoneTest(
    private val capture:
        (shouldContinue: () -> Boolean, onAudio: (ByteArray, Float) -> Unit) -> Unit,
    private val executor: Executor = sharedExecutor,
    private val dictating: () -> Boolean = {
        ActiveDictation.engine?.state is DictationState.Listening
    },
) {
    var running by mutableStateOf(false)
        private set

    /** The input loudness from 0 to 1, as the panel's waveform shows it. */
    var level by mutableFloatStateOf(0f)
        private set

    /** What the test has found, or null before it has found anything. */
    var status by mutableStateOf<String?>(null)
        private set

    /** Identifies the current run, so a stopped run's capture thread changes nothing. */
    @Volatile private var run = 0
    private var silentBytes = 0

    @Synchronized
    fun start() {
        if (running || dictating()) return
        val current = ++run
        running = true
        status = LISTENING
        silentBytes = 0
        executor.execute { listen(current) }
    }

    /** Leaves a detected input reported, since that is what the test was for. */
    @Synchronized
    fun stop() {
        run++
        running = false
        level = 0f
        if (status != DETECTED) status = null
    }

    fun toggle() = if (running) stop() else start()

    private fun listen(current: Int) {
        try {
            capture({ current == run && !dictating() }) { audio, level ->
                hear(current, audio, level)
            }
            synchronized(this) { if (current == run) stop() }
        } catch (_: SecurityException) {
            fail(current, NO_ACCESS)
        } catch (_: Exception) {
            fail(current, UNAVAILABLE)
        }
    }

    @Synchronized
    private fun hear(current: Int, audio: ByteArray, level: Float) {
        if (current != run) return
        this.level = level
        if (status == DETECTED) return
        // The desktop counts input from about -58 dBFS; this level starts at -50, so any counts.
        if (level > 0f) {
            status = DETECTED
            return
        }
        silentBytes += audio.size
        if (silentBytes >= SILENT_BYTES) status = SILENT
    }

    @Synchronized
    private fun fail(current: Int, message: String) {
        if (current != run) return
        stop()
        status = message
    }

    private companion object {
        const val LISTENING = "Listening for microphone input…"
        const val DETECTED = "Microphone input detected."
        const val SILENT =
            "No input yet — check that the microphone isn't muted or turned off in Quick " +
                "Settings."
        const val NO_ACCESS = "Microphone access is off."
        const val UNAVAILABLE = "Microphone unavailable"
        /** The desktop setup's five seconds without input, as 16 kHz PCM16 bytes. */
        const val SILENT_BYTES = 5 * 16000 * 2
    }
}
