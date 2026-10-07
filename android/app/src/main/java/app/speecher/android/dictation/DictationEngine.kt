package app.speecher.android.dictation

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import android.text.format.DateFormat
import android.view.inputmethod.InputConnection
import app.speecher.android.BuildConfig
import app.speecher.android.auth.ApiKey
import app.speecher.android.auth.TokenStore
import app.speecher.protocol.Cancellation
import app.speecher.protocol.ClaudeVoiceClient
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CodexDictationClient
import app.speecher.protocol.EndpointSpeechClient
import app.speecher.protocol.ProviderFailure
import app.speecher.protocol.ProviderFailureKind
import app.speecher.protocol.RefinementContext
import app.speecher.protocol.ReplacedTranscript
import app.speecher.protocol.Replacement
import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import app.speecher.protocol.failureKind
import app.speecher.protocol.modelSupportsUltrafast
import app.speecher.protocol.preferredTranscript
import app.speecher.protocol.refineTranscript
import app.speecher.protocol.refinementVocabulary
import app.speecher.protocol.replaceSpoken
import app.speecher.protocol.replacementAliases
import app.speecher.protocol.speechTerms
import app.speecher.protocol.transcribeSpeech
import app.speecher.protocol.webSocketTransport
import app.speecher.protocol.withLearnedCorrections
import app.speecher.protocol.withVariablesFilled
import java.io.ByteArrayOutputStream
import java.util.Date
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

/**
 * A commit that went through: the text, how long the microphone listened, the provider that
 * transcribed it and the cleanup providers this dictation called, in order, including any that
 * failed before the transcript went in as heard. [dictated] is what was said: the text, except for
 * a selection edit, whose words were the instructions the desktop counts.
 */
data class Inserted(
    val text: String,
    val audioMillis: Long,
    val speech: SpeechProvider,
    val cleanups: List<CleanupProvider>,
    val dictated: String = text,
)

/** [cleanup] is the LLM that tidies the transcript, or null for a plain Insert. */
private data class PendingInsert(val cleanup: CleanupProvider?)

