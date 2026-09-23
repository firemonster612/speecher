package app.speecher.android.dictation

import app.speecher.protocol.ClaudeVoiceClient
import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import app.speecher.protocol.webSocketTransport
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executor
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DictationEngineTest {
    @Test
    fun `final sent after CloseStream is committed`() {
        MockWebServer().use { server ->
            val closes = LinkedBlockingQueue<String>()
            server.enqueue(
                MockResponse.Builder()
                    .webSocketUpgrade(
                        object : WebSocketListener() {
                            override fun onMessage(webSocket: WebSocket, text: String) {
                                if (text == "{\"type\":\"CloseStream\"}") {
                                    closes.add(text)
                                    webSocket.send(
                                        "{\"type\":\"TranscriptEndpoint\",\"data\":\"late final\"}"
                                    )
                                }
                            }
                        }
                    )
                    .build()
            )
            server.start()
            val listening = CountDownLatch(1)
            val commits = LinkedBlockingQueue<String>()
            val engine =
                DictationEngine(
                    { _, _ -> },
                    {},
                    { _, events ->
                        ClaudeVoiceClient(
                            webSocketTransport(server.url("/").toString()),
                            "fake",
                            emptyList(),
                            events,
                            server.url("/voice").toString().replaceFirst("http", "ws"),
                        )
                    },
                    { _, raw -> raw },
                    { commits.add(it) },
                    Executor { it.run() },
                    { if (it is DictationState.Listening) listening.countDown() },
                )
            engine.start(Provider.Claude)
            assertTrue(listening.await(3, TimeUnit.SECONDS))
            engine.insert()
            assertEquals("{\"type\":\"CloseStream\"}", closes.poll(3, TimeUnit.SECONDS))
            assertEquals("late final", commits.poll(3, TimeUnit.SECONDS))
            engine.close()
        }
    }

    private class Capture {
        var audio: ((ByteArray, Float) -> Unit)? = null

        fun capture(shouldContinue: () -> Boolean, onAudio: (ByteArray, Float) -> Unit) {
            if (!shouldContinue()) return
            audio = onAudio
        }

        fun stop() {
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
    fun `raw insert commits once`() {
        val capture = Capture()
        val client = Client()
        val commits = mutableListOf<String>()
        lateinit var speech: (SpeechEvent) -> Unit
        val engine =
            DictationEngine(
                capture::capture,
                capture::stop,
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
        assertEquals(DictationState.Listening("", "hello", 0f), engine.state)
        engine.insert()
        engine.insert()
        speech(SpeechEvent.Final("world"))
        speech(SpeechEvent.Completed)
        assertEquals(listOf("world"), commits)
        assertTrue(client.stopped)
        engine.cancel()
        assertEquals(listOf("world"), commits)
    }

    @Test
    fun `audio level reaches listening state and cancel leaves field untouched`() {
        val capture = Capture()
        val client = Client()
        val commits = mutableListOf<String>()
        lateinit var speech: (SpeechEvent) -> Unit
        val engine =
            DictationEngine(
                capture::capture,
                capture::stop,
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
        capture.audio?.invoke(byteArrayOf(1, 2), 0.5f)
        assertEquals(DictationState.Listening("", "", 0.5f), engine.state)
        assertEquals(listOf(1.toByte(), 2.toByte()), client.audio.single().toList())
        engine.cancel()
        assertTrue(client.cancelled)
        assertTrue(commits.isEmpty())
    }

    @Test
    fun `refined insert uses cleanup result once and failures stay in panel state`() {
        val capture = Capture()
        val commits = mutableListOf<String>()
        lateinit var speech: (SpeechEvent) -> Unit
        val engine =
            DictationEngine(
                capture::capture,
                capture::stop,
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
        speech(SpeechEvent.Completed)
        engine.insert()
        assertEquals(listOf("Hello."), commits)

        val failed =
            DictationEngine(
                capture::capture,
                capture::stop,
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

    @Test
    fun `failed commit keeps transcript available for retry`() {
        val capture = Capture()
        lateinit var speech: (SpeechEvent) -> Unit
        var commitSucceeds = false
        val commits = mutableListOf<String>()
        val engine =
            DictationEngine(
                capture::capture,
                capture::stop,
                { _, events ->
                    speech = events
                    Client()
                },
                { _, raw -> raw },
                { text -> if (commitSucceeds) commits.add(text) else false },
                Executor { it.run() },
                {},
            )
        engine.start(Provider.Claude)
        speech(SpeechEvent.Final("keep this"))
        engine.insert()
        speech(SpeechEvent.Completed)
        assertEquals("keep this", (engine.state as DictationState.Failed).transcript)
        commitSucceeds = true
        engine.insert()
        assertEquals(listOf("keep this"), commits)
    }

    @Test
    fun `partials stay interim until a final commits them`() {
        val capture = Capture()
        lateinit var speech: (SpeechEvent) -> Unit
        val engine =
            DictationEngine(
                capture::capture,
                capture::stop,
                { _, events ->
                    speech = events
                    Client()
                },
                { _, raw -> raw },
                { true },
                Executor { it.run() },
                {},
            )
        engine.start(Provider.ChatGpt)
        speech(SpeechEvent.Connected)
        speech(SpeechEvent.Partial("hello"))
        assertEquals(DictationState.Listening("", "hello", 0f), engine.state)
        speech(SpeechEvent.Final("hello there"))
        assertEquals(DictationState.Listening("hello there", "", 0f), engine.state)
        speech(SpeechEvent.Partial("friend"))
        assertEquals(DictationState.Listening("hello there", "friend", 0f), engine.state)
    }

    @Test
    fun `retry repeats refinement on saved transcript`() {
        val capture = Capture()
        lateinit var speech: (SpeechEvent) -> Unit
        var attempts = 0
        val commits = mutableListOf<String>()
        val engine =
            DictationEngine(
                capture::capture,
                capture::stop,
                { _, events ->
                    speech = events
                    Client()
                },
                { _, raw -> if (++attempts == 1) error("temporary") else "$raw refined" },
                { commits.add(it) },
                Executor { it.run() },
                {},
            )
        engine.start(Provider.Claude)
        speech(SpeechEvent.Final("save me"))
        engine.insertRefined(Provider.Claude)
        speech(SpeechEvent.Completed)
        assertEquals("save me", (engine.state as DictationState.Failed).transcript)
        engine.retry()
        assertEquals(listOf("save me refined"), commits)
    }
}
