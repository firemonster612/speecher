package app.speecher.android.dictation

import app.speecher.android.provider.CleanupProvider
import app.speecher.protocol.Cancellation
import app.speecher.protocol.Replacement
import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import java.util.concurrent.Executor

/**
 * A [DictationEngine] whose microphone, speech streams and field a test drives by hand, recording
 * what the engine did to them. Every collaborator has a default, so a test passes only what it
 * varies: the cleanup, the batch pass, the selection edit, the replacements, how background work
 * runs (on the calling thread unless given an [executor]) and, for protocol tests, a real speech
 * client in place of the fake [FakeSpeechClient].
 */
internal class TestDictation(
    refine: (CleanupProvider, String, Cancellation, (String) -> Unit) -> String? = { _, raw, _, _ ->
        raw
    },
    transcribe: ((ByteArray, Cancellation) -> String)? = null,
    editSelection: (CleanupProvider, String, String, Cancellation) -> String? = { _, _, _, _ ->
        null
    },
    replacements: () -> List<Replacement> = { emptyList() },
    executor: Executor = Executor(Runnable::run),
    speechClient: (((SpeechEvent) -> Unit) -> SpeechClient)? = null,
    onState: (DictationState) -> Unit = {},
) {
    /** Each fake stream the engine opened, in order. Empty when [speechClient] opens them. */
    val clients = mutableListOf<FakeSpeechClient>()
    /** Every text the engine tried to put into the field, whether or not the field took it. */
    val commits = mutableListOf<String>()
    val states = mutableListOf<DictationState>()
    val inserted = mutableListOf<Inserted>()
    /** The reconnect backoffs the engine waited out, which take no time here. */
    val pauses = mutableListOf<Long>()
    /** "pause" and "resume", as the engine paused and resumed other apps' media. */
    val media = mutableListOf<String>()
    /** Whether the field takes a commit. */
    var fieldTakesText = true
    /** How many taps bound the words to the field focused then. */
    var fieldsBound = 0
    /** The field's selected text, read when Insert refined is tapped. */
    var selection: String? = null
    /** Thrown by every stream the engine opens while set. */
    var connectFailure: Exception? = null
    /** Thrown by the microphone as it starts while set. */
    var microphoneFailure: Exception? = null
    private var microphone: ((ByteArray, Float) -> Unit)? = null

    val engine =
        DictationEngine(
            capture = { shouldContinue, onAudio ->
                microphoneFailure?.let { throw it }
                if (shouldContinue()) microphone = onAudio
            },
            stopCapture = { microphone = null },
            connect = { _, events ->
                connectFailure?.let { throw it }
                speechClient?.invoke(events) ?: FakeSpeechClient(events).also(clients::add)
            },
            refine = refine,
            transcribe = transcribe,
            commit = {
                commits.add(it)
                fieldTakesText
            },
            bindField = { fieldsBound++ },
            executor = executor,
            onState = {
                states.add(it)
                onState(it)
            },
            pause = pauses::add,
            pauseMedia = { media.add("pause") },
            resumeMedia = { media.add("resume") },
            replacements = replacements,
            onCommitted = inserted::add,
            selection = { selection },
            editSelection = editSelection,
        )

    /** The stream opened last. */
    val client: FakeSpeechClient
        get() = clients.last()

    /** The microphone hears [audio] at [level], if it is on. */
    fun hear(vararg audio: Byte, level: Float = 0f) = microphone?.invoke(audio, level)

    /** The stream opened last reports [event]. */
    fun speak(event: SpeechEvent) = client.events(event)
}

/** A speech stream that keeps the audio it was sent and reports what a test tells [events]. */
internal class FakeSpeechClient(val events: (SpeechEvent) -> Unit) : SpeechClient {
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

/** Holds the engine's background work until the test runs it. */
internal class ManualExecutor : Executor {
    private val queue = ArrayDeque<Runnable>()

    override fun execute(task: Runnable) {
        queue.add(task)
    }

    fun runNext() = queue.removeFirst().run()

    fun runAll() {
        while (queue.isNotEmpty()) runNext()
    }

    fun isEmpty() = queue.isEmpty()
}
