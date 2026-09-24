package app.speecher.android.dictation

import app.speecher.protocol.ClaudeVoiceClient
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens
import app.speecher.protocol.RefinementContext
import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import app.speecher.protocol.refineTranscript
import app.speecher.protocol.webSocketTransport
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executor
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import mockwebserver3.MockResponse
import mockwebserver3.MockResponseBody
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
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
                                    // Close the server side too, as real servers do, so
                                    // MockWebServer's task queue drains when the use block exits
                                    // instead of giving up waiting for it to shut down.
                                    webSocket.close(1000, null)
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
                    { _, raw, _ -> raw },
                    { error("no batch pass") },
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
    fun `speech before the connection opens is captured and sent once it does`() {
        val capture = Capture()
        val client = Client()
        val tasks = ArrayDeque<Runnable>()
        lateinit var speech: (SpeechEvent) -> Unit
        val engine =
            DictationEngine(
                capture::capture,
                capture::stop,
                { _, events ->
                    speech = events
                    client
                },
                { _, raw, _ -> raw },
                null,
                { true },
                Executor { tasks.add(it) },
                {},
            )
        engine.start(Provider.Claude)
        assertEquals(DictationState.Listening(), engine.state)
        tasks.removeFirst().run() // The microphone, started by the tap.
        capture.audio?.invoke(byteArrayOf(1, 2), 0.5f)
        assertEquals(DictationState.Listening("", "", 0.5f), engine.state)
        tasks.removeFirst().run() // The connection, which returns the client after the audio.
        speech(SpeechEvent.Connected)
        capture.audio?.invoke(byteArrayOf(3), 0.5f)
        assertEquals(listOf(listOf<Byte>(1, 2), listOf<Byte>(3)), client.audio.map { it.toList() })
    }

    @Test
    fun `refinement streams into the panel and commits the final text once`() {
        MockWebServer().use { server ->
            val firstShown = CountDownLatch(1)
            var streamedBeforeEnd = false
            fun chunk(event: String, delta: String) =
                "event: $event\ndata: {\"delta\":\"$delta\"}\n\n"
                    .let { "${it.length.toString(16)}\r\n$it\r\n" }
            server.enqueue(
                MockResponse.Builder()
                    .addHeader("Transfer-Encoding", "chunked")
                    .body(
                        object : MockResponseBody {
                            override val contentLength = -1L

                            override fun writeTo(sink: okio.BufferedSink) {
                                sink.writeUtf8(chunk("response.output_text.delta", "Hello")).flush()
                                // The rest waits until the panel shows the first token, so a reader
                                // that
                                // takes the reply whole leaves streamedBeforeEnd false.
                                streamedBeforeEnd = firstShown.await(3, TimeUnit.SECONDS)
                                sink
                                    .writeUtf8(chunk("response.output_text.delta", " there."))
                                    .writeUtf8(chunk("response.completed", ""))
                                    .writeUtf8("0\r\n\r\n")
                                    .flush()
                            }
                        }
                    )
                    .build()
            )
            server.start()
            lateinit var speech: (SpeechEvent) -> Unit
            val refined = mutableListOf<String>()
            val commits = mutableListOf<String>()
            val engine =
                DictationEngine(
                    { _, _ -> },
                    {},
                    { _, events ->
                        speech = events
                        Client()
                    },
                    { _, raw, onText ->
                        refineTranscript(
                            OkHttpClient(),
                            OAuthProvider.ChatGpt,
                            OAuthTokens("access", "", "", 0, ""),
                            raw,
                            emptyList(),
                            "gpt-6-luna",
                            "none",
                            RefinementContext(),
                            server.url("/codex").toString(),
                            onText,
                        )
                    },
                    null,
                    { commits.add(it) },
                    Executor { it.run() },
                    { state ->
                        if (state is DictationState.Refining && state.refined.isNotEmpty()) {
                            refined.add(state.refined)
                            firstShown.countDown()
                        }
                    },
                )
            engine.start(Provider.ChatGpt)
            speech(SpeechEvent.Final("hello there"))
            engine.insertRefined(Provider.ChatGpt)
            speech(SpeechEvent.Completed)
            assertTrue(streamedBeforeEnd)
            assertEquals(listOf("Hello", "Hello there."), refined)
            assertEquals(listOf("Hello there."), commits)
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
                { _, raw, _ -> raw },
                { error("no batch pass") },
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
                { _, raw, _ -> raw },
                { error("no batch pass") },
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
                { _, _, _ -> "Hello." },
                { error("no batch pass") },
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
                { _, raw, _ -> raw },
                { error("no batch pass") },
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
                { _, raw, _ -> raw },
                { error("no batch pass") },
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
                { _, raw, _ -> raw },
                { error("no batch pass") },
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
                { _, raw, _ -> if (++attempts == 1) error("temporary") else "$raw refined" },
                { error("no batch pass") },
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

    @Test
    fun `ChatGPT refined insert re-transcribes the recording then cleans up, falling back to the stream`() {
        val capture = Capture()
        lateinit var speech: (SpeechEvent) -> Unit
        val uploads = mutableListOf<List<Byte>>()
        var batchFails = false
        val commits = mutableListOf<String>()
        fun engine() =
            DictationEngine(
                capture::capture,
                capture::stop,
                { _, events ->
                    speech = events
                    Client()
                },
                { _, raw, _ -> "clean: $raw" },
                { pcm ->
                    uploads.add(pcm.toList())
                    if (batchFails) error("HTTP 500") else "Hello there, friend."
                },
                { commits.add(it) },
                Executor { it.run() },
                {},
            )
        val batch = engine()
        batch.start(Provider.ChatGpt)
        speech(SpeechEvent.Connected)
        capture.audio?.invoke(byteArrayOf(1, 2), 0f)
        capture.audio?.invoke(byteArrayOf(3, 4), 0f)
        speech(SpeechEvent.Final("hello there friend"))
        batch.insertRefined(null)
        speech(SpeechEvent.Completed)
        assertEquals(listOf(listOf<Byte>(1, 2, 3, 4)), uploads)
        assertEquals(listOf("Hello there, friend."), commits)

        batchFails = true
        val fallback = engine()
        fallback.start(Provider.ChatGpt)
        speech(SpeechEvent.Connected)
        capture.audio?.invoke(byteArrayOf(5), 0f)
        speech(SpeechEvent.Final("streamed words"))
        fallback.insertRefined(Provider.Claude)
        speech(SpeechEvent.Completed)
        assertEquals(listOf<Byte>(5), uploads.last())
        assertEquals("clean: streamed words", commits.last())
    }

    @Test
    fun `ChatGPT plain insert commits the batch text with the extra pass on, the stream with it off`() {
        val capture = Capture()
        lateinit var speech: (SpeechEvent) -> Unit
        val commits = mutableListOf<String>()
        fun engine(transcribe: ((ByteArray) -> String)?) =
            DictationEngine(
                capture::capture,
                capture::stop,
                { _, events ->
                    speech = events
                    Client()
                },
                { _, _, _ -> error("plain Insert never cleans up") },
                transcribe,
                { commits.add(it) },
                Executor { it.run() },
                {},
            )
        for (pass in listOf({ _: ByteArray -> "Hello there, friend." }, null)) {
            val engine = engine(pass)
            engine.start(Provider.ChatGpt)
            speech(SpeechEvent.Connected)
            capture.audio?.invoke(byteArrayOf(1, 2), 0f)
            speech(SpeechEvent.Final("hello there friend"))
            engine.insert()
            speech(SpeechEvent.Completed)
        }
        assertEquals(listOf("Hello there, friend.", "hello there friend"), commits)
    }

    @Test
    fun `ChatGPT refined insert with the extra pass off only cleans up`() {
        val capture = Capture()
        lateinit var speech: (SpeechEvent) -> Unit
        val commits = mutableListOf<String>()
        val engine =
            DictationEngine(
                capture::capture,
                capture::stop,
                { _, events ->
                    speech = events
                    Client()
                },
                { _, raw, _ -> "clean: $raw" },
                null,
                { commits.add(it) },
                Executor { it.run() },
                {},
            )
        engine.start(Provider.ChatGpt)
        speech(SpeechEvent.Connected)
        capture.audio?.invoke(byteArrayOf(1, 2), 0f)
        speech(SpeechEvent.Final("streamed words"))
        engine.insertRefined(Provider.ChatGpt)
        speech(SpeechEvent.Completed)
        assertEquals(listOf("clean: streamed words"), commits)
    }
}
