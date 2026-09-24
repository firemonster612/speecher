package app.speecher.android.dictation

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.inputmethod.InputConnection
import app.speecher.android.BuildConfig
import app.speecher.android.auth.TokenStore
import app.speecher.protocol.ClaudeVoiceClient
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CodexDictationClient
import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import app.speecher.protocol.preferredTranscript
import app.speecher.protocol.refineTranscript
import app.speecher.protocol.transcribeSpeech
import app.speecher.protocol.webSocketTransport
import java.io.ByteArrayOutputStream
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import okhttp3.OkHttpClient
import okhttp3.brotli.BrotliInterceptor

// Brotli lets the OAuth requests advertise the same compressions Claude Code's Axios client does
// (br included) and still decode the reply, so the sign-in traffic looks native to Cloudflare.
val sharedHttp = OkHttpClient.Builder().addInterceptor(BrotliInterceptor).build()
val sharedExecutor = Executors.newCachedThreadPool()

/** [transcribe] is the batch speech-to-text endpoint, for the providers that have one. */
private data class Endpoints(
    val speech: String,
    val refinement: String,
    val transcribe: String? = null,
)

/** [cleanup] is the LLM that tidies the transcript, or null for a plain Insert. */
private data class PendingInsert(val cleanup: Provider?)

class DictationEngine(
    private val capture: (() -> Boolean, (ByteArray, Float) -> Unit) -> Unit,
    private val stopCapture: () -> Unit,
    private val connect: (Provider, (SpeechEvent) -> Unit) -> SpeechClient,
    private val refine: (Provider, String) -> String,
    /** ChatGPT's batch re-transcription of the session's PCM16 audio; null skips that pass. */
    private val transcribe: ((ByteArray) -> String)?,
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
    private var pendingInsert: PendingInsert? = null
    private var failedRefinement: Provider? = null
    private var failedCommit: String? = null
    /** The provider streaming this dictation. */
    var sourceProvider = providerOrder.first()
        private set

    /** The session's audio, kept for the batch pass. A retried session appends to it. */
    private val recorded = ByteArrayOutputStream()
    @Volatile private var session = 0

    @Synchronized
    fun start(provider: Provider) {
        recorded.reset()
        startSession(provider, "")
    }

    private fun startSession(provider: Provider, priorTranscript: String) {
        cancelSession()
        val current = ++session
        finalText.clear()
        finalText.append(priorTranscript)
        interim = ""
        inserted = false
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
        publish(listening(0f))
    }

    @Synchronized
    fun cancel() {
        ++session
        cancelSession()
    }

    @Synchronized
    fun insert() {
        if (inserted || pendingInsert != null) return
        if (state is DictationState.Failed) {
            commitTranscript((state as DictationState.Failed).transcript)
            return
        }
        pendingInsert = PendingInsert(null)
        stop()
    }

    @Synchronized
    fun insertRefined(cleanup: Provider?) {
        if (inserted || pendingInsert != null) return
        pendingInsert = PendingInsert(cleanup)
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
                publish(listening(0f))
                executor.execute {
                    if (current != session || !recording) return@execute
                    try {
                        capture({ current == session && recording }) { audio, level ->
                            synchronized(this) {
                                if (current == session && recording) {
                                    client?.sendAudio(audio)
                                    if (
                                        transcribe != null &&
                                            sourceProvider.hasBatchTranscription &&
                                            recorded.size() + audio.size <= MAX_RECORDED_BYTES
                                    )
                                        recorded.write(audio)
                                    publish(listening(level))
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
                if (pendingInsert != null) finishPendingInsert() else publish(listening(0f))
            }
            is SpeechEvent.Failed ->
                if (pendingInsert != null && !event.authentication && transcript().isNotBlank())
                    finishPendingInsert()
                else
                    fail(
                        current,
                        if (event.authentication) FailureReason.SignedOut
                        else FailureReason.Network,
                        event.detail.ifEmpty { "Speech connection failed" },
                    )
        }
    }

    private fun finishPendingInsert() {
        val pending = pendingInsert ?: return
        pendingInsert = null
        insertBest(pending.cleanup)
    }

    /**
     * Both Insert buttons: ChatGPT re-transcribes the whole recording for accuracy when the extra
     * pass is on, then [cleanup], if any, tidies the text. A failed or truncated batch pass falls
     * back to the streamed transcript.
     */
    private fun insertBest(cleanup: Provider?) {
        val streamed = transcript()
        val audio = recorded.toByteArray()
        fun finish(text: String) =
            if (cleanup != null) refineTranscript(cleanup, text) else commitTranscript(text)
        if (transcribe == null || !sourceProvider.hasBatchTranscription || audio.isEmpty()) {
            finish(streamed)
            return
        }
        val current = session
        publish(DictationState.Refining(streamed))
        executor.execute {
            val batch =
                try {
                    transcribe(audio)
                } catch (_: Exception) {
                    null
                }
            synchronized(this) {
                if (current != session || inserted) return@execute
                finish(preferredTranscript(batch, streamed))
            }
        }
    }

    @Synchronized
    private fun fail(
        current: Int,
        reason: FailureReason,
        detail: String,
        raw: String = transcript(),
        commitFailed: Boolean = false,
    ) {
        if (current != session || inserted) return
        recording = false
        stopCapture()
        client?.cancel()
        pendingInsert = null
        publish(
            DictationState.Failed(
                reason,
                detail,
                raw,
                failedRefinement ?: sourceProvider,
                commitFailed,
            )
        )
    }

    private fun commitTranscript(text: String) {
        if (commit(text)) {
            inserted = true
            failedCommit = null
        } else {
            failedCommit = text
            fail(
                session,
                FailureReason.Provider,
                "Could not insert text",
                text,
                commitFailed = true,
            )
        }
    }

    private fun publishListening() =
        publish(listening((state as? DictationState.Listening)?.level ?: 0f))

    /** The current preview split into its committed and interim parts for the panel to render. */
    private fun listening(level: Float) =
        DictationState.Listening(finalText.toString(), interim, level)

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

// 90 s of 16 kHz mono PCM16: the batch endpoint transcribes no more than that.
private const val MAX_RECORDED_BYTES = 90 * 16000 * 2

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
    val endpoints =
        if (fake == null)
            mapOf(
                Provider.Claude to
                    Endpoints(
                        "wss://claude.ai/api/ws/speech_to_text/voice_stream",
                        "https://api.anthropic.com/v1",
                    ),
                Provider.ChatGpt to
                    Endpoints(
                        "wss://chatgpt.com/backend-api/dictation/stream",
                        "https://chatgpt.com/backend-api/codex",
                        "https://chatgpt.com/backend-api/transcribe",
                    ),
            )
        else
            mapOf(
                Provider.Claude to
                    Endpoints("$fake/api/ws/speech_to_text/voice_stream", "$fake/v1"),
                Provider.ChatGpt to
                    Endpoints(
                        "$fake/backend-api/dictation/stream",
                        "$fake/v1",
                        "$fake/backend-api/transcribe",
                    ),
            )
    val main = Handler(Looper.getMainLooper())
    fun token(value: Provider) = store.validTokens(value.oauth, http) ?: throw SignInRequired()
    return DictationEngine(
        microphone::capture,
        microphone::stop,
        { selected, events ->
            val access = token(selected).accessToken
            if (selected == Provider.Claude)
                ClaudeVoiceClient(
                    webSocketTransport(endpoints.getValue(selected).speech),
                    access,
                    settings.vocabulary,
                    events,
                    endpoints.getValue(selected).speech,
                )
            else
                CodexDictationClient(
                    webSocketTransport(endpoints.getValue(selected).speech),
                    access,
                    events,
                    endpoints.getValue(selected).speech,
                )
        },
        { selected, raw ->
            val context =
                refinementContext(settings, ActiveDictation.target) { length ->
                    connection()?.getSurroundingText(length, length, 0)?.let {
                        // A selection made backwards reports its start after its end.
                        val (start, end) = listOf(it.selectionStart, it.selectionEnd).sorted()
                        nearbyText(it.text, start, end, it.offset)
                    }
                }
            val choice = settings.refinement(selected)
            // A profile set to no cleanup inserts the transcript as heard, as the desktop does.
            if (context.style == CleanupStrength.None) raw
            else
                refineTranscript(
                    http,
                    selected.oauth,
                    token(selected),
                    raw,
                    settings.vocabulary,
                    choice.model,
                    choice.effort,
                    context,
                    endpoints.getValue(selected).refinement,
                )
        },
        if (settings.transcribePassEnabled)
            { pcm ->
                transcribeSpeech(
                    token(Provider.ChatGpt).accessToken,
                    pcm,
                    endpoints.getValue(Provider.ChatGpt).transcribe!!,
                )
            }
        else null,
        { text ->
            val committed = connection()?.commitText(text, 1) == true
            if (committed) main.post(onInserted)
            committed
        },
        sharedExecutor,
        { next -> main.post { onState(next) } },
    )
}
