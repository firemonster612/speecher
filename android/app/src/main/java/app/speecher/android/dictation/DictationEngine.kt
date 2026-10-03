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
import app.speecher.protocol.modelSupportsUltrafast
import app.speecher.protocol.preferredTranscript
import app.speecher.protocol.refineTranscript
import app.speecher.protocol.speechTerms
import app.speecher.protocol.transcribeSpeech
import app.speecher.protocol.webSocketTransport
import java.io.ByteArrayOutputStream
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import okhttp3.OkHttpClient
import okhttp3.brotli.BrotliInterceptor

// Brotli lets the OAuth requests advertise the same compressions Claude Code's Axios client does
// (br included) and still decode the reply, so the sign-in traffic looks native to Cloudflare.
val sharedHttp = OkHttpClient.Builder().addInterceptor(BrotliInterceptor).build()
val sharedExecutor = Executors.newCachedThreadPool()

/**
 * Whether fast mode is still worth asking each provider for. Cleared for the rest of the process
 * once a fast request fails and the standard-speed retry succeeds, as the desktop does.
 */
private val fastModeAvailable = Provider.entries.associateWith { AtomicBoolean(true) }

/** The same for ChatGPT's ultrafast tier, so its refusal leaves Fast alone. */
private val ultrafastAvailable = AtomicBoolean(true)

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
    /** Refines the raw transcript, reporting the refined text so far as it streams in. */
    private val refine: (Provider, String, (String) -> Unit) -> String,
    /** ChatGPT's batch re-transcription of the session's PCM16 audio; null skips that pass. */
    private val transcribe: ((ByteArray) -> String)?,
    private val commit: (String) -> Boolean,
    private val executor: Executor,
    private val onState: (DictationState) -> Unit,
    /** Waits out a reconnect's backoff on the executor thread. */
    private val pause: (Long) -> Unit = Thread::sleep,
) : AutoCloseable {
    @Volatile
    var state: DictationState = DictationState.Listening()
        private set

    private var client: SpeechClient? = null
    /**
     * Audio captured while no client is open, at the tap or during a reconnect, handed to the next
     * client [connect] returns.
     */
    private val unsent = mutableListOf<ByteArray>()
    /**
     * Identifies the current stream within the session, so a replaced stream's events are dropped.
     */
    private var attempt = 0
    /**
     * Whether this dictation has had a live stream, so losing one, or failing to reopen it, is a
     * mid-dictation drop worth reconnecting rather than a failure to connect at all.
     */
    private var streamed = false
    /** Set while a dropped stream reopens, for the panel's hint. A session rollover shows none. */
    private var reconnecting = false
    private var reconnectsLeft = 0
    private val finalText = StringBuilder()
    private var interim = ""
    @Volatile private var recording = false
    /** Paused by the person: the microphone is off and the stream finishing its words. */
    private var paused = false
    /**
     * The paused stream has not ended yet. Audio heard after a resume waits in [heardAfterResume]
     * until it has, since its final words would be dropped by opening the next stream any sooner.
     */
    @Volatile private var finishingPause = false
    private val heardAfterResume = mutableListOf<ByteArray>()
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
        paused = false
        finishingPause = false
        streamed = false
        reconnecting = false
        reconnectsLeft = RECONNECT_BACKOFF_MS.size
        // Listening from the tap: the microphone starts now and the clients hold audio until their
        // socket is up, so words spoken while it connects are sent, not lost.
        publish(listening(0f))
        executor.execute { captureAudio(current) }
        openStream(current)
    }

    private fun openStream(current: Int, delayMillis: Long = 0) {
        val opening = ++attempt
        val provider = sourceProvider
        executor.execute {
            if (delayMillis > 0) pause(delayMillis)
            if (synchronized(this) { !isCurrent(current, opening) }) return@execute
            try {
                val opened = connect(provider) { event -> onSpeech(current, opening, event) }
                synchronized(this) {
                    if (isCurrent(current, opening)) {
                        client = opened
                        unsent.forEach(opened::sendAudio)
                        unsent.clear()
                        // Paused while it connected, or opened only for what a resume heard: it
                        // ends as it opens, whether or not the microphone is back on.
                        if (!recording || finishingPause) opened.stop()
                    } else opened.cancel()
                }
            } catch (_: SignInRequired) {
                fail(current, FailureReason.SignedOut, "Sign in to continue")
            } catch (e: SpokenLanguageUnsupported) {
                fail(current, FailureReason.SpokenLanguage, e.message.orEmpty())
            } catch (_: Exception) {
                synchronized(this) {
                    if (!isCurrent(current, opening)) return@synchronized
                    // A stream opened while a pause finishes, after this dictation has streamed:
                    // a blip ends it like a paused stream that failed, and the words stay.
                    if (finishingPause && streamed) pausedStreamEnded(current, opened = false)
                    else
                        streamFailed(
                            current,
                            retryable = true,
                            FailureReason.Network,
                            "Could not connect to the speech provider",
                        )
                }
            }
        }
    }

    /** Whether [opening] is still the stream this dictation is waiting on. */
    private fun isCurrent(current: Int, opening: Int) =
        current == session &&
            opening == attempt &&
            !inserted &&
            state !is DictationState.Refining &&
            state !is DictationState.Failed

    @Synchronized
    fun stop() {
        if (paused) {
            paused = false
            // An Insert while paused takes what was heard once the paused stream has ended.
            if (!finishingPause) finishPendingInsert() else publish(listening(0f))
            return
        }
        if (!recording) return
        recording = false
        stopCapture()
        // A stream finishing a pause was already told; its end opens the one for what came after.
        if (!finishingPause) client?.stop()
        publish(listening(0f))
    }

    /**
     * Turns the microphone off and lets the stream finish the words already spoken; they stay in
     * the transcript. Resume carries on in a new stream.
     */
    @Synchronized
    fun pause() {
        if (!recording || state !is DictationState.Listening) return
        recording = false
        paused = true
        // A stream still connecting is stopped as it opens, and ends like any other. Paused again
        // before the last pause's stream ended, that stream is still finishing.
        val alreadyFinishing = finishingPause
        finishingPause = true
        stopCapture()
        if (!alreadyFinishing) client?.stop()
        publish(listening(0f))
    }

    @Synchronized
    fun resume() {
        if (!paused) return
        paused = false
        recording = true
        val current = session
        publish(listening(0f))
        executor.execute { captureAudio(current) }
        if (!finishingPause) reopen(current)
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

    private fun captureAudio(current: Int) {
        try {
            capture({ current == session && recording }) { audio, level ->
                synchronized(this) {
                    if (current == session && recording) {
                        if (finishingPause) heardAfterResume.add(audio)
                        else client?.sendAudio(audio) ?: unsent.add(audio)
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
            fail(current, FailureReason.MicrophoneDenied, "Grant microphone permission in Speecher")
        } catch (_: Exception) {
            fail(current, FailureReason.MicrophoneUnavailable, "Microphone unavailable")
        }
    }

    private fun refineTranscript(provider: Provider, raw: String) {
        val current = session
        failedRefinement = provider
        publish(DictationState.Refining(raw))
        executor.execute {
            try {
                val text =
                    refine(provider, raw) { refined ->
                        synchronized(this) {
                            if (current == session && state is DictationState.Refining)
                                publish(DictationState.Refining(raw, refined))
                        }
                    }
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
    private fun onSpeech(current: Int, opening: Int, event: SpeechEvent) {
        if (!isCurrent(current, opening)) return
        when (event) {
            SpeechEvent.Connected -> {
                streamed = true
                if (reconnecting) {
                    reconnecting = false
                    publishListening()
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
            SpeechEvent.Completed ->
                if (finishingPause) pausedStreamEnded(current)
                else if (pendingInsert != null) finishPendingInsert()
                // Still recording, so the provider ended the session itself, as ChatGPT does at its
                // session limit. That is not an error: carry on in a new session.
                else if (recording) reopen(current) else publish(listening(0f))
            // A paused stream that fails to finish, closed during a long pause, has still ended.
            is SpeechEvent.Failed ->
                if (finishingPause) pausedStreamEnded(current)
                else
                    streamFailed(
                        current,
                        event.retryable,
                        if (event.authentication) FailureReason.SignedOut
                        else FailureReason.Network,
                        event.detail.ifEmpty { "Speech connection failed" },
                    )
        }
    }

    /**
     * A stream failed, or could not open. A pending Insert takes what was heard; a retryable loss
     * mid-dictation reconnects while the budget lasts, as on the desktop; anything else fails.
     */
    private fun streamFailed(
        current: Int,
        retryable: Boolean,
        reason: FailureReason,
        detail: String,
    ) {
        if (pendingInsert != null && reason != FailureReason.SignedOut && transcript().isNotBlank())
            finishPendingInsert()
        else if (retryable && streamed && recording && reconnectsLeft > 0) {
            val backoff = RECONNECT_BACKOFF_MS[RECONNECT_BACKOFF_MS.size - reconnectsLeft]
            reconnectsLeft--
            reconnecting = true
            reopen(current, backoff)
        } else fail(current, reason, detail)
    }

    /**
     * Replaces the ended stream with a new one after [delayMillis]. The microphone keeps running
     * into [unsent] meanwhile. The old stream will never finalise the words in progress, so they
     * are committed now.
     */
    private fun reopen(current: Int, delayMillis: Long = 0) {
        if (interim.isNotEmpty()) {
            if (finalText.isNotEmpty()) finalText.append(' ')
            finalText.append(interim)
            interim = ""
        }
        client?.cancel()
        client = null
        publishListening()
        openStream(current, delayMillis)
    }

    /**
     * The paused stream ended. What was heard after a resume goes to the next stream: one that
     * keeps listening, or, with the microphone off again, one that ends as it opens and is waited
     * on like the paused one. Only then does an Insert go ahead. A stream that never [opened]
     * leaves that audio in [unsent] for the next resume rather than retrying while paused.
     */
    private fun pausedStreamEnded(current: Int, opened: Boolean = true) {
        finishingPause = false
        client = null
        unsent.addAll(heardAfterResume)
        heardAfterResume.clear()
        if (opened && !recording && unsent.isNotEmpty()) {
            finishingPause = true
            reopen(current)
        } else if (pendingInsert != null) finishPendingInsert()
        else if (recording) reopen(current) else publishListening()
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
        // The microphone and the connection start together and can both fail; the first failure
        // is the one shown. Only a failed commit replaces a failure, with its retry.
        if (state is DictationState.Failed && !commitFailed) return
        cancelSession()
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
        DictationState.Listening(finalText.toString(), interim, level, reconnecting, paused)

    private fun transcript(): String =
        if (finalText.isEmpty()) interim
        else if (interim.isEmpty()) finalText.toString() else "$finalText $interim"

    private fun publish(next: DictationState) {
        state = next
        onState(next)
    }

    private fun cancelSession() {
        recording = false
        paused = false
        finishingPause = false
        stopCapture()
        client?.cancel()
        client = null
        unsent.clear()
        heardAfterResume.clear()
    }

    override fun close() {
        cancel()
    }
}

/**
 * The wait before each reconnect after a dropped stream, so a Wi-Fi to mobile handoff can settle.
 * Its length is the desktop's budget of reconnects per dictation; session rollovers cost none.
 */
private val RECONNECT_BACKOFF_MS = longArrayOf(1_000, 3_000)

// 90 s of 16 kHz mono PCM16: the batch endpoint transcribes no more than that.
private const val MAX_RECORDED_BYTES = 90 * 16000 * 2

class SignInRequired : Exception()

/** The provider can't listen for the saved spoken language; [message] says so for the panel. */
class SpokenLanguageUnsupported(message: String) : Exception(message)

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
        { selected, onEvent ->
            spokenLanguageMismatch(selected, settings.spokenLanguage)?.let {
                throw SpokenLanguageUnsupported(it)
            }
            val access = token(selected).accessToken
            val events = { event: SpeechEvent ->
                if (event == SpeechEvent.Connected || event is SpeechEvent.Final)
                    store.clearSessionEnded(selected.oauth)
                onEvent(event)
            }
            if (selected == Provider.Claude)
                ClaudeVoiceClient(
                    webSocketTransport(endpoints.getValue(selected).speech),
                    access,
                    speechTerms(
                        settings.vocabularyFor(writingProfile(settings, ActiveDictation.target))
                    ),
                    settings.spokenLanguage,
                    events,
                    endpoints.getValue(selected).speech,
                )
            else
                CodexDictationClient(
                    webSocketTransport(endpoints.getValue(selected).speech),
                    access,
                    settings.spokenLanguage,
                    events,
                    endpoints.getValue(selected).speech,
                )
        },
        { selected, raw, onRefined ->
            val context =
                refinementContext(
                    settings,
                    ActiveDictation.target,
                    ActiveDictation.screen,
                    ActiveDictation.screenshotJpeg,
                ) { length ->
                    connection()?.getSurroundingText(length, length, 0)?.let {
                        // A selection made backwards reports its start after its end.
                        val (start, end) = listOf(it.selectionStart, it.selectionEnd).sorted()
                        nearbyText(it.text, start, end, it.offset)
                    }
                }
            val choice = settings.refinement(selected)
            val ultrafast =
                selected == Provider.ChatGpt &&
                    settings.chatGptSpeed == OpenAiSpeed.Ultrafast &&
                    modelSupportsUltrafast(choice.model)
            // A profile set to no cleanup inserts the transcript as heard, as the desktop does.
            if (context.style == CleanupStrength.None) raw
            else
                refineTranscript(
                        http,
                        selected.oauth,
                        token(selected),
                        raw,
                        settings.vocabularyFor(context.profile),
                        choice.model,
                        choice.effort,
                        context,
                        endpoints.getValue(selected).refinement,
                        (if (ultrafast) ultrafastAvailable
                            else fastModeAvailable.getValue(selected))
                            .takeIf { settings.fastMode(selected) },
                        ultrafast,
                        onRefined,
                    )
                    .also { store.clearSessionEnded(selected.oauth) }
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
