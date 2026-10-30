package app.speecher.android.dictation

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import android.text.format.DateFormat
import app.speecher.android.BuildConfig
import app.speecher.android.auth.ApiKey
import app.speecher.android.auth.TokenStore
import app.speecher.android.lacksLocalNetwork
import app.speecher.android.onLocalNetwork
import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.OpenAiSpeed
import app.speecher.android.provider.Provider
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.provider.oauth
import app.speecher.android.provider.speech
import app.speecher.android.settings.SettingsStore
import app.speecher.android.sharedExecutor
import app.speecher.android.sharedHttp
import app.speecher.protocol.Cancellation
import app.speecher.protocol.ClaudeVoiceClient
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CodexDictationClient
import app.speecher.protocol.EndpointSpeechClient
import app.speecher.protocol.ProviderFailure
import app.speecher.protocol.ProviderFailureKind
import app.speecher.protocol.RefinementContext
import app.speecher.protocol.Replacement
import app.speecher.protocol.SpeechEvent
import app.speecher.protocol.modelSupportsUltrafast
import app.speecher.protocol.refineTranscript
import app.speecher.protocol.refinementVocabulary
import app.speecher.protocol.replacementAliases
import app.speecher.protocol.speechTerms
import app.speecher.protocol.transcribeSpeech
import app.speecher.protocol.webSocketTransport
import app.speecher.protocol.withLearnedCorrections
import app.speecher.protocol.withVariablesFilled
import java.util.Date
import java.util.concurrent.atomic.AtomicBoolean

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
 * Android wiring for [session]'s engine, reading its field and captures as they change; call
 * [DictationEngine.start] on the chip tap before the IME appears.
 */
fun createDictationEngine(
    context: Context,
    session: DictationSession,
    onState: (DictationState) -> Unit,
    onInserted: () -> Unit,
): DictationEngine {
    val settings = session.settings
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
            session.target?.packageName.orEmpty(),
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
                session.target,
                session.screen,
                session.screenshotJpeg,
            ) { length ->
                session.connection?.getSurroundingText(length, length, 0)?.let {
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
                throw CannotListen(it)
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
                    writingProfile(settings, session.target),
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
                    if (
                        lacksLocalNetwork(context) && onLocalNetwork(settings.speechEndpoint.server)
                    )
                        throw CannotListen(FailureReason.LocalNetworkDenied)
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
        { text -> commitToField(context, session, text) },
        sharedExecutor,
        { next ->
            main.post {
                // A rejected sign-in ends the account's session, the opposite of the
                // clearSessionEnded calls above.
                val reason = (next as? DictationState.Failed)?.reason
                if (
                    reason is FailureReason.ProviderFailed &&
                        reason.kind == ProviderFailureKind.Authentication
                )
                    reason.account?.let { store.endSession(it.oauth) }
                onState(next)
            }
        },
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
                    dictationRecord(inserted, settings, session.target),
                )
            }
            main.post(onInserted)
        },
        selection = {
            // A password field's text never leaves the device, selected or not.
            if (session.target?.secure == false)
                session.connection?.getSelectedText(0)?.toString()?.takeIf(String::isNotEmpty)
            else null
        },
        // The screenshot stays behind, as on the desktop: the edit is of the text, not the screen.
        editSelection = { selected, text, instructions, cancellation ->
            val context = targetContext().copy(selectedText = text, screenshotJpeg = null)
            cleanUp(selected, instructions, context, cancellation) {}
        },
    )
}
