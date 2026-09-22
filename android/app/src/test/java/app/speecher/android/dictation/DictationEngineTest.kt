package app.speecher.android.dictation

import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import java.util.concurrent.Executor
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DictationEngineTest {
    private class Capture : AudioCapture {
        var audio: ((ByteArray, Float) -> Unit)? = null

        override fun capture(onAudio: (ByteArray, Float) -> Unit) {
            audio = onAudio
        }

        override fun stop() {
            audio = null
        }
    }

    private class Client : SpeechClient {
        var stopped = false
        var cancelled = false
        val audio = mutableListOf<ByteArray>()

        override fun sendAudio(pcm: ByteArray) {
            audio.add(pcm)
        }

        override fun stop() {
            stopped = true
        }

        override fun cancel() {
            cancelled = true
        }
    }

    @Test
    fun `raw insert commits once and cancel leaves field untouched`() {
        val capture = Capture()
        val client = Client()
        val commits = mutableListOf<String>()
        lateinit var speech: (SpeechEvent) -> Unit
        val engine =
            DictationEngine(
                capture,
                { _, events ->
                    speech = events
                    client
                },
                { _, raw -> raw },
                { commits.add(it) },
                Executor { it.run() },
                {},
            )
        engine.start(Provider.Claude)
        speech(SpeechEvent.Connected)
        speech(SpeechEvent.Partial("hello"))
        assertEquals(DictationState.Listening("hello", 0f), engine.state)
        engine.insert()
        engine.insert()
        assertEquals(listOf("hello"), commits)
        assertTrue(client.stopped)
        engine.cancel()
        assertEquals(listOf("hello"), commits)
    }

    @Test
    fun `refined insert uses cleanup result once and failures stay in panel state`() {
        val commits = mutableListOf<String>()
        lateinit var speech: (SpeechEvent) -> Unit
        val engine =
            DictationEngine(
                Capture(),
                { _, events ->
                    speech = events
                    Client()
                },
                { _, _ -> "Hello." },
                { commits.add(it) },
                Executor { it.run() },
                {},
            )
        engine.start(Provider.ChatGpt)
        speech(SpeechEvent.Final("hello"))
        engine.insertRefined(Provider.Claude)
        engine.insert()
        assertEquals(listOf("Hello."), commits)

        val failed =
            DictationEngine(
                Capture(),
                { _, events ->
                    speech = events
                    Client()
                },
                { _, raw -> raw },
                { commits.add(it) },
                Executor { it.run() },
                {},
            )
        failed.start(Provider.Claude)
        speech(SpeechEvent.Failed(true))
        assertEquals(FailureReason.SignedOut, (failed.state as DictationState.Failed).reason)
    }
}