class DictationEngine(
    private val capture: (() -> Boolean, (ByteArray, Float) -> Unit) -> Unit,
    private val stopCapture: () -> Unit,
    private val connect: (SpeechProvider, (SpeechEvent) -> Unit) -> SpeechClient,
    /**
     * Refines the raw transcript, reporting the refined text so far as it streams in, until the
     * [Cancellation] aborts it. Null when the profile does no cleanup, so the transcript goes in as
     * heard.
     */
    private val refine: (CleanupProvider, String, Cancellation, (String) -> Unit) -> String?,
    /**
     * ChatGPT's batch re-transcription of the session's PCM16 audio, until the [Cancellation]
     * aborts it; null skips that pass.
     */
    private val transcribe: ((ByteArray, Cancellation) -> String)?,
    private val commit: (String) -> Boolean,
    private val executor: Executor,
    private val onState: (DictationState) -> Unit,
    /** Waits out a reconnect's backoff on the executor thread. */
    private val pause: (Long) -> Unit = Thread::sleep,
    /**
     * Pauses other apps' media from the tap until Insert, Cancel or a failure stops the microphone,
     * when [resumeMedia] lets it play again. A pause keeps it paused: the person is still
     * mid-dictation.
     */
    private val pauseMedia: () -> Unit = {},
    private val resumeMedia: () -> Unit = {},
    /** The replacements an Insert applies to the words as heard, read once per dictation. */
    private val replacements: () -> List<Replacement> = { emptyList() },
    private val onCommitted: (Inserted) -> Unit = {},
    /**
     * The field's selected text, null when nothing is selected or the field is a password field.
     * Read when Insert refined is tapped, and again before the edit replaces it.
     */
    private val selection: () -> String? = { null },
    /**
     * Revises the selected text (the second argument) as the spoken instructions (the third) say,
     * until the [Cancellation] aborts it. Null when the profile does no cleanup.
     */
    private val editSelection: (CleanupProvider, String, String, Cancellation) -> String? =
        { _, _, _, _ ->
            null
        },
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
    private var failedRefinement: CleanupProvider? = null
    private var failedCommit: String? = null
    /** This dictation's replacement step, once an Insert ran it; see [replaced]. */
    private var prepared: ReplacedTranscript? = null
    /**
     * The selection Insert refined found, which this dictation edits instead of inserting its
     * words, as the desktop does with a selection. Nothing goes in unless the edit succeeds.
     */
    private var selected: String? = null
    /** The batch or refinement request in flight, which Cancel aborts. */
    private var request: Cancellation? = null
    /** The provider streaming this dictation. */
    var sourceProvider = providerOrder.first().speech
        private set

    /** The session's audio, kept for the batch pass. A retried session appends to it. */
    private val recorded = ByteArrayOutputStream()
    /** The dictation outgrew what the batch pass transcribes, so [recorded] is dropped. */
    private var recordedTooLong = false
    /** All the audio this dictation heard, retries included, pauses not. */
    private var heardBytes = 0L
    /** The cleanup providers this dictation called, retries included, each once. */
    private val cleanupsCalled = linkedSetOf<CleanupProvider>()
    @Volatile private var session = 0

    @Synchronized
    fun start(provider: SpeechProvider) {
        recorded.reset()
        recordedTooLong = false
        heardBytes = 0
        cleanupsCalled.clear()
        startSession(provider, "")
    }

    private fun startSession(provider: SpeechProvider, priorTranscript: String) {
        cancelSession()
        val current = ++session
        finalText.clear()
        finalText.append(priorTranscript)
        interim = ""
        inserted = false
        pendingInsert = null
        failedRefinement = null
        failedCommit = null
        prepared = null
        selected = null
        sourceProvider = provider
        recording = true
        paused = false
        finishingPause = false
        streamed = false
        reconnecting = false
        reconnectsLeft = RECONNECT_BACKOFF_MS.size
        pauseMedia()
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
            } catch (e: SpokenLanguageUnsupported) {
                fail(current, FailureReason.SpokenLanguage, e.message.orEmpty())
            } catch (e: Exception) {
                synchronized(this) {
                    if (!isCurrent(current, opening)) return@synchronized
                    val kind = failureKind(e)
                    // A stream opened while a pause finishes, after this dictation has streamed:
                    // a blip ends it like a paused stream that failed, and the words stay.
                    if (finishingPause && streamed && !kind.needsSignIn)
                        pausedStreamEnded(current, opened = false)
                    else
                        streamFailed(
                            current,
                            retryable = !kind.needsSignIn,
                            kind,
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

    /** Ends the listening, running or paused; false when there was none to end. */
    private fun stop(): Boolean {
        if (paused) {
            paused = false
            resumeMedia()
            // An Insert while paused takes what was heard once the paused stream has ended.
            if (!finishingPause) finishPendingInsert() else publish(listening(0f))
            return true
        }
        if (!recording) return false
        recording = false
        stopCapture()
        resumeMedia()
        // A stream finishing a pause was already told; its end opens the one for what came after.
        if (!finishingPause) client?.stop()
        publish(listening(0f))
        return true
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
        resumeMedia()
    }

    /**
     * Inserts what was heard once the stream has finished it. True when this stopped the listening,
     * the moment the desktop plays its stop sound; a repeated tap or a failure's Insert stops
     * nothing.
     */
    @Synchronized
    fun insert(): Boolean {
        if (inserted || pendingInsert != null) return false
        if (state is DictationState.Failed) {
            if (selected != null) return false
            val text = failedCommit
            if (text != null) commitTranscript(text)
            else commitHeard((state as DictationState.Failed).transcript)
            return false
        }
        // During the cleanup, Insert stops it and takes the words as heard, as the desktop's
        // Cancel refinement does. An edit's words are instructions, so it takes nothing there.
        val refining = state as? DictationState.Refining
        if (refining != null && refining.editsSelection) return false
        if (refining != null && !refining.transcribingAgain) {
            cancelSession()
            // The stopped cleanup was still called, as the desktop's record counts it.
            failedRefinement?.let { cleanupsCalled += it }
            commitHeard(refining.transcript)
            return false
        }
        pendingInsert = PendingInsert(null)
        return stop()
    }

    /** [insert] with [cleanup], if any, tidying the text first. */
    @Synchronized
    fun insertRefined(cleanup: CleanupProvider?): Boolean {
        if (inserted || pendingInsert != null) return false
        if (cleanup != null) selected = selection()
        pendingInsert = PendingInsert(cleanup)
        return stop()
    }

    /**
     * Repeats what failed. True when that starts listening again, the moment the desktop plays its
     * start sound; a repeated refinement or commit does not listen.
     */
    @Synchronized
    fun retry(): Boolean {
        val failed = state as? DictationState.Failed ?: return false
        failedCommit?.let {
            commitTranscript(it)
            return false
        }
        val provider = failedRefinement
        if (provider != null) {
            refineTranscript(provider, failed.transcript)
            return false
        }
        startSession(sourceProvider, failed.transcript)
        return true
    }

    private fun captureAudio(current: Int) {
        try {
            capture({ current == session && recording }) { audio, level ->
                synchronized(this) {
                    if (current == session && recording) {
                        if (finishingPause) heardAfterResume.add(audio)
                        else client?.sendAudio(audio) ?: unsent.add(audio)
                        heardBytes += audio.size
                        if (
                            transcribe != null &&
                                sourceProvider.hasBatchTranscription &&
                                !recordedTooLong
                        ) {
                            recorded.write(audio)
                            if (recorded.size() > MAX_RETRANSCRIBE_BYTES) {
                                recorded.reset()
                                recordedTooLong = true
                            }
                        }
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

    /**
     * Cleans up [raw] with each replacement's text held in a placeholder the model must keep, as on
     * the desktop. Words that are all spoken phrases need no model, and an answer that garbles or
     * invents a placeholder goes in as heard, with the replacements.
     */
    private fun refineTranscript(provider: CleanupProvider, raw: String) {
        val replaced = replaced(raw)
        val edited = selected
        if (edited == null && replaced.skipsRefinement) {
            commitTranscript(replaced.text)
            return
        }
        val current = session
        failedRefinement = provider
        val cancellation = Cancellation().also { request = it }
        publish(DictationState.Refining(raw, editsSelection = edited != null))
        executor.execute {
            try {
                // An edit streams the whole revised selection, so like the desktop it shows none.
                val text =
                    if (edited != null)
                        editSelection(provider, edited, replaced.refinementInput, cancellation)
                    else
                        refine(provider, replaced.refinementInput, cancellation) { refined ->
                            synchronized(this) {
                                if (current == session && state is DictationState.Refining)
                                    publish(DictationState.Refining(raw, replaced.preview(refined)))
                            }
                        }
                synchronized(this) {
                    // A cleanup cancelled as it finished, by Insert or a failure, never goes in.
                    if (current != session || request !== cancellation) return@execute
                    // Null when the profile does no cleanup, so no provider was called.
                    if (text != null) cleanupsCalled += provider
                    if (edited == null)
                        commitTranscript(text?.let(replaced::restore) ?: replaced.text)
                    else if (text == null)
                        fail(current, FailureReason.SelectionNeedsCleanup, "No cleanup", raw)
                    else
                        replaced.restoreEdit(text)?.let(::commitTranscript)
                            ?: fail(
                                current,
                                FailureReason.Cleanup(provider, ProviderFailureKind.InvalidResult),
                                "The refinement model returned an unusable selection edit",
                                raw,
                            )
                }
            } catch (e: Exception) {
                cleanupCalled(current, provider)
                fail(
                    current,
                    FailureReason.Cleanup(provider, failureKind(e)),
                    "Could not refine the transcript",
                    raw,
                )
            }
        }
    }

    /** [provider] was called and failed; it still counts as one this dictation used. */
    @Synchronized
    private fun cleanupCalled(current: Int, provider: CleanupProvider) {
        if (current == session) cleanupsCalled += provider
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
                        event.kind,
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
        kind: ProviderFailureKind,
        detail: String,
    ) {
        if (pendingInsert != null && !kind.needsSignIn && transcript().isNotBlank())
            finishPendingInsert()
        else if (retryable && streamed && recording && reconnectsLeft > 0) {
            val backoff = RECONNECT_BACKOFF_MS[RECONNECT_BACKOFF_MS.size - reconnectsLeft]
            reconnectsLeft--
            reconnecting = true
            reopen(current, backoff)
        } else fail(current, FailureReason.Speech(sourceProvider, kind), detail)
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
     * back to the streamed transcript, and a recording too long for it skips the pass.
     */
    private fun insertBest(cleanup: CleanupProvider?) {
        val streamed = transcript()
        val audio = recorded.toByteArray()
        // Nothing heard, as a server that transcribes after the stop can answer, is not cleaned up.
        fun finish(text: String) =
            if (cleanup != null && text.isNotBlank()) refineTranscript(cleanup, text)
            else commitHeard(text)
        if (transcribe == null || !sourceProvider.hasBatchTranscription || audio.isEmpty()) {
            finish(streamed)
            return
        }
        val current = session
        val cancellation = Cancellation().also { request = it }
        publish(DictationState.Refining(streamed, transcribingAgain = true))
        executor.execute {
            val batch =
                try {
                    transcribe(audio, cancellation)
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
    ) {
        if (current != session || inserted) return
        // The microphone and the connection start together and can both fail; the first failure
        // is the one shown. Only a failed commit replaces a failure, with its retry.
        if (
            state is DictationState.Failed &&
                reason != FailureReason.Commit &&
                reason != FailureReason.SelectionChanged
        )
            return
        cancelSession()
        resumeMedia()
        pendingInsert = null
        publish(DictationState.Failed(reason, detail, raw, editsSelection = selected != null))
    }

    /** Commits words as heard, each spoken phrase replaced by its text. */
    private fun commitHeard(raw: String) = commitTranscript(replaced(raw).text)

    /**
     * The replacement step for [raw], run once per dictation: a retried cleanup and the Insert
     * after a failed one reuse it, so they insert the same {date} and {time}, as the desktop's
     * single snapshot does. Only the first Insert's words reach here before a new session clears
     * it.
     */
    private fun replaced(raw: String): ReplacedTranscript =
        prepared ?: replaceSpoken(raw, replacements()).also { prepared = it }

    private fun commitTranscript(text: String) {
        // commitText replaces whatever is selected now, so an edit goes in only over the text it
        // revised.
        val edited = selected
        if (edited != null && selection() != edited) {
            failedCommit = text
            fail(session, FailureReason.SelectionChanged, "The selection changed", text)
            return
        }
        if (commit(text)) {
            inserted = true
            failedCommit = null
            onCommitted(
                Inserted(
                    text,
                    heardBytes / PCM_BYTES_PER_MILLI,
                    sourceProvider,
                    cleanupsCalled.toList(),
                    if (edited != null) prepared?.text ?: text else text,
                )
            )
        } else {
            failedCommit = text
            fail(session, FailureReason.Commit, "Could not insert text", text)
        }
    }

    private fun publishListening() =
        publish(listening((state as? DictationState.Listening)?.level ?: 0f))

    /** The current preview split into its committed and interim parts for the panel to render. */
    private fun listening(level: Float) =
        DictationState.Listening(
            finalText.toString(),
            interim,
            level,
            reconnecting,
            paused,
            stopping = pendingInsert != null,
            textAfterStop = sourceProvider.transcribesAfterStop,
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
        paused = false
        finishingPause = false
        stopCapture()
        client?.cancel()
        client = null
        request?.cancel()
        request = null
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

// 16 kHz mono PCM16, as the microphone captures it.
private const val PCM_BYTES_PER_MILLI = 16 * 2

/**
 * 80 s of it. The batch endpoint transcribes only the first ~86 s of a recording and returns that
 * prefix as a success (docs/research/0004), so a longer dictation keeps its streamed transcript
 * rather than lose its tail; 80 s keeps a margin under the observed cutoff.
 */
private const val MAX_RETRANSCRIBE_BYTES = 80_000 * PCM_BYTES_PER_MILLI

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
    val audio = context.getSystemService(AudioManager::class.java)
    // Exclusive transient focus is the one Android documents for speech recognition: other apps
    // pause rather than duck. Each app decides how to answer it, so this asks, it can't force.
    val mediaPause =
        AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE)
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ASSISTANT)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build()
            )
            .build()
    // The replacement step's rules: the user's own, {date} and {time} as the phone shows them now,
    // then the learned corrections for the target app.
    fun replacements(): List<Replacement> {
        val now = Date()
        return withLearnedCorrections(
            withVariablesFilled(
                settings.replacements,
                DateFormat.getDateFormat(context).format(now),
                DateFormat.getTimeFormat(context).format(now),
            ),
            settings.learnedCorrections,
            ActiveDictation.target?.packageName.orEmpty(),
        )
    }
    fun token(account: Provider) =
        store.validTokens(account.oauth, http)
            ?: throw ProviderFailure(ProviderFailureKind.Unavailable, "Not signed in")
    fun refineWithAccount(
        account: Provider,
        raw: String,
        context: RefinementContext,
        cancellation: Cancellation,
        onRefined: (String) -> Unit,
    ): String {
        val choice = settings.refinement(account)
        val ultrafast =
            account == Provider.ChatGpt &&
                settings.chatGptSpeed == OpenAiSpeed.Ultrafast &&
                modelSupportsUltrafast(choice.model)
        return refineTranscript(
                http,
                account.oauth,
                token(account),
                raw,
                refinementVocabulary(
                    settings.vocabulary,
                    context.profile,
                    settings.learnedCorrections,
                ),
                choice.model,
                choice.effort,
                context,
                endpoints.getValue(account).refinement,
                (if (ultrafast) ultrafastAvailable else fastModeAvailable.getValue(account))
                    .takeIf { settings.fastMode(account) },
                ultrafast,
                onRefined,
                cancellation,
            )
            .also { store.clearSessionEnded(account.oauth) }
    }
    fun targetContext() =
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
            .copy(bindingAliases = replacementAliases(replacements()))
    fun cleanUp(
        selected: CleanupProvider,
        raw: String,
        context: RefinementContext,
        cancellation: Cancellation,
        onRefined: (String) -> Unit,
    ): String? =
        // A profile set to no cleanup calls no provider: a dictation goes in as heard, as the
        // desktop does, and an edit fails.
        if (context.style == CleanupStrength.None) null
        // Each cleanup provider's client; a new provider adds its branch here.
        else
            when (selected) {
                CleanupProvider.ChatGpt,
                CleanupProvider.Claude ->
                    refineWithAccount(selected.account, raw, context, cancellation, onRefined)
            }
    return DictationEngine(
        microphone::capture,
        microphone::stop,
        { selected, onEvent ->
            spokenLanguageMismatch(selected, settings.spokenLanguage)?.let {
                throw SpokenLanguageUnsupported(it)
            }
            val account = selected.account
            val events = { event: SpeechEvent ->
                if (
                    account != null &&
                        (event == SpeechEvent.Connected || event is SpeechEvent.Final)
                )
                    store.clearSessionEnded(account.oauth)
                onEvent(event)
            }
            val terms =
                speechTerms(
                    settings.vocabulary,
                    writingProfile(settings, ActiveDictation.target),
                    settings.learnedCorrections,
                )
            // Each speech provider's client; a new provider adds its branch here.
            when (selected) {
                SpeechProvider.Claude -> {
                    val endpoint = endpoints.getValue(Provider.Claude).speech
                    ClaudeVoiceClient(
                        webSocketTransport(endpoint),
                        token(Provider.Claude).accessToken,
                        terms,
                        settings.spokenLanguage,
                        events,
                        endpoint,
                    )
                }
                SpeechProvider.ChatGpt -> {
                    val endpoint = endpoints.getValue(Provider.ChatGpt).speech
                    CodexDictationClient(
                        webSocketTransport(endpoint),
                        token(Provider.ChatGpt).accessToken,
                        settings.spokenLanguage,
                        events,
                        endpoint,
                    )
                }
                SpeechProvider.Endpoint -> {
                    if (settings.speechEndpoint.server.isEmpty())
                        throw ProviderFailure(ProviderFailureKind.Unavailable, "No server URL")
                    EndpointSpeechClient(
                        http,
                        settings.speechEndpoint,
                        store.apiKey(ApiKey.SpeechEndpoint),
                        terms.joinToString(", "),
                        settings.spokenLanguage,
                        events,
                    )
                }
            }
        },
        { selected, raw, cancellation, onRefined ->
            cleanUp(selected, raw, targetContext(), cancellation, onRefined)
        },
        if (settings.transcribePassEnabled)
            { pcm, cancellation ->
                transcribeSpeech(
                    token(Provider.ChatGpt).accessToken,
                    pcm,
                    endpoints.getValue(Provider.ChatGpt).transcribe!!,
                    cancellation,
                )
            }
        else null,
        { text ->
            // Kept before the commit, as the desktop does, so words that fail to go in stay too.
            val target = ActiveDictation.target
            if (target?.secure != true)
                ActiveDictation.latest = LatestTranscript(text, target?.label)
            val connection = connection()
            // Read before the commit: the text on each side pins the words for an edit after it.
            val window =
                connection
                    ?.takeIf {
                        settings.correctionLearningEnabled &&
                            target?.secure == false &&
                            ActiveDictation.watchCorrections != null
                    }
                    ?.let { correctionWindow(it, text) }
            val committed = connection?.commitText(text, 1) == true
            if (committed) {
                SettingsStore(context).recordVocabularyUsage(text)
                if (window != null && target != null)
                    ActiveDictation.watchCorrections?.invoke(window, target.packageName)
                main.post(onInserted)
            }
            committed
        },
        sharedExecutor,
        { next -> main.post { onState(next) } },
        // A refused request, as during a phone call, needs nothing: the media plays on, the
        // dictation goes ahead, and abandoning focus never granted does nothing.
        pauseMedia = { if (settings.pauseMedia) audio.requestAudioFocus(mediaPause) },
        resumeMedia = { audio.abandonAudioFocusRequest(mediaPause) },
        replacements = ::replacements,
        onCommitted = { inserted ->
            // The live setting, not the session's: turning insights off mid-dictation stops this
            // one being recorded, as on the desktop.
            if (SettingsStore(context).insightsEnabled()) {
                recordInsight(
                    insightsFile(context),
                    dictationRecord(inserted, settings, ActiveDictation.target),
                )
            }
        },
        selection = {
            // A password field's text never leaves the device, selected or not.
            if (ActiveDictation.target?.secure == false)
                connection()?.getSelectedText(0)?.toString()?.takeIf(String::isNotEmpty)
            else null
        },
        // The screenshot stays behind, as on the desktop: the edit is of the text, not the screen.
        editSelection = { selected, text, instructions, cancellation ->
            val context = targetContext().copy(selectedText = text, screenshotJpeg = null)
            cleanUp(selected, instructions, context, cancellation) {}
        },
    )
}
