package app.speecher.android.dictation

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.inputmethod.InputConnection
import app.speecher.android.BuildConfig
import app.speecher.android.auth.TokenStore
import app.speecher.protocol.ClaudeVoiceClient
import app.speecher.protocol.CodexDictationClient
import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import app.speecher.protocol.refineTranscript
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import okhttp3.OkHttpClient

val sharedHttp = OkHttpClient()
val sharedExecutor = Executors.newCachedThreadPool()

class DictationEngine(
    private val capture: (() -> Boolean, (ByteArray, Float) -> Unit) -> Unit,
    private val stopCapture: () -> Unit,
    private val connect: (Provider, (SpeechEvent) -> Unit) -> SpeechClient,
    private val refine: (Provider, String) -> String,
    private val commit: (String) -> Boolean,
    private val executor: Executor,
    private val onState: (DictationState) -> Unit,
) : AutoCloseable {
    @Volatile
    var state: DictationState = DictationState.Connecting
        private set

    private var client: SpeechClient? = null
    private val finalText = StringBuilder()
    private var interim = ""
    @Volatile private var recording = false
    private var inserted = false
    private var pendingInsert: Provider? = null
    private var insertPending = false
    private var failedRefinement: Provider? = null
    private var failedCommit: String? = null
    private var sourceProvider = Provider.Claude
    @Volatile private var session = 0

    @Synchronized fun start(provider: Provider) = startSession(provider, "")

    private fun startSession(provider: Provider, priorTranscript: String) {
        cancelSession()
        val current = ++session
        finalText.clear()
        finalText.append(priorTranscript)
        interim = ""
        inserted = false
        insertPending = false
        pendingInsert = null
        failedRefinement = null
        failedCommit = null
        sourceProvider = provider
        recording = true
        publish(DictationState.Connecting)
        executor.execute {
            try {
                val opened = connect(provider) { event -> onSpeech(current, event) }
                synchronized(this) {
                    if (current == session) {
                        client = opened
                        if (!recording) opened.stop()
                    } else opened.cancel()
                }
            } catch (_: SignInRequired) {
                fail(current, FailureReason.SignedOut, "Sign in to continue")
            } catch (_: Exception) {
                fail(current, FailureReason.Network, "Could not connect to the speech provider")
            }
        }
    }

    @Synchronized
    fun stop() {
        if (!recording) return
        recording = false
        stopCapture()
        client?.stop()
        publish(DictationState.Listening(transcript(), 0f))
    }

    @Synchronized
    fun cancel() {
        ++session
        cancelSession()
    }

    @Synchronized
    fun insert() {
        if (inserted || insertPending) return
        if (state is DictationState.Failed) {
            commitTranscript((state as DictationState.Failed).transcript)
            return
        }
        insertPending = true
        stop()
    }

    @Synchronized
    fun insertRefined(provider: Provider) {
        if (inserted || insertPending) return
        insertPending = true
        pendingInsert = provider
        stop()
    }

    @Synchronized
    fun retry() {
        val failed = state as? DictationState.Failed ?: return
        failedCommit?.let {
            commitTranscript(it)
            return
        }
        val provider = failedRefinement
        if (provider != null) refineTranscript(provider, failed.transcript)
        else startSession(sourceProvider, failed.transcript)
    }

    private fun refineTranscript(provider: Provider, raw: String) {
        val current = session
        failedRefinement = provider
        publish(DictationState.Refining(raw))
        executor.execute {
            try {
                val text = refine(provider, raw)
                synchronized(this) {
                    if (current != session || inserted) return@execute
                    commitTranscript(text)
                }
            } catch (_: SignInRequired) {
                fail(current, FailureReason.SignedOut, "Sign in to continue")
            } catch (_: Exception) {
                fail(current, FailureReason.Provider, "Could not refine the transcript", raw)
            }
        }
    }

    @Synchronized
    private fun onSpeech(current: Int, event: SpeechEvent) {
        if (
            current != session ||
                inserted ||
                state is DictationState.Refining ||
                state is DictationState.Failed
        )
            return
        when (event) {
            SpeechEvent.Connected -> {
                publish(DictationState.Listening(transcript(), 0f))
                executor.execute {
                    if (current != session || !recording) return@execute
                    try {
                        capture({ current == session && recording }) { audio, level ->
                            synchronized(this) {
                                if (current == session && recording) {
                                    client?.sendAudio(audio)
                                    publish(DictationState.Listening(transcript(), level))
                                }
                            }
                        }
                    } catch (_: SecurityException) {
                        fail(
                            current,
                            FailureReason.MicrophoneDenied,
                            "Grant microphone permission in Speecher",
                        )
                    } catch (_: Exception) {
                        fail(current, FailureReason.Provider, "Microphone unavailable")
                    }
                }
            }
            is SpeechEvent.Partial -> {
                interim = event.text
                publishListening()
            }
            is SpeechEvent.Final -> {
                if (finalText.isNotEmpty()) finalText.append(' ')
                finalText.append(event.text)
                interim = ""
                publishListening()
            }
            SpeechEvent.Completed -> {
                if (insertPending) {
                    insertPending = false
                    val provider = pendingInsert
                    pendingInsert = null
                    if (provider == null) commitTranscript(transcript())
                    else refineTranscript(provider, transcript())
                } else publish(DictationState.Listening(transcript(), 0f))
            }
            is SpeechEvent.Failed ->
                if (insertPending && !event.authentication && transcript().isNotBlank()) {
                    insertPending = false
                    val provider = pendingInsert
                    pendingInsert = null
                    if (provider == null) commitTranscript(transcript())
                    else refineTranscript(provider, transcript())
                } else
                    fail(
                        current,
                        if (event.authentication) FailureReason.SignedOut
                        else FailureReason.Network,
                        "Speech connection failed",
                    )
        }
    }

    @Synchronized
    private fun fail(
        current: Int,
        reason: FailureReason,
        detail: String,
        raw: String = transcript(),
    ) {
        if (current != session || inserted) return
        recording = false
        stopCapture()
        client?.cancel()
        insertPending = false
        publish(DictationState.Failed(reason, detail, raw, failedRefinement ?: sourceProvider))
    }

    private fun commitTranscript(text: String) {
        if (commit(text)) {
            inserted = true
            failedCommit = null
        } else {
            failedCommit = text
            fail(session, FailureReason.Provider, "Could not insert text", text)
        }
    }

    private fun publishListening() =
        publish(
            DictationState.Listening(
                transcript(),
                (state as? DictationState.Listening)?.level ?: 0f,
            )
        )

    private fun transcript(): String =
        if (finalText.isEmpty()) interim
        else if (interim.isEmpty()) finalText.toString() else "$finalText $interim"

    private fun publish(next: DictationState) {
        state = next
        onState(next)
    }

    private fun cancelSession() {
        recording = false
        stopCapture()
        client?.cancel()
        client = null
    }

    override fun close() {
        cancel()
    }
}

