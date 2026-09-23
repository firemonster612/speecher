package app.speecher.android.dictation

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.inputmethod.InputConnection
import app.speecher.android.BuildConfig
import app.speecher.android.auth.TokenStore
import app.speecher.protocol.ClaudeVoiceClient
import app.speecher.protocol.CodexDictationClient
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokenClient
import app.speecher.protocol.SpeechClient
import app.speecher.protocol.SpeechEvent
import app.speecher.protocol.TranscriptRefiner
import java.util.concurrent.Executor
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import okhttp3.OkHttpClient

class DictationEngine(
    private val microphone: AudioCapture,
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
    private var recording = false
    private var inserted = false
    private var session = 0

    @Synchronized
    fun start(provider: Provider) {
        cancelSession()
        val current = ++session
        finalText.clear()
        interim = ""
        inserted = false
        recording = true
        microphone.prepare()
        publish(DictationState.Connecting)
        executor.execute {
            try {
                val opened = connect(provider) { event -> onSpeech(current, event) }
                synchronized(this) {
                    if (current == session && recording) client = opened else opened.cancel()
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
        microphone.stop()
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
        if (inserted) return
        stop()
        inserted = true
        commit(transcript())
    }

    @Synchronized
    fun insertRefined(provider: Provider) {
        if (inserted) return
        stop()
        val current = session
        val raw = transcript()
        publish(DictationState.Refining(raw))
        executor.execute {
            try {
                val text = refine(provider, raw)
                synchronized(this) {
                    if (current != session || inserted) return@execute
                    inserted = true
                    commit(text)
                }
            } catch (_: SignInRequired) {
                fail(current, FailureReason.SignedOut, "Sign in to continue")
            } catch (_: Exception) {
                fail(current, FailureReason.Provider, "Could not refine the transcript")
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
                        microphone.capture { audio, level ->
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
                        fail(current, FailureReason.MicrophoneDenied, "Microphone unavailable")
                    }
                }
            }
            is SpeechEvent.Partial -> {
                interim = event.text
                publish(
                    DictationState.Listening(
                        transcript(),
                        (state as? DictationState.Listening)?.level ?: 0f,
                    )
                )
            }
            is SpeechEvent.Final -> {
                if (finalText.isNotEmpty()) finalText.append(' ')
                finalText.append(event.text)
                interim = ""
                publish(
                    DictationState.Listening(
                        transcript(),
                        (state as? DictationState.Listening)?.level ?: 0f,
                    )
                )
            }
            SpeechEvent.Completed -> publish(DictationState.Listening(transcript(), 0f))
            is SpeechEvent.Failed ->
                fail(
                    current,
                    if (event.authentication) FailureReason.SignedOut else FailureReason.Network,
                    "Speech connection failed",
                )
        }
    }

    @Synchronized
    private fun fail(current: Int, reason: FailureReason, detail: String) {
        if (current != session || inserted) return
        recording = false
        microphone.stop()
        client?.cancel()
        publish(DictationState.Failed(reason, detail, transcript()))
    }

    private fun transcript(): String =
        if (finalText.isEmpty()) interim
        else if (interim.isEmpty()) finalText.toString() else "$finalText $interim"

    private fun publish(next: DictationState) {
        state = next
        onState(next)
    }

    private fun cancelSession() {
        recording = false
        microphone.stop()
        client?.cancel()
        client = null
    }

    override fun close() {
        cancel()
        (executor as? ExecutorService)?.shutdownNow()
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
    val http = OkHttpClient()
    val tokenClient = OAuthTokenClient(http)
    val main = Handler(Looper.getMainLooper())
    fun provider(value: Provider) =
        if (value == Provider.Claude) OAuthProvider.Claude else OAuthProvider.ChatGpt
    fun token(value: Provider) =
        store.validTokens(provider(value), tokenClient) ?: throw SignInRequired()
    return DictationEngine(
        Microphone(context),
        { selected, events ->
            val access = token(selected).accessToken
            val fake = BuildConfig.FAKE_SPEECH_BASE.takeIf(String::isNotEmpty)
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
            else CodexDictationClient(http, access, events)
        },
        { selected, raw ->
            val refiner = TranscriptRefiner(http)
            val fake = BuildConfig.FAKE_SPEECH_BASE.takeIf(String::isNotEmpty)
            if (fake == null)
                refiner.refine(provider(selected), token(selected), raw, settings.vocabulary)
            else
                refiner.refine(
                    provider(selected),
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
        Executors.newCachedThreadPool(),
        { next -> main.post { onState(next) } },
    )
}
