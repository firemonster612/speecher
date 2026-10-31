package app.speecher.android.dictation

import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.SpeechProvider
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
import java.util.concurrent.Executor
import java.util.concurrent.TimeUnit
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DictationEngineTest {
    /** Starts dictating with [tasks] run by hand: the microphone, then a stream that goes live. */
    private fun live(tasks: ManualExecutor) =
        TestDictation(executor = tasks).apply {
            engine.start(SpeechProvider.Claude)
            tasks.runNext() // The microphone.
            tasks.runNext() // The connection.
            speak(SpeechEvent.Connected)
        }

    /** Dictates [heard] with [provider], ends it with [end], and the stream completes. */
    private fun TestDictation.dictate(
        heard: String,
        provider: SpeechProvider = SpeechProvider.Claude,
        end: DictationEngine.() -> Unit = { insertRefined(CleanupProvider.Claude) },
    ) = apply {
        engine.start(provider)
        speak(SpeechEvent.Final(heard))
        engine.end()
        speak(SpeechEvent.Completed)
    }

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
        assertEquals(listOf(listOf<Byte>(1, 2), listOf<Byte>(3)), t.client.heard)
    }

    @Test
    fun `a stream closed with 1012 mid-dictation reconnects without losing words or failing`() {
        val tasks = ManualExecutor()
        val t = live(tasks)
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
        assertEquals(listOf(listOf<Byte>(7)), t.clients[1].heard)
        assertEquals(
            DictationState.Listening("first part still talking second part", "", 0.5f),
            t.engine.state,
        )
        assertTrue(t.states.none { it is DictationState.Failed })
    }

    /** Starts dictating with [tasks] run by hand, then loses the stream after it heard [heard]. */
    private fun dropAfter(heard: String, tasks: ManualExecutor) =
        live(tasks).apply {
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
    fun `a reconnect that cannot connect retries with the rest of the budget, or Insert takes what was heard`() {
        val tasks = ManualExecutor()
        val t = dropAfter("before the drop", tasks)
        t.connectFailure = IOException("network down")
        tasks.runNext() // The first reconnect, with the network still down.
        t.hear(9) // Spoken while offline.
        t.connectFailure = null
        tasks.runNext() // The second reconnect.
        t.speak(SpeechEvent.Connected)
        assertEquals(listOf(1_000L, 3_000L), t.pauses)
        assertEquals(listOf(listOf<Byte>(9)), t.clients[1].heard)
        assertEquals(DictationState.Listening("before the drop", "", 0f), t.engine.state)

        val insertTasks = ManualExecutor()
        val inserted = dropAfter("heard so far", insertTasks)
        inserted.connectFailure = IOException("network down")
        inserted.engine.insert()
        insertTasks.runNext()
        assertEquals(listOf("heard so far"), inserted.commits)
    }

    @Test
    fun `failures a reconnect would not fix fail at once, keeping what was heard`() {
        // Dictation after [setup], then [meanwhile]; nothing may be left to run.
        fun failure(setup: TestDictation.() -> Unit, meanwhile: TestDictation.() -> Unit = {}) =
            ManualExecutor().let { tasks ->
                val t = TestDictation(executor = tasks).apply(setup)
                t.engine.start(SpeechProvider.Claude)
                tasks.runNext() // The microphone.
                tasks.runNext() // The connection.
                t.meanwhile()
                assertTrue(tasks.isEmpty())
                t.engine.state
            }
        fun speech(kind: ProviderFailureKind) = FailureReason.Speech(SpeechProvider.Claude, kind)
        val language = FailureReason.SpokenLanguage(SpeechProvider.Claude, "cy")
        assertEquals(
            listOf(
                DictationState.Failed(
                    speech(ProviderFailureKind.Authentication),
                    "HTTP 401",
                    "kept",
                ),
                DictationState.Failed(
                    speech(ProviderFailureKind.Unavailable),
                    "Could not connect to the speech provider",
                    "",
                ),
                DictationState.Failed(language, "SpokenLanguage(provider=Claude, language=cy)", ""),
                DictationState.Failed(
                    FailureReason.MicrophoneUnavailable,
                    "Microphone unavailable",
                    "",
                ),
            ),
            listOf(
                failure({}) {
                    speak(SpeechEvent.Connected)
                    speak(SpeechEvent.Final("kept"))
                    speak(SpeechEvent.Failed(ProviderFailureKind.Authentication, "HTTP 401"))
                },
                // No sign-in.
                failure({
                    connectFailure =
                        ProviderFailure(ProviderFailureKind.Unavailable, "Not signed in")
                }),
                failure({ connectFailure = CannotListen(language) }),
                // Not as the provider's failure.
                failure({ microphoneFailure = IllegalStateException("Microphone unavailable") }),
            ),
        )
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
     * spoken after each end and before the next session opens, and returns what each session after
     * the first was sent that [isAudio]. More rollovers than the reconnect budget, and none of them
     * may fail.
     */
    private fun rollOver(
        provider: SpeechProvider,
        client: (WebSocketTransport, (SpeechEvent) -> Unit) -> SpeechClient,
        isAudio: (String) -> Boolean,
        session: (WebSocketTransport.Listener, String) -> Unit,
    ): List<List<String>> {
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
        return transports.drop(1).map { it.sent.filter(isAudio) }
    }

    @Test
    fun `ChatGPT and Claude closing the session mid-dictation roll over to a new one every time`() {
        assertEquals(
            listOf("AA==", "AQ==", "Ag==").map {
                listOf("""{"type":"audio.append","audio":"$it"}""")
            },
            rollOver(
                SpeechProvider.ChatGpt,
                { transport, events -> CodexDictationClient(transport, "token", "en", events) },
                { it.startsWith("""{"type":"audio.append"""") },
            ) { server, words ->
                server.onOpen()
                server.onText("""{"type":"session.started"}""")
                server.onText("""{"type":"transcript.final","utterance_id":"1","text":"$words"}""")
                server.onText("""{"type":"session.updated","session":{"status":"closed"}}""")
            },
        )
        assertEquals(
            listOf(listOf("binary:0"), listOf("binary:1"), listOf("binary:2")),
            rollOver(
                SpeechProvider.Claude,
                { transport, events ->
                    ClaudeVoiceClient(transport, "token", emptyList(), "en", events)
                },
                { it.startsWith("binary:") },
            ) { server, words ->
                server.onOpen()
                server.onText("""{"type":"TranscriptEndpoint","data":"$words"}""")
                server.onClosed(1000, "")
            },
        )
    }

    @Test
    fun `refinement streams into the panel and commits the final text once`() {
        lateinit var t: TestDictation
        // The panel as each piece arrives, while the cleanup is still running.
        val shown = mutableListOf<DictationState>()
        t =
            TestDictation(
                refine = { _, _, _, onText ->
                    onText("Hello")
                    shown += t.engine.state
                    onText("Hello there.")
                    shown += t.engine.state
                    "Hello there."
                }
            )
        t.dictate("hello there", SpeechProvider.ChatGpt)
        assertEquals(
            listOf(
                DictationState.Refining("hello there", "Hello"),
                DictationState.Refining("hello there", "Hello there."),
            ),
            shown,
        )
        assertEquals(listOf("Hello there."), t.commits)
    }

    @Test
    fun `partials stay interim until a final commits them, and Insert commits once`() {
        val t = TestDictation()
        t.engine.start(SpeechProvider.Claude)
        t.speak(SpeechEvent.Connected)
        t.speak(SpeechEvent.Partial("hello"))
        assertEquals(DictationState.Listening("", "hello", 0f), t.engine.state)
        t.speak(SpeechEvent.Final("hello there"))
        assertEquals(DictationState.Listening("hello there", "", 0f), t.engine.state)
        t.speak(SpeechEvent.Partial("friend"))
        assertEquals(DictationState.Listening("hello there", "friend", 0f), t.engine.state)
        // Only the tap that stops the listening reports it, for the stop vibration, and binds the
        // words to the field focused then.
        assertTrue(t.engine.insert())
        assertFalse(t.engine.insert())
        assertEquals(1, t.fieldsBound)
        t.speak(SpeechEvent.Final("friends"))
        t.speak(SpeechEvent.Completed)
        assertTrue(t.client.stopped)
        t.engine.cancel()
        assertEquals(listOf("hello there friends"), t.commits)
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
    fun `a commit reports the text, the listening time and the providers the dictation called`() {
        // Dictates "hello there" over 150 ms of audio, taps Insert refined with [cleanup] (plain
        // Insert when null) while the cleanup does [refine], and inserts again if that failed.
        fun inserted(cleanup: CleanupProvider?, refine: () -> String?): Inserted {
            val t = TestDictation(refine = { _, _, _, _ -> refine() })
            t.engine.start(SpeechProvider.Claude)
            t.hear(*ByteArray(3200)) // 100 ms of 16 kHz PCM16.
            t.hear(*ByteArray(1600))
            t.speak(SpeechEvent.Final("hello there"))
            if (cleanup == null) t.engine.insert() else t.engine.insertRefined(cleanup)
            t.speak(SpeechEvent.Completed)
            if (t.engine.state is DictationState.Failed) t.engine.insert()
            return t.inserted.single()
        }
        fun heard(vararg cleanups: CleanupProvider) =
            Inserted("hello there", 150, SpeechProvider.Claude, cleanups.toList())
        assertEquals(heard(), inserted(null) { "unused" })
        assertEquals(
            Inserted("Hello there.", 150, SpeechProvider.Claude, listOf(CleanupProvider.ChatGpt)),
            inserted(CleanupProvider.ChatGpt) { "Hello there." },
        )
        // A cleanup that failed was still called, though the transcript went in as heard.
        assertEquals(
            heard(CleanupProvider.Claude),
            inserted(CleanupProvider.Claude) { throw IOException("offline") },
        )
        // A profile that does no cleanup calls no provider.
        assertEquals(heard(), inserted(CleanupProvider.ChatGpt) { null })
    }

    @Test
    fun `words heard after a resume wait for the paused stream, then reach a stream before Insert`() {
        // Insert while listening again, or after a second pause.
        for (pauseAgain in listOf(false, true)) {
            val t = TestDictation()
            t.engine.start(SpeechProvider.Claude)
            t.speak(SpeechEvent.Final("before"))
            t.engine.pause()
            assertTrue(t.clients[0].stopped)
            assertEquals(DictationState.Listening("before", "", 0f, paused = true), t.engine.state)
            t.engine.resume()
            t.hear(7)
            assertEquals(1, t.clients.size)
            if (pauseAgain) t.engine.pause() else t.engine.insert()
            t.clients[0].events(SpeechEvent.Final("pause"))
            t.clients[0].events(SpeechEvent.Completed)
            assertEquals(listOf(listOf<Byte>(7)), t.clients[1].heard)
            assertTrue(t.clients[1].stopped)
            if (pauseAgain) t.engine.insert()
            assertEquals(emptyList<String>(), t.commits)
            t.clients[1].events(SpeechEvent.Final("after"))
            t.clients[1].events(SpeechEvent.Completed)
            assertEquals(listOf("before pause after"), t.commits)
        }
    }

    @Test
    fun `media stays paused from the tap through a pause until Insert, Cancel or a failure`() {
        fun media(end: TestDictation.() -> Unit) =
            TestDictation().apply {
                engine.start(SpeechProvider.Claude)
                end()
            }
        val canceled = media { engine.cancel() }
        assertTrue(canceled.client.cancelled)
        val inserted = media {
            engine.pause()
            engine.resume()
            engine.pause()
            assertEquals(listOf("pause"), media)
            engine.insert()
        }
        val failed = media { speak(SpeechEvent.Failed(ProviderFailureKind.Authentication)) }
        assertEquals(
            List(3) { listOf("pause", "resume") },
            listOf(inserted, canceled, failed).map { it.media },
        )
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
        assertEquals(listOf(listOf<Byte>(7)), t.clients[1].heard)
        assertFalse(t.clients[1].stopped)
    }

    @Test
    fun `words heard after a resume that no stream took are sent on Insert, or the dictation fails`() {
        fun pausedWithUnsentWords() =
            TestDictation().apply {
                engine.start(SpeechProvider.Claude)
                speak(SpeechEvent.Connected)
                speak(SpeechEvent.Final("before"))
                engine.pause()
                engine.resume()
                hear(7)
                engine.pause()
                connectFailure = IOException("offline")
                speak(SpeechEvent.Completed)
            }
        // Back online, Insert opens a stream for them.
        val online = pausedWithUnsentWords()
        online.connectFailure = null
        online.engine.insert()
        assertEquals(listOf(listOf<Byte>(7)), online.clients[1].heard)
        assertTrue(online.clients[1].stopped)
        online.speak(SpeechEvent.Final("after"))
        online.speak(SpeechEvent.Completed)
        assertEquals(listOf("before after"), online.commits)

        // Still offline, Insert fails rather than put the words in without them.
        val offline = pausedWithUnsentWords()
        offline.engine.insert()
        assertEquals(
            DictationState.Failed(
                FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Network),
                "Could not connect to the speech provider",
                "before",
            ),
            offline.engine.state,
        )
        assertEquals(emptyList<String>(), offline.commits)
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
        assertEquals(listOf(listOf<Byte>(1)), t.clients[0].heard)
        t.clients[0].events(SpeechEvent.Completed)
        tasks.runNext() // The next stream.
        assertEquals(listOf(listOf<Byte>(2)), t.clients[1].heard)
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
        assertTrue(t.dictate("never inserted").commits.isEmpty())
    }

    @Test
    fun `failed commit keeps transcript available for retry`() {
        val t = TestDictation()
        t.fieldTakesText = false
        t.dictate("keep this") { insert() }
        assertEquals("keep this", (t.engine.state as DictationState.Failed).transcript)
        t.fieldTakesText = true
        t.engine.insert()
        assertEquals(listOf("keep this", "keep this"), t.commits)
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
        t.dictate("save me")
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
    fun `ChatGPT re-transcribes up to 80 s of the recording, then cleans up, falling back to the stream`() {
        val uploads = mutableListOf<ByteArray>()
        // Ends with [end] after [audio], the second pass answering as [batch] does, if it is on.
        fun dictated(
            batch: (() -> String)?,
            vararg audio: ByteArray,
            end: DictationEngine.() -> Unit,
        ): String {
            val t =
                TestDictation(
                    refine = { _, raw, _, _ -> "clean: $raw" },
                    transcribe =
                        batch?.let {
                            { pcm, _ ->
                                uploads.add(pcm)
                                it()
                            }
                        },
                )
            t.engine.start(SpeechProvider.ChatGpt)
            t.speak(SpeechEvent.Connected)
            audio.forEach { t.hear(*it) }
            t.speak(SpeechEvent.Final("streamed words"))
            t.engine.end()
            t.speak(SpeechEvent.Completed)
            return t.commits.single()
        }
        val heard = arrayOf(byteArrayOf(1, 2), byteArrayOf(3, 4))
        val second = ByteArray(32_000) // One second of 16 kHz mono PCM16.
        assertEquals(
            listOf(
                "Hello there, friend.",
                "clean: streamed words",
                "clean: streamed words",
                "Batch text.",
                "streamed words",
            ),
            listOf(
                dictated({ "Hello there, friend." }, *heard) { insertRefined(null) },
                dictated({ error("HTTP 500") }, *heard) { insertRefined(CleanupProvider.Claude) },
                // With the extra pass off it only cleans up.
                dictated(null, *heard) { insertRefined(CleanupProvider.ChatGpt) },
                // A plain Insert takes the second pass too, up to 80 s.
                dictated({ "Batch text." }, *Array(80) { second }) { insert() },
                dictated({ "Batch text." }, *Array(81) { second }) { insert() },
            ),
        )
        assertEquals(listOf(4, 4, 2_560_000), uploads.map { it.size })
        assertEquals(List(2) { listOf<Byte>(1, 2, 3, 4) }, uploads.take(2).map { it.toList() })
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

    /**
     * Taps Insert refined with a Claude cleanup whose request the server holds for five seconds,
     * then does [interrupt] once the request is in flight; its worker must end within two.
     */
    private fun interruptCleanup(interrupt: DictationEngine.() -> Unit) =
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().headersDelay(5, TimeUnit.SECONDS).build())
            server.start()
            val workers = mutableListOf<Thread>()
            val t =
                TestDictation(
                    refine = { _, raw, cancellation, _ ->
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
                    },
                    executor = Executor { task -> Thread(task).also(workers::add).start() },
                )
            t.engine.start(SpeechProvider.Claude)
            workers.forEach(Thread::join) // The microphone and the connection.
            t.speak(SpeechEvent.Final("as heard"))
            t.engine.insertRefined(CleanupProvider.Claude)
            t.speak(SpeechEvent.Completed)
            server.takeRequest()
            t.engine.interrupt()
            workers.last().join(2_000)
            assertFalse(workers.last().isAlive)
            t
        }

    @Test
    fun `cancel or Insert during a cleanup stops its request, and Insert puts in the words as heard`() {
        assertEquals(emptyList<String>(), interruptCleanup { cancel() }.commits)
        // The cleanup still counts as called.
        assertEquals(
            listOf(Inserted("as heard", 0, SpeechProvider.Claude, listOf(CleanupProvider.Claude))),
            interruptCleanup { insert() }.inserted,
        )
    }

    /** Inserts with the "sign off" phrase, the cleanup answering as [refine] does its input. */
    private fun signOff(refine: (String, (String) -> Unit) -> String) =
        TestDictation(
            refine = { _, input, _, onText -> refine(input, onText) },
            replacements = { listOf(Replacement("sign off", "Regards,\nEfox")) },
        )

    @Test
    fun `both Inserts replace spoken phrases, and refinement keeps snippets in placeholders`() {
        val plain = signOff { _, _ ->
            error("plain Insert never cleans up")
        }
            .dictate("thanks sign off") { insert() }
        val sent = mutableListOf<String>()
        val refined = signOff { input, onText ->
            sent.add(input)
            onText("Thanks, SPEECHER_BINDING_0.")
            "Thanks, SPEECHER_BINDING_0."
        }
            .dictate("thanks sign off")
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
        val alone = signOff { _, _ -> error("nothing to clean up") }.dictate("sign off")
        val garbled = signOff { _, _ ->
            "Thanks, speecher binding zero."
        }
            .dictate("thanks sign off")
        // Each read of the rules stamps a later time; the Insert after the failure keeps the first.
        var reads = 0
        val failed =
            TestDictation(
                    refine = { _, _, _, _ -> throw IOException() },
                    replacements = { listOf(Replacement("stamp", "${++reads}:00")) },
                )
                .dictate("noted at stamp")
        failed.engine.insert()
        assertEquals(
            listOf("Regards,\nEfox", "thanks Regards,\nEfox", "noted at 1:00"),
            alone.commits + garbled.commits + failed.commits,
        )
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
        return t.dictate(heard)
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
        val invented = editSelection("add sign off", "Dinner") { _, _ -> "SPEECHER_BINDING_7" }
        assertEquals(
            listOf(
                FailureReason.SelectionNeedsCleanup,
                FailureReason.Cleanup(CleanupProvider.Claude, ProviderFailureKind.InvalidResult),
            ),
            listOf(noCleanup, invented).map { (it.engine.state as DictationState.Failed).reason },
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
