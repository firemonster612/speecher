package app.speecher.android.dictation

import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.provider.cleanup
import app.speecher.android.provider.hasBatchTranscription
import app.speecher.android.provider.providerOrder
import app.speecher.android.provider.speech
import app.speecher.android.provider.transcribesAfterStop
import app.speecher.protocol.Cancellation
import app.speecher.protocol.ProviderFailureKind
import app.speecher.protocol.ReplacedTranscript
import app.speecher.protocol.Replacement
import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import app.speecher.protocol.failureKind
import app.speecher.protocol.preferredTranscript
import app.speecher.protocol.refineTranscript
import app.speecher.protocol.replaceSpoken
import java.io.ByteArrayOutputStream
import java.util.concurrent.Executor

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

/**
 * Where a dictation is. Each phase holds only what is true in it, so a pending Insert, a running
 * request and a failure's retry can never be set together.
 */
private sealed interface Phase {
    /** Not dictating: before the start, once the words went in, or after Cancel. */
    data object Idle : Phase

    /**
     * A speech stream is open or opening. [finishingPause] is set while the stream a pause stopped
     * has not ended: audio heard after a resume waits for it, since opening the next stream any
     * sooner would drop its final words.
     */
    data class Listening(val mic: Mic, val finishingPause: Boolean = false) : Phase

    /**
     * ChatGPT's batch pass, while [cleanup] is null, or the [cleanup] runs as [request], which
     * Cancel and Insert abort. [transcript] is the words it works on, as heard.
     */
    data class Refining(
        val request: Cancellation,
        val cleanup: CleanupProvider?,
        val transcript: String,
    ) : Phase

    /** [transcript] is what was heard before the failure, which Insert and Retry carry on from. */
    data class Failed(val retry: Retry, val transcript: String) : Phase
}

/** The microphone while listening. */
private sealed interface Mic {
    data object On : Mic

    /** Paused by the person: off, with the stream finishing the words already spoken. */
    data object Paused : Mic

    /**
     * Insert was tapped: off, with the stream finishing the last words, which [cleanup], if any,
     * then tidies.
     */
    data class Stopped(val cleanup: CleanupProvider?) : Mic
}

/** What Retry repeats after a failure. */
private sealed interface Retry {
    /** Commits [text] again: the field turned it down, or the selection it edits changed. */
    data class Commit(val text: String) : Retry

    /** Runs [cleanup] again on the words as heard. */
    data class Refine(val cleanup: CleanupProvider) : Retry