class SignInRequired : Exception()

/** Android wiring; call [DictationEngine.start] on the chip tap before the IME appears. */
fun createDictationEngine(
    context: Context,
    settings: SpeecherSettings,
    connection: () -> InputConnection?,
    onState: (DictationState) -> Unit,
    onInserted: () -> Unit = {},
): DictationEngine {
    val store = TokenStore(context)
    val http = sharedHttp
    val microphone = Microphone(context)
    val fake = BuildConfig.FAKE_SPEECH_BASE.takeIf(String::isNotEmpty)
    val main = Handler(Looper.getMainLooper())
    fun token(value: Provider) = store.validTokens(value.oauth, http) ?: throw SignInRequired()
    return DictationEngine(
        microphone::capture,
        microphone::stop,
        { selected, events ->
            val access = token(selected).accessToken
            if (selected == Provider.Claude)
                fake?.let {
                    ClaudeVoiceClient(
                        http,
                        access,
                        settings.vocabulary,
                        events,
                        "$it/api/ws/speech_to_text/voice_stream",
                    )
                } ?: ClaudeVoiceClient(http, access, settings.vocabulary, events)
            else
                fake?.let {
                    CodexDictationClient(http, access, events, "$it/backend-api/dictation/stream")
                } ?: CodexDictationClient(http, access, events)
        },
        { selected, raw ->
            if (fake == null)
                refineTranscript(http, selected.oauth, token(selected), raw, settings.vocabulary)
            else
                refineTranscript(
                    http,
                    selected.oauth,
                    token(selected),
                    raw,
                    settings.vocabulary,
                    "$fake/v1",
                )
        },
        { text ->
            val committed = connection()?.commitText(text, 1) == true
            if (committed) main.post(onInserted)
            committed
        },
        sharedExecutor,
        { next -> main.post { onState(next) } },
    )
}
