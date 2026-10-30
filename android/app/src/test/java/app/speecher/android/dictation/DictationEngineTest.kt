package app.speecher.android.dictation

import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.SpeechProvider
import app.speecher.protocol.Cancellation
import app.speecher.protocol.ClaudeVoiceClient
import app.speecher.protocol.CodexDictationClient
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens
import app.speecher.protocol.ProviderFailure
import app.speecher.protocol.ProviderFailureKind
import app.speecher.protocol.RefinementContext
import app.speecher.protocol.Replacement
import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import app.speecher.protocol.WebSocketTransport
import app.speecher.protocol.refineTranscript
import java.io.IOException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executor
import java.util.concurrent.TimeUnit
import mockwebserver3.MockResponse
import mockwebserver3.MockResponseBody
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DictationEngineTest {
    @Test
    fun `speech before the connection opens is captured and sent once it does`() {
        val tasks = ManualExecutor()
        val t = TestDictation(executor = tasks)
        t.engine.start(SpeechProvider.Claude)
        assertEquals(DictationState.Listening(), t.engine.state)
        tasks.runNext() // The microphone, started by the tap.
        t.hear(1, 2, level = 0.5f)
        assertEquals(DictationState.Listening("", "", 0.5f), t.engine.state)
        tasks.runNext() // The connection, which returns the client after the audio.
        t.speak(SpeechEvent.Connected)
        t.hear(3, level = 0.5f)
        assertEquals(
            listOf(listOf<Byte>(1, 2), listOf<Byte>(3)),
            t.client.audio.map { it.toList() },
        )
    }

    @Test
    fun `a stream closed with 1012 mid-dictation reconnects without losing words or failing`() {
        val tasks = ManualExecutor()
        val t = TestDictation(executor = tasks)
        t.engine.start(SpeechProvider.Claude)
        tasks.runNext() // The microphone.
        tasks.runNext() // The first connection.
        t.speak(SpeechEvent.Connected)
        t.speak(SpeechEvent.Final("first part"))
        t.speak(SpeechEvent.Partial("still talking"))
        t.speak(SpeechEvent.Failed(ProviderFailureKind.Network, "closed 1012", retryable = true))
        t.hear(7, level = 0.5f) // Spoken while the new stream opens.
        assertEquals(
            DictationState.Listening("first part still talking", "", 0.5f, reconnecting = true),
            t.engine.state,
        )
        tasks.runNext() // The second connection.
        t.speak(SpeechEvent.Connected)
        t.speak(SpeechEvent.Final("second part"))
        assertTrue(t.clients[0].cancelled)
        assertEquals(listOf(listOf<Byte>(7)), t.clients[1].audio.map { it.toList() })
        assertEquals(
            DictationState.Listening("first part still talking second part", "", 0.5f),
            t.engine.state,
        )
        assertTrue(t.states.none { it is DictationState.Failed })
    }

    /**
     * Starts dictating with [tasks] run by hand, then loses the first stream after it heard
     * [heard].
     */
    private fun dropAfter(heard: String, tasks: ManualExecutor) =
        TestDictation(executor = tasks).apply {
            engine.start(SpeechProvider.Claude)
            tasks.runNext() // The microphone.
            tasks.runNext() // The first connection.
            speak(SpeechEvent.Connected)
            speak(SpeechEvent.Final(heard))
            speak(SpeechEvent.Failed(ProviderFailureKind.Network, "closed 1006", retryable = true))
        }

    @Test
    fun `a drop once the reconnect budget is spent fails with the transcript kept`() {
        val tasks = ManualExecutor()
        val t = dropAfter("part 0", tasks)
        repeat(2) { stream ->
            tasks.runNext() // The reconnect, after its backoff.
            t.speak(SpeechEvent.Connected)
            t.speak(SpeechEvent.Final("part ${stream + 1}"))
            t.speak(
                SpeechEvent.Failed(ProviderFailureKind.Network, "closed 1006", retryable = true)
            )
        }
        assertEquals(listOf(1_000L, 3_000L), t.pauses)
        assertEquals(
            DictationState.Failed(
                FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Network),
                "closed 1006",
                "part 0 part 1 part 2",
            ),
            t.engine.state,
        )
        assertTrue(tasks.isEmpty())
        assertTrue(t.engine.retry()) // Listens again, for the start vibration.
    }

    @Test
    fun `a reconnect that cannot connect retries with the rest of the budget`() {
        val tasks = ManualExecutor()
        val t = dropAfter("before the drop", tasks)
        t.connectFailure = IOException("network down")
        tasks.runNext() // The first reconnect, with the network still down.
        t.hear(9) // Spoken while offline.
        t.connectFailure = null
        tasks.runNext() // The second reconnect.
        t.speak(SpeechEvent.Connected)
        assertEquals(listOf(1_000L, 3_000L), t.pauses)
        assertEquals(listOf(listOf<Byte>(9)), t.clients[1].audio.map { it.toList() })
        assertEquals(DictationState.Listening("before the drop", "", 0f), t.engine.state)
    }

    @Test
    fun `an authentication failure mid-dictation fails instead of reconnecting`() {
        val tasks = ManualExecutor()
        val t = TestDictation(executor = tasks)
        t.engine.start(SpeechProvider.Claude)
        tasks.runNext() // The microphone.
        tasks.runNext() // The connection.
        t.speak(SpeechEvent.Connected)
        t.speak(SpeechEvent.Final("kept"))
        t.speak(SpeechEvent.Failed(ProviderFailureKind.Authentication, "HTTP 401"))
        assertEquals(
            DictationState.Failed(
                FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Authentication),
                "HTTP 401",
                "kept",
            ),
            t.engine.state,
        )
        assertTrue(tasks.isEmpty())
    }

    @Test
    fun `a spoken language the provider can't listen for fails in the panel`() {
        val reason = FailureReason.SpokenLanguage(SpeechProvider.Claude, "cy")
        val t = TestDictation()
        t.connectFailure = CannotListen(reason)
        t.engine.start(SpeechProvider.Claude)
        assertEquals(
            DictationState.Failed(reason, "SpokenLanguage(provider=Claude, language=cy)", ""),
            t.engine.state,
        )
    }

    @Test
    fun `Insert during a reconnect that cannot connect inserts what was heard`() {
        val tasks = ManualExecutor()
        val t = dropAfter("heard so far", tasks)
        t.connectFailure = IOException("network down")
        t.engine.insert()
        tasks.runNext()
        assertEquals(listOf("heard so far"), t.commits)
    }

    /** A socket that keeps what it was sent; one that [refusesAudio] fails inside each send. */
    private class FakeTransport(private val refusesAudio: Boolean = false) : WebSocketTransport {
        lateinit var server: WebSocketTransport.Listener
        val sent = mutableListOf<String>()

        override fun open(
            url: String,
            headers: Map<String, String>,
            subprotocol: String?,
            listener: WebSocketTransport.Listener,
        ) {
            server = listener
        }

        override fun sendText(text: String) = sent.add(text)

        override fun sendBinary(bytes: ByteArray): Boolean {
            if (!refusesAudio) return sent.add("binary:${bytes.joinToString()}")
            server.onFailure(IOException("send queue full"), null)
            return false
        }

        override fun close(code: Int, reason: String?) {}

        override fun cancel() {}
    }

    @Test
    fun `a stream that fails as it is sent audio fails the dictation, and Retry listens again`() {
        val transports = mutableListOf<FakeTransport>()
        val tasks = ManualExecutor()
        val t =
            TestDictation(
                executor = tasks,
                speechClient = { events ->
                    val transport = FakeTransport(refusesAudio = true).also(transports::add)
                    ClaudeVoiceClient(transport, "token", emptyList(), "en", events)
                },
            )
        t.engine.start(SpeechProvider.Claude)
        tasks.runNext() // The microphone.
        // The first stream and both reconnects go live, then fail on their first audio.
        repeat(3) {
            tasks.runNext()
            transports.last().server.onOpen()
            t.hear(1)
        }
        assertEquals(
            DictationState.Failed(
                FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Network),
                "send queue full",
                "",
            ),
            t.engine.state,
        )
        assertTrue(t.engine.retry())
    }

    /**
     * Dictates through four provider sessions that each end on the server's side, with a word
     * spoken after each end and before the next session opens, and returns every session's
     * transport. More rollovers than the reconnect budget, and none of them may fail.
     */
    private fun rollOver(
        provider: SpeechProvider,
        client: (WebSocketTransport, (SpeechEvent) -> Unit) -> SpeechClient,
        session: (WebSocketTransport.Listener, String) -> Unit,
    ): List<FakeTransport> {
        val transports = mutableListOf<FakeTransport>()
        val tasks = ManualExecutor()
        val t =
            TestDictation(
                executor = tasks,
                speechClient = { events -> client(FakeTransport().also(transports::add), events) },
            )
        t.engine.start(provider)
        tasks.runNext() // The microphone.
        repeat(4) { index ->
            tasks.runNext() // The session's connection.
            session(transports[index].server, "part $index")
            t.hear(index.toByte())
        }
        assertEquals(
            DictationState.Listening("part 0 part 1 part 2 part 3", "", 0f),
            t.engine.state,
        )
        assertTrue(t.states.none { it is DictationState.Failed })
        t.engine.close()
        return transports
    }

    @Test
    fun `ChatGPT closing its session mid-dictation rolls over to a new one every time`() {
        val transports =
            rollOver(
                SpeechProvider.ChatGpt,
                { transport, events -> CodexDictationClient(transport, "token", "en", events) },
            ) { server, words ->
                server.onOpen()
                server.onText("""{"type":"session.started"}""")
                server.onText("""{"type":"transcript.final","utterance_id":"1","text":"$words"}""")
                server.onText("""{"type":"session.updated","session":{"status":"closed"}}""")
            }
        assertEquals(
            listOf(
                listOf("""{"type":"audio.append","audio":"AA=="}"""),
                listOf("""{"type":"audio.append","audio":"AQ=="}"""),
                listOf("""{"type":"audio.append","audio":"Ag=="}"""),
            ),
            transports.subList(1, 4).map { transport ->
                transport.sent.filter { it.startsWith("""{"type":"audio.append"""") }
            },
        )
    }

    @Test
    fun `Claude closing the stream mid-dictation rolls over to a new one every time`() {
        val transports =
            rollOver(
                SpeechProvider.Claude,
                { transport, events ->
                    ClaudeVoiceClient(transport, "token", emptyList(), "en", events)
                },
            ) { server, words ->
                server.onOpen()
                server.onText("""{"type":"TranscriptEndpoint","data":"$words"}""")
                server.onClosed(1000, "")
            }
        assertEquals(
            listOf(listOf("binary:0"), listOf("binary:1"), listOf("binary:2")),
            transports.subList(1, 4).map { transport ->
                transport.sent.filter { it.startsWith("binary:") }
            },
        )
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
                                // that takes the reply whole leaves streamedBeforeEnd false.
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
            val t =
                TestDictation(
                    refine = { _, raw, cancellation, onText ->
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
                            onText = onText,
                            cancellation = cancellation,
                        )
                    },
                    onState = { state ->
                        if (state is DictationState.Refining && state.refined.isNotEmpty())
                            firstShown.countDown()
                    },
                )
            t.engine.start(SpeechProvider.ChatGpt)
            t.speak(SpeechEvent.Final("hello there"))
            t.engine.insertRefined(CleanupProvider.ChatGpt)
            t.speak(SpeechEvent.Completed)
            assertTrue(streamedBeforeEnd)
            assertEquals(
                listOf("Hello", "Hello there."),
                t.states
                    .filterIsInstance<DictationState.Refining>()
                    .map { it.refined }
                    .filter { it.isNotEmpty() },
            )
            assertEquals(listOf("Hello there."), t.commits)
        }
    }

    @Test
    fun `raw insert commits once`() {
        val t = TestDictation()
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Connected)
        t.speak(SpeechEvent.Partial("hello"))
        assertEquals(DictationState.Listening("", "hello", 0f), t.engine.state)
        // Only the tap that stops the listening reports it, for the stop vibration, and binds the
        // words to the field focused then.
        assertTrue(t.engine.insert())
        assertFalse(t.engine.insert())
        assertEquals(1, t.fieldsBound)
        t.speak(SpeechEvent.Final("world"))
        t.speak(SpeechEvent.Completed)
        assertEquals(listOf("world"), t.commits)
        assertTrue(t.client.stopped)
        t.engine.cancel()
        assertEquals(listOf("world"), t.commits)
    }

    @Test
    fun `a server that transcribes after the stop takes Insert before any words, and nothing heard leaves the field alone`() {
        val refined = mutableListOf<String>()
        val t =
            TestDictation(
                refine = { _, raw, _, _ ->
                    refined.add(raw)
                    raw.uppercase()
                }
            )
        t.engine.start(SpeechProvider.Endpoint)
        assertEquals(DictationState.Listening(textAfterStop = true), t.engine.state)
        t.engine.insertRefined(CleanupProvider.Claude)
        assertTrue(t.client.stopped)
        t.speak(SpeechEvent.Final("hello there"))
        t.speak(SpeechEvent.Completed)
        assertEquals(listOf("HELLO THERE"), t.commits)

        // Committing no text would delete the selection, so the dictation ends with nothing.
        t.selection = "keep this"
        t.engine.start(SpeechProvider.Endpoint)
        t.engine.insertRefined(CleanupProvider.Claude)
        t.speak(SpeechEvent.Completed)
        assertEquals(listOf("HELLO THERE"), t.commits)
        assertEquals(listOf("HELLO THERE", ""), t.inserted.map { it.text })
        assertEquals(listOf("hello there"), refined)
    }

    @Test
    fun `a microphone that cannot start fails as unavailable, not as the provider`() {
        val t = TestDictation()
        t.microphoneFailure = IllegalStateException("Microphone unavailable")
        t.engine.start(SpeechProvider.Claude)
        assertEquals(
            DictationState.Failed(
                FailureReason.MicrophoneUnavailable,
                "Microphone unavailable",
                "",
            ),
            t.engine.state,
        )
    }

    /**
     * Dictates "hello there" over 150 ms of audio with Claude, asks for Insert refined with
     * [cleanup] (plain Insert when null) while the cleanup does [refine], inserts again if that
     * failed, and returns what the engine reported inserting.
     */
    private fun dictateAndInsert(cleanup: CleanupProvider?, refine: () -> String?): List<Inserted> {
        val t = TestDictation(refine = { _, _, _, _ -> refine() })
        t.engine.start(SpeechProvider.Claude)
        t.hear(*ByteArray(3200)) // 100 ms of 16 kHz PCM16.
        t.hear(*ByteArray(1600))
        t.speak(SpeechEvent.Final("hello there"))
        if (cleanup == null) t.engine.insert() else t.engine.insertRefined(cleanup)
        t.speak(SpeechEvent.Completed)
        if (t.engine.state is DictationState.Failed) t.engine.insert()
        return t.inserted
    }

    @Test
    fun `a commit reports the text, the listening time and the providers the dictation called`() {
        assertEquals(
            listOf(Inserted("hello there", 150, SpeechProvider.Claude, emptyList())),
            dictateAndInsert(null) { "unused" },
        )
        assertEquals(
            listOf(
                Inserted(
                    "Hello there.",
                    150,
                    SpeechProvider.Claude,
                    listOf(CleanupProvider.ChatGpt),
                )
            ),
            dictateAndInsert(CleanupProvider.ChatGpt) { "Hello there." },
        )
        // A cleanup that failed was still called, though the transcript went in as heard.
        assertEquals(
            listOf(
                Inserted("hello there", 150, SpeechProvider.Claude, listOf(CleanupProvider.Claude))
            ),
            dictateAndInsert(CleanupProvider.Claude) { throw IOException("offline") },
        )
        // A profile that does no cleanup calls no provider.
        assertEquals(
            listOf(Inserted("hello there", 150, SpeechProvider.Claude, emptyList())),
            dictateAndInsert(CleanupProvider.ChatGpt) { null },
        )
    }

    @Test
    fun `pause finishes the stream and keeps its words, and resume carries on in a new one`() {
        val t = TestDictation()
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Final("before"))
        t.engine.pause()
        assertTrue(t.clients[0].stopped)
        assertEquals(DictationState.Listening("before", "", 0f, paused = true), t.engine.state)

        // Audio after a resume waits for the paused stream's last words, then goes to the next.
        t.engine.resume()
        t.hear(7)
        assertEquals(1, t.clients.size)
        t.clients[0].events(SpeechEvent.Final("pause"))
        t.clients[0].events(SpeechEvent.Completed)
        assertEquals(2, t.clients.size)
        assertEquals(listOf(7.toByte()), t.clients[1].audio.single().toList())
        t.clients[1].events(SpeechEvent.Final("after"))
        t.engine.insert()
        t.clients[1].events(SpeechEvent.Completed)
        assertEquals(listOf("before pause after"), t.commits)
    }

    @Test
    fun `media stays paused from the tap through a pause until Insert, Cancel or a failure`() {
        val inserted = TestDictation()
        inserted.engine.start(SpeechProvider.Claude)
        inserted.engine.pause()
        inserted.engine.resume()
        inserted.engine.pause()
        assertEquals(listOf("pause"), inserted.media)
        inserted.engine.insert()
        assertEquals(listOf("pause", "resume"), inserted.media)

        val canceled = TestDictation()
        canceled.engine.start(SpeechProvider.Claude)
        canceled.engine.cancel()
        assertEquals(listOf("pause", "resume"), canceled.media)
        assertTrue(canceled.client.cancelled)

        val failed = TestDictation()
        failed.engine.start(SpeechProvider.Claude)
        failed.speak(SpeechEvent.Failed(ProviderFailureKind.Authentication))
        assertEquals(listOf("pause", "resume"), failed.media)
    }

    @Test
    fun `Insert after a resume sends what was heard since to a stream before inserting`() {
        val t = TestDictation()
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Final("before"))
        t.engine.pause()
        t.engine.resume()
        t.hear(7)
        t.engine.insert()
        t.clients[0].events(SpeechEvent.Final("pause"))
        t.clients[0].events(SpeechEvent.Completed)

        assertEquals(2, t.clients.size)
        assertEquals(listOf(7.toByte()), t.clients[1].audio.single().toList())
        assertTrue(t.clients[1].stopped)
        assertEquals(emptyList<String>(), t.commits)
        t.clients[1].events(SpeechEvent.Final("after"))
        t.clients[1].events(SpeechEvent.Completed)
        assertEquals(listOf("before pause after"), t.commits)
    }

    @Test
    fun `a brief resume between two pauses reaches a stream before Insert`() {
        val t = TestDictation()
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Final("before"))
        t.engine.pause()
        t.engine.resume()
        t.hear(7)
        t.engine.pause()
        t.clients[0].events(SpeechEvent.Final("pause"))
        t.clients[0].events(SpeechEvent.Completed)

        assertEquals(2, t.clients.size)
        assertEquals(listOf(7.toByte()), t.clients[1].audio.single().toList())
        assertTrue(t.clients[1].stopped)
        t.engine.insert()
        assertEquals(emptyList<String>(), t.commits)
        t.clients[1].events(SpeechEvent.Final("brief"))
        t.clients[1].events(SpeechEvent.Completed)
        assertEquals(listOf("before pause brief"), t.commits)
    }

    @Test
    fun `a stream that cannot open while paused keeps the words and waits for the resume`() {
        val t = TestDictation()
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Connected)
        t.speak(SpeechEvent.Final("before"))
        t.engine.pause()
        t.engine.resume()
        t.hear(7, level = 0.4f)
        t.engine.pause()
        t.connectFailure = IOException("offline")
        t.speak(SpeechEvent.Final("pause"))
        t.speak(SpeechEvent.Completed)

        assertEquals(
            DictationState.Listening("before pause", "", 0f, paused = true),
            t.engine.state,
        )
        t.connectFailure = null
        t.engine.resume()
        assertEquals(2, t.clients.size)
        assertEquals(listOf(7.toByte()), t.clients[1].audio.single().toList())
        assertFalse(t.clients[1].stopped)
    }

    @Test
    fun `pause and resume while the stream connects still end the paused stream`() {
        val tasks = ManualExecutor()
        val t = TestDictation(executor = tasks)
        t.engine.start(SpeechProvider.Claude)
        tasks.runNext() // The microphone, started by the tap.
        t.hear(1)
        t.engine.pause()
        t.engine.resume()
        tasks.runNext() // The connection, opening after the resume.
        tasks.runNext() // The microphone again.
        t.hear(2)

        assertTrue(t.clients[0].stopped)
        assertEquals(listOf(1.toByte()), t.clients[0].audio.single().toList())
        t.clients[0].events(SpeechEvent.Completed)
        tasks.runNext() // The next stream.
        assertEquals(listOf(2.toByte()), t.clients[1].audio.single().toList())
        assertFalse(t.clients[1].stopped)
    }

    @Test
    fun `cancel during refinement inserts nothing`() {
        lateinit var t: TestDictation
        t =
            TestDictation(
                refine = { _, raw, _, _ ->
                    t.engine.cancel()
                    raw
                }
            )
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Final("never inserted"))
        t.engine.insertRefined(CleanupProvider.Claude)
        t.speak(SpeechEvent.Completed)
        assertTrue(t.commits.isEmpty())
    }

    @Test
    fun `failed commit keeps transcript available for retry`() {
        val t = TestDictation()
        t.fieldTakesText = false
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Final("keep this"))
        t.engine.insert()
        t.speak(SpeechEvent.Completed)
        assertEquals("keep this", (t.engine.state as DictationState.Failed).transcript)
        t.fieldTakesText = true
        t.engine.insert()
        assertEquals(listOf("keep this", "keep this"), t.commits)
    }

    @Test
    fun `partials stay interim until a final commits them`() {
        val t = TestDictation()
        t.engine.start(SpeechProvider.ChatGpt)
        t.speak(SpeechEvent.Connected)
        t.speak(SpeechEvent.Partial("hello"))
        assertEquals(DictationState.Listening("", "hello", 0f), t.engine.state)
        t.speak(SpeechEvent.Final("hello there"))
        assertEquals(DictationState.Listening("hello there", "", 0f), t.engine.state)
        t.speak(SpeechEvent.Partial("friend"))
        assertEquals(DictationState.Listening("hello there", "friend", 0f), t.engine.state)
    }

    @Test
    fun `a cleanup failure is reported as the cleanup's, and retry repeats it on the transcript`() {
        var attempts = 0
        val t =
            TestDictation(
                refine = { _, raw, _, _ ->
                    if (++attempts == 1)
                        throw ProviderFailure(ProviderFailureKind.RateLimited, "HTTP 429", 429)
                    else "$raw refined"
                }
            )
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Final("save me"))
        t.engine.insertRefined(CleanupProvider.Claude)
        t.speak(SpeechEvent.Completed)
        assertEquals(
            DictationState.Failed(
                FailureReason.Cleanup(CleanupProvider.Claude, ProviderFailureKind.RateLimited),
                "Could not refine the transcript",
                "save me",
            ),
            t.engine.state,
        )
        assertFalse(t.engine.retry()) // Refines again without listening.
        assertEquals(listOf("save me refined"), t.commits)
    }

    @Test
    fun `ChatGPT refined insert re-transcribes the recording then cleans up, falling back to the stream`() {
        val uploads = mutableListOf<List<Byte>>()
        var batchFails = false
        fun dictation() =
            TestDictation(
                refine = { _, raw, _, _ -> "clean: $raw" },
                transcribe = { pcm, _ ->
                    uploads.add(pcm.toList())
                    if (batchFails) error("HTTP 500") else "Hello there, friend."
                },
            )
        val batch = dictation()
        batch.engine.start(SpeechProvider.ChatGpt)
        batch.speak(SpeechEvent.Connected)
        batch.hear(1, 2)
        batch.hear(3, 4)
        batch.speak(SpeechEvent.Final("hello there friend"))
        batch.engine.insertRefined(null)
        batch.speak(SpeechEvent.Completed)
        assertEquals(listOf(listOf<Byte>(1, 2, 3, 4)), uploads)
        assertEquals(listOf("Hello there, friend."), batch.commits)

        batchFails = true
        val fallback = dictation()
        fallback.engine.start(SpeechProvider.ChatGpt)
        fallback.speak(SpeechEvent.Connected)
        fallback.hear(5)
        fallback.speak(SpeechEvent.Final("streamed words"))
        fallback.engine.insertRefined(CleanupProvider.Claude)
        fallback.speak(SpeechEvent.Completed)
        assertEquals(listOf<Byte>(5), uploads.last())
        assertEquals(listOf("clean: streamed words"), fallback.commits)
    }

    @Test
    fun `ChatGPT batch pass runs up to 80 s of audio and is skipped past it`() {
        val uploadSizes = mutableListOf<Int>()
        // One second of 16 kHz mono PCM16.
        val second = ByteArray(32_000)
        val commits =
            listOf(80, 81).flatMap { seconds ->
                val t =
                    TestDictation(
                        refine = { _, _, _, _ -> error("plain Insert never cleans up") },
                        transcribe = { pcm, _ ->
                            uploadSizes.add(pcm.size)
                            "Batch text."
                        },
                    )
                t.engine.start(SpeechProvider.ChatGpt)
                t.speak(SpeechEvent.Connected)
                repeat(seconds) { t.hear(*second) }
                t.speak(SpeechEvent.Final("streamed text"))
                t.engine.insert()
                t.speak(SpeechEvent.Completed)
                t.commits
            }
        assertEquals(listOf(2_560_000), uploadSizes)
        assertEquals(listOf("Batch text.", "streamed text"), commits)
    }

    @Test
    fun `ChatGPT refined insert with the extra pass off only cleans up`() {
        val t = TestDictation(refine = { _, raw, _, _ -> "clean: $raw" })
        t.engine.start(SpeechProvider.ChatGpt)
        t.speak(SpeechEvent.Connected)
        t.hear(1, 2)
        t.speak(SpeechEvent.Final("streamed words"))
        t.engine.insertRefined(CleanupProvider.ChatGpt)
        t.speak(SpeechEvent.Completed)
        assertEquals(listOf("clean: streamed words"), t.commits)
    }

    @Test
    fun `Insert shows each wait in turn - the last words, the second pass, then the cleanup`() {
        val tasks = ManualExecutor()
        val t =
            TestDictation(
                refine = { _, raw, _, _ -> "clean: $raw" },
                transcribe = { _, _ -> "batch words" },
                executor = tasks,
            )
        t.engine.start(SpeechProvider.ChatGpt)
        tasks.runNext() // The microphone.
        tasks.runNext() // The connection.
        t.speak(SpeechEvent.Connected)
        t.hear(1, level = 0.5f)
        t.speak(SpeechEvent.Final("streamed words"))
        t.engine.insertRefined(CleanupProvider.Claude)
        val waits = mutableListOf(t.engine.state)
        t.speak(SpeechEvent.Completed)
        waits.add(t.engine.state)
        tasks.runNext() // The second pass.
        waits.add(t.engine.state)
        assertEquals(
            listOf(
                DictationState.Listening("streamed words", "", 0f, stopping = true),
                DictationState.Refining("streamed words", transcribingAgain = true),
                DictationState.Refining("batch words"),
            ),
            waits,
        )
    }

    @Test
    fun `no sign-in fails as missing setup without reconnecting`() {
        val tasks = ManualExecutor()
        val t = TestDictation(executor = tasks)
        t.connectFailure = ProviderFailure(ProviderFailureKind.Unavailable, "Not signed in")
        t.engine.start(SpeechProvider.Claude)
        tasks.runNext() // The microphone.
        tasks.runNext() // The connection.
        assertEquals(
            DictationState.Failed(
                FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Unavailable),
                "Could not connect to the speech provider",
                "",
            ),
            t.engine.state,
        )
        assertTrue(tasks.isEmpty())
    }

    /** A Claude cleanup that sends its request to [server]. */
    private fun claudeCleanupAt(
        server: MockWebServer
    ): (CleanupProvider, String, Cancellation, (String) -> Unit) -> String? =
        { _, raw, cancellation, _ ->
            refineTranscript(
                OkHttpClient(),
                OAuthProvider.Claude,
                OAuthTokens("access", "", "", 0, ""),
                raw,
                emptyList(),
                "claude-sonnet-5-5",
                "low",
                RefinementContext(),
                server.url("/v1").toString(),
                cancellation = cancellation,
            )
        }

    @Test
    fun `cancel aborts a cleanup request in flight`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().headersDelay(5, TimeUnit.SECONDS).build())
            server.start()
            val workers = mutableListOf<Thread>()
            val t =
                TestDictation(
                    refine = claudeCleanupAt(server),
                    executor = Executor { task -> Thread(task).also(workers::add).start() },
                )
            t.engine.start(SpeechProvider.Claude)
            workers.forEach(Thread::join) // The microphone and the connection.
            t.speak(SpeechEvent.Final("never inserted"))
            t.engine.insertRefined(CleanupProvider.Claude)
            t.speak(SpeechEvent.Completed)
            server.takeRequest()
            t.engine.cancel()
            // The request would otherwise hold its worker for the server's five seconds.
            workers.last().join(2_000)
            assertFalse(workers.last().isAlive)
            assertTrue(t.commits.isEmpty())
        }
    }

    /**
     * A dictation of [heard] with the [rules] an Insert reads, ended by the Insert [end] taps, the
     * cleanup answering as [refine] does with the provider, its input and the streamed-text
     * callback.
     */
    private fun dictate(
        heard: String,
        refine: (CleanupProvider, String, (String) -> Unit) -> String,
        rules: () -> List<Replacement> = { listOf(Replacement("sign off", "Regards,\nEfox")) },
        end: DictationEngine.() -> Unit,
    ): TestDictation {
        val t =
            TestDictation(
                refine = { provider, input, _, onText -> refine(provider, input, onText) },
                replacements = rules,
            )
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Final(heard))
        t.engine.end()
        t.speak(SpeechEvent.Completed)
        return t
    }

    @Test
    fun `both Inserts replace spoken phrases, and refinement keeps snippets in placeholders`() {
        val plain =
            dictate("thanks sign off", { _, _, _ -> error("plain Insert never cleans up") }) {
                insert()
            }
        val sent = mutableListOf<String>()
        val refined =
            dictate(
                "thanks sign off",
                { _, input, onText ->
                    sent.add(input)
                    onText("Thanks, SPEECHER_BINDING_0.")
                    "Thanks, SPEECHER_BINDING_0."
                },
            ) {
                insertRefined(CleanupProvider.Claude)
            }
        assertEquals(listOf("thanks SPEECHER_BINDING_0"), sent)
        assertTrue(
            DictationState.Refining("thanks sign off", "Thanks, Regards,\nEfox.") in refined.states
        )
        assertEquals(
            listOf("thanks Regards,\nEfox", "Thanks, Regards,\nEfox."),
            plain.commits + refined.commits,
        )
    }

    @Test
    fun `a snippet alone skips cleanup, and a garbled or failed cleanup inserts the replacements`() {
        val alone =
            dictate("sign off", { _, _, _ -> error("nothing to clean up") }) {
                insertRefined(CleanupProvider.Claude)
            }
        val garbled =
            dictate("thanks sign off", { _, _, _ -> "Thanks, speecher binding zero." }) {
                insertRefined(CleanupProvider.Claude)
            }
        // Each read of the rules stamps a later time; the Insert after the failure keeps the first.
        var reads = 0
        val failed =
            dictate(
                "noted at stamp",
                { _, _, _ -> throw IOException() },
                rules = { listOf(Replacement("stamp", "${++reads}:00")) },
            ) {
                insertRefined(CleanupProvider.Claude)
            }
        failed.engine.insert()
        assertEquals(
            listOf("Regards,\nEfox", "thanks Regards,\nEfox", "noted at 1:00"),
            alone.commits + garbled.commits + failed.commits,
        )
    }

    @Test
    fun `Insert during the cleanup stops its request and inserts the words as heard, still counting it`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().headersDelay(5, TimeUnit.SECONDS).build())
            server.start()
            val workers = mutableListOf<Thread>()
            val t =
                TestDictation(
                    refine = claudeCleanupAt(server),
                    executor = Executor { task -> Thread(task).also(workers::add).start() },
                )
            t.engine.start(SpeechProvider.Claude)
            workers.forEach(Thread::join) // The microphone and the connection.
            t.speak(SpeechEvent.Final("as heard"))
            t.engine.insertRefined(CleanupProvider.Claude)
            t.speak(SpeechEvent.Completed)
            server.takeRequest()
            t.engine.insert()
            workers.last().join(2_000)
            assertFalse(workers.last().isAlive)
            assertEquals(
                listOf(
                    Inserted("as heard", 0, SpeechProvider.Claude, listOf(CleanupProvider.Claude))
                ),
                t.inserted,
            )
        }
    }

    @Test
    fun `a cleanup that finishes after Insert took the words as heard is never inserted`() {
        val tasks = ManualExecutor()
        val t = TestDictation(refine = { _, _, _, _ -> "Refined." }, executor = tasks)
        t.fieldTakesText = false
        t.engine.start(SpeechProvider.Claude)
        tasks.runAll()
        t.speak(SpeechEvent.Final("as heard"))
        t.engine.insertRefined(CleanupProvider.Claude)
        t.speak(SpeechEvent.Completed)
        t.engine.insert()
        tasks.runNext() // The cleanup, done before it saw the cancel.
        assertEquals(listOf("as heard"), t.commits)
        assertEquals(
            DictationState.Failed(FailureReason.Commit, "Could not insert text", "as heard"),
            t.engine.state,
        )
    }

    /**
     * Dictates [heard] and taps Insert refined while the field's selection is [selection], the edit
     * answering as [edit] does with the selection and the instructions.
     */
    private fun editSelection(
        heard: String,
        selection: String,
        edit: (String, String) -> String?,
    ): TestDictation {
        val t =
            TestDictation(
                refine = { _, _, _, _ -> error("an edit is not a dictation") },
                editSelection = { _, selected, instructions, _ -> edit(selected, instructions) },
                replacements = { listOf(Replacement("sign off", "Regards,\nEfox")) },
            )
        t.selection = selection
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Final(heard))
        t.engine.insertRefined(CleanupProvider.Claude)
        t.speak(SpeechEvent.Completed)
        return t
    }

    @Test
    fun `Insert refined over a selection replaces it with the revision, phrases in it untouched`() {
        val edits = mutableListOf<Pair<String, String>>()
        val revised =
            editSelection("make it eight", "Dinner at seven, sign off") { selected, instructions ->
                edits.add(selected to instructions)
                "Dinner at eight, sign off"
            }
        // Words that are all a spoken phrase still go to the model, as instructions.
        val phrase =
            editSelection("sign off", "Dinner") { selected, instructions ->
                edits.add(selected to instructions)
                "Dinner SPEECHER_BINDING_0"
            }
        assertEquals(
            listOf(
                "Dinner at seven, sign off" to "make it eight",
                "Dinner" to "SPEECHER_BINDING_0",
            ),
            edits,
        )
        assertEquals(
            listOf("Dinner at eight, sign off", "Dinner Regards,\nEfox"),
            revised.commits + phrase.commits,
        )
        assertEquals(
            listOf(DictationState.Refining("make it eight", editsSelection = true)),
            revised.states.filterIsInstance<DictationState.Refining>(),
        )
        assertEquals("make it eight", revised.inserted.single().dictated)
    }

    @Test
    fun `a failed edit leaves the field alone until a retry revises the same selection`() {
        var answers = 0
        val t =
            editSelection("make it eight", "Dinner at seven") { _, _ ->
                if (answers++ == 0) throw ProviderFailure(ProviderFailureKind.Server, "502")
                "Dinner at eight"
            }
        assertEquals(
            DictationState.Failed(
                FailureReason.Cleanup(CleanupProvider.Claude, ProviderFailureKind.Server),
                "Could not refine the transcript",
                "make it eight",
                editsSelection = true,
            ),
            t.engine.state,
        )
        assertFalse(t.engine.insert()) // The instructions never go in as text.
        t.selection = "Dinner"
        t.engine.retry()
        assertEquals(
            FailureReason.SelectionChanged,
            (t.engine.state as DictationState.Failed).reason,
        )
        t.selection = "Dinner at seven"
        t.engine.retry()
        assertEquals(listOf("Dinner at eight"), t.commits)

        val noCleanup = editSelection("make it eight", "Dinner") { _, _ -> null }
        assertEquals(
            FailureReason.SelectionNeedsCleanup,
            (noCleanup.engine.state as DictationState.Failed).reason,
        )
        val invented = editSelection("add sign off", "Dinner") { _, _ -> "SPEECHER_BINDING_7" }
        assertEquals(
            FailureReason.Cleanup(CleanupProvider.Claude, ProviderFailureKind.InvalidResult),
            (invented.engine.state as DictationState.Failed).reason,
        )
        assertEquals(emptyList<String>(), noCleanup.commits + invented.commits)
    }

    @Test
    fun `Insert while an edit is cleaning up inserts nothing, and the edit still goes in`() {
        val tasks = ManualExecutor()
        val t =
            TestDictation(
                refine = { _, _, _, _ -> error("an edit is not a dictation") },
                editSelection = { _, _, _, _ -> "eight" },
                executor = tasks,
            )
        t.selection = "seven"
        t.engine.start(SpeechProvider.Claude)
        tasks.runAll()
        t.speak(SpeechEvent.Final("make it eight"))
        t.engine.insertRefined(CleanupProvider.Claude)
        t.speak(SpeechEvent.Completed)
        assertFalse(t.engine.insert())
        tasks.runNext() // The edit.
        assertEquals(listOf("eight"), t.commits)
    }
}