    /** Listens again, carrying on from the words heard so far. */
    data object Restart : Retry
}

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
    /** Puts non-blank text into the field; false when it could not. */
    private val commit: (String) -> Boolean,
    /**
     * Binds the words to the field focused now, the only one [commit] may put them into. Called by
     * each tap that acts: an Insert that stops the listening or commits, and a Retry.
     */
    private val bindField: () -> Unit,
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
    /** The dictation is done: its words went in, or there were none to put in. */
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

    /** Read off the lock by the capture loop, which stops once the microphone is no longer on. */
    @Volatile private var phase: Phase = Phase.Idle

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
    /** Audio heard after a resume while the paused stream finishes; see [Phase.Listening]. */
    private val heardAfterResume = mutableListOf<ByteArray>()
    /** This dictation's replacement step, once an Insert ran it; see [replaced]. */
    private var prepared: ReplacedTranscript? = null
    /**
     * The selection Insert refined found, which this dictation edits instead of inserting its
     * words, as the desktop does with a selection. Nothing goes in unless the edit succeeds. It
     * outlives the phases from the Insert on, through a failure and its retry.
     */
    private var selected: String? = null
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
        prepared = null
        selected = null
        sourceProvider = provider
        streamed = false
        reconnecting = false
        reconnectsLeft = RECONNECT_BACKOFF_MS.size
        phase = Phase.Listening(Mic.On)
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
            if (synchronized(this) { listeningTo(current, opening) == null }) return@execute
            try {
                val opened = connect(provider) { event -> onSpeech(current, opening, event) }
                synchronized(this) {
                    val now = listeningTo(current, opening)
                    if (now != null) {
                        client = opened
                        // One at a time: a client can fail as it is sent audio, replacing itself or
                        // ending the dictation, and what is left is not its to send.
                        while (client === opened && unsent.isNotEmpty()) {
                            opened.sendAudio(unsent.removeAt(0))
                        }
                        // Paused while it connected, or opened only for what a resume heard: it
                        // ends as it opens, whether or not the microphone is back on.
                        if (client === opened && (now.mic != Mic.On || now.finishingPause))
                            opened.stop()
                    } else opened.cancel()
                }
            } catch (e: CannotListen) {
                fail(current, e.reason, e.message.orEmpty())
            } catch (e: Exception) {
                synchronized(this) {
                    val now = listeningTo(current, opening) ?: return@synchronized
                    val kind = failureKind(e)
                    // A stream opened while a pause finishes, after this dictation has streamed:
                    // a blip ends it like a paused stream that failed, and the words stay.
                    if (now.finishingPause && streamed && !kind.needsSignIn)
                        pausedStreamEnded(current, now.mic, openFailure = kind)
                    else
                        streamFailed(
                            current,
                            now.mic,
                            retryable = !kind.needsSignIn,
                            kind,
                            "Could not connect to the speech provider",
                        )
                }
            }
        }
    }

    /** The listening phase while [opening] is still the stream this dictation waits on. */
    private fun listeningTo(current: Int, opening: Int): Phase.Listening? =
        (phase as? Phase.Listening)?.takeIf { current == session && opening == attempt }

    /** The listening an Insert can still stop: running or paused, not stopped already. */
    private val stoppable: Phase.Listening?
        get() = (phase as? Phase.Listening)?.takeIf { it.mic !is Mic.Stopped }

    /** Whether the microphone is on: listening, neither paused nor stopped for an Insert. */
    private val micOn: Boolean
        get() = (phase as? Phase.Listening)?.mic == Mic.On

    /**
     * Ends the listening, running or paused, for an Insert that [cleanup], if any, tidies; false
     * when there was none to end.
     */
    private fun stop(cleanup: CleanupProvider?): Boolean {
        val now = stoppable ?: return false
        bindField()
        phase = now.copy(mic = Mic.Stopped(cleanup))
        if (now.mic == Mic.Paused) {
            resumeMedia()
            // An Insert while paused takes what was heard once the paused stream has ended, and
            // once words a resume heard, left when no stream for them could open, reach one.
            when {
                now.finishingPause -> publish(listening(0f))
                unsent.isNotEmpty() -> {
                    phase = Phase.Listening(Mic.Stopped(cleanup), finishingPause = true)
                    reopen(session)
                }
                else -> insertBest(cleanup)
            }
            return true
        }
        stopCapture()
        resumeMedia()
        // Shown before the stream is told, as a stream can end, and the dictation move on, inside
        // its stop.
        publish(listening(0f))
        // A stream finishing a pause was already told; its end opens the one for what came after.
        if (!now.finishingPause) client?.stop()
        return true
    }

    /**
     * Turns the microphone off and lets the stream finish the words already spoken; they stay in
     * the transcript. Resume carries on in a new stream.
     */
    @Synchronized
    fun pause() {
        val now = phase as? Phase.Listening ?: return
        if (now.mic != Mic.On) return
        // A stream still connecting is stopped as it opens, and ends like any other. Paused again
        // before the last pause's stream ended, that stream is still finishing.
        phase = Phase.Listening(Mic.Paused, finishingPause = true)
        stopCapture()
        if (!now.finishingPause) client?.stop()
        publish(listening(0f))
    }

    @Synchronized
    fun resume() {
        val now = phase as? Phase.Listening ?: return
        if (now.mic != Mic.Paused) return
        phase = now.copy(mic = Mic.On)
        val current = session
        publish(listening(0f))
        executor.execute { captureAudio(current) }
        if (!now.finishingPause) reopen(current)
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
        when (val now = phase) {
            is Phase.Listening -> return stop(null)
            // During the cleanup, Insert stops it and takes the words as heard, as the desktop's
            // Cancel refinement does. Not during the batch pass, and an edit's words are
            // instructions, so it takes nothing there.
            is Phase.Refining -> {
                val cleanup = now.cleanup ?: return false
                if (selected != null) return false
                bindField()
                cancelSession()
                // The stopped cleanup was still called, as the desktop's record counts it.
                cleanupsCalled += cleanup
                commitHeard(now.transcript)
            }
            is Phase.Failed -> {
                if (selected != null) return false
                bindField()
                val retry = now.retry
                if (retry is Retry.Commit) commitTranscript(retry.text)
                else commitHeard(now.transcript)
            }
            Phase.Idle -> {}
        }
        return false
    }

    /** [insert] with [cleanup], if any, tidying the text first. */
    @Synchronized
    fun insertRefined(cleanup: CleanupProvider?): Boolean {
        if (stoppable == null) return false
        if (cleanup != null) selected = selection()
        return stop(cleanup)
    }

    /**
     * Repeats what failed. True when that starts listening again, the moment the desktop plays its
     * start sound; a repeated refinement or commit does not listen.
     */
    @Synchronized
    fun retry(): Boolean {
        val failed = phase as? Phase.Failed ?: return false
        bindField()
        when (val retry = failed.retry) {
            is Retry.Commit -> commitTranscript(retry.text)
            is Retry.Refine -> refineTranscript(retry.cleanup, failed.transcript)
            Retry.Restart -> {
                startSession(sourceProvider, failed.transcript)
                return true
            }
        }
        return false
    }

    private fun captureAudio(current: Int) {
        try {
            capture({ current == session && micOn }) { audio, level ->
                synchronized(this) {
                    val now = phase
                    if (current == session && now is Phase.Listening && now.mic == Mic.On) {
                        if (now.finishingPause) heardAfterResume.add(audio)
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
                        // The client can fail as it is sent audio, which can end the listening.
                        if (micOn) publish(listening(level))
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
        val cancellation = Cancellation()
        phase = Phase.Refining(cancellation, provider, raw)
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
                                if (isRunning(cancellation))
                                    publish(DictationState.Refining(raw, replaced.preview(refined)))
                            }
                        }
                synchronized(this) {
                    // A cleanup cancelled as it finished, by Insert or a failure, never goes in.
                    if (!isRunning(cancellation)) return@execute
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
                synchronized(this) {
                    // Called and failed, it still counts as one this dictation used.
                    if (current == session) cleanupsCalled += provider
                    if (isRunning(cancellation))
                        fail(
                            current,
                            FailureReason.Cleanup(provider, failureKind(e)),
                            "Could not refine the transcript",
                            raw,
                        )
                }
            }
        }
    }

    /** Whether [request] is still the batch pass or cleanup this dictation waits on. */
    private fun isRunning(request: Cancellation) = (phase as? Phase.Refining)?.request === request

    @Synchronized
    private fun onSpeech(current: Int, opening: Int, event: SpeechEvent) {
        val now = listeningTo(current, opening) ?: return
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
                when {
                    now.finishingPause -> pausedStreamEnded(current, now.mic)
                    now.mic is Mic.Stopped -> insertBest(now.mic.cleanup)
                    // Still recording, so the provider ended the session itself, as ChatGPT does at
                    // its session limit. That is not an error: carry on in a new session.
                    now.mic == Mic.On -> reopen(current)
                    else -> publish(listening(0f))
                }
            // A paused stream that fails to finish, closed during a long pause, has still ended.
            is SpeechEvent.Failed ->
                if (now.finishingPause) pausedStreamEnded(current, now.mic)
                else
                    streamFailed(
                        current,
                        now.mic,
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
        mic: Mic,
        retryable: Boolean,
        kind: ProviderFailureKind,
        detail: String,
    ) {
        if (mic is Mic.Stopped && !kind.needsSignIn && transcript().isNotBlank())
            insertBest(mic.cleanup)
        else if (retryable && streamed && mic == Mic.On && reconnectsLeft > 0) {
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
     * on like the paused one. Only then does an Insert go ahead. A stream that could not open, as
     * [openFailure] says, leaves that audio in [unsent] for the next resume or Insert rather than
     * retrying while paused; with an Insert waiting, the dictation fails instead of putting the
     * words in without it.
     */
    private fun pausedStreamEnded(
        current: Int,
        mic: Mic,
        openFailure: ProviderFailureKind? = null,
    ) {
        client = null
        unsent.addAll(heardAfterResume)
        heardAfterResume.clear()
        if (openFailure == null && mic != Mic.On && unsent.isNotEmpty()) {
            reopen(current)
            return
        }
        if (openFailure != null && mic is Mic.Stopped && unsent.isNotEmpty()) {
            fail(
                current,
                FailureReason.Speech(sourceProvider, openFailure),
                "Could not connect to the speech provider",
            )
            return
        }
        phase = Phase.Listening(mic)
        when (mic) {
            is Mic.Stopped -> insertBest(mic.cleanup)
            Mic.On -> reopen(current)
            Mic.Paused -> publishListening()
        }
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
        val cancellation = Cancellation()
        phase = Phase.Refining(cancellation, cleanup = null, streamed)
        publish(DictationState.Refining(streamed, transcribingAgain = true))
        executor.execute {
            val batch =
                try {
                    transcribe(audio, cancellation)
                } catch (_: Exception) {
                    null
                }
            synchronized(this) {
                if (isRunning(cancellation)) finish(preferredTranscript(batch, streamed))
            }
        }
    }

    /**
     * Fails the dictation, unless it ended or failed already: the microphone and the connection
     * start together and can both fail, and the first failure is the one shown. Retry repeats a
     * failed cleanup, and otherwise listens again.
     */
    @Synchronized
    private fun fail(
        current: Int,
        reason: FailureReason,
        detail: String,
        raw: String? = null,
    ) {
        val now = phase
        if (current != session || now == Phase.Idle || now is Phase.Failed) return
        val cleanup = (now as? Phase.Refining)?.cleanup
        // The transcript is read here, under the lock: a default argument would read it before
        // the lock is taken, while the speech thread may be appending to it.
        showFailure(
            reason,
            detail,
            raw ?: transcript(),
            cleanup?.let(Retry::Refine) ?: Retry.Restart,
        )
    }

    /** The field did not take [text]. This replaces any failure, with a retry that commits it. */
    private fun failCommit(reason: FailureReason, detail: String, text: String) =
        showFailure(reason, detail, text, Retry.Commit(text))

    private fun showFailure(reason: FailureReason, detail: String, raw: String, retry: Retry) {
        cancelSession()
        resumeMedia()
        phase = Phase.Failed(retry, raw)
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
            failCommit(FailureReason.SelectionChanged, "The selection changed", text)
            return
        }
        // Nothing heard leaves the field as it was: committing no text would delete a selection.
        if (text.isBlank() || commit(text)) {
            phase = Phase.Idle
            onCommitted(
                Inserted(
                    text,
                    heardBytes / PCM_BYTES_PER_MILLI,
                    sourceProvider,
                    cleanupsCalled.toList(),
                    if (edited != null) prepared?.text ?: text else text,
                )
            )
        } else failCommit(FailureReason.Commit, "Could not insert text", text)
    }

    private fun publishListening() =
        publish(listening((state as? DictationState.Listening)?.level ?: 0f))

    /** The current preview split into its committed and interim parts for the panel to render. */
    private fun listening(level: Float): DictationState.Listening {
        val mic = (phase as? Phase.Listening)?.mic
        return DictationState.Listening(
            finalText.toString(),
            interim,
            level,
            reconnecting,
            paused = mic == Mic.Paused,
            stopping = mic is Mic.Stopped,
            textAfterStop = sourceProvider.transcribesAfterStop,
        )
    }

    private fun transcript(): String =
        if (finalText.isEmpty()) interim
        else if (interim.isEmpty()) finalText.toString() else "$finalText $interim"

    private fun publish(next: DictationState) {
        state = next
        onState(next)
    }

    /** Stops everything the dictation runs and leaves it idle, for the caller to move on from. */
    private fun cancelSession() {
        val request = (phase as? Phase.Refining)?.request
        phase = Phase.Idle
        stopCapture()
        client?.cancel()
        client = null
        request?.cancel()
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

/**
 * The speech stream can't open, as [reason] says: the provider can't listen for the saved spoken
 * language, or Android keeps Speecher from reaching the server. Retrying would not change that.
 */
class CannotListen(val reason: FailureReason) : Exception("$reason")
