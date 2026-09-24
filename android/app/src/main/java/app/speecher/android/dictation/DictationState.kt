package app.speecher.android.dictation

import app.speecher.protocol.OAuthProvider

/** The two accounts Speecher can sign in to. Each one can transcribe and refine. */
enum class Provider {
    Claude,
    ChatGpt,
}

val Provider.oauth: OAuthProvider
    get() = if (this == Provider.Claude) OAuthProvider.Claude else OAuthProvider.ChatGpt

/** Only ChatGPT has a batch speech-to-text pass; both Insert buttons can run it. */
val Provider.hasBatchTranscription: Boolean
    get() = this == Provider.ChatGpt

/** What the dictation panel shows. The engine produces it; the UI only renders it. */
sealed interface DictationState {
    /** The panel is up and the microphone is not yet streaming. */
    data object Connecting : DictationState

    /**
     * Streaming. [committed] is the finalised text and [interim] the recogniser's current guess for
     * the word in progress; keeping them apart lets the preview grow append-only instead of
     * reflowing whenever an interim shrinks. [level] is the input loudness from 0 to 1.
     */
    data class Listening(val committed: String, val interim: String, val level: Float) :
        DictationState {
        /** The whole live preview: committed text with the interim word appended. */
        val text: String
            get() =
                if (committed.isEmpty()) interim
                else if (interim.isEmpty()) committed else "$committed $interim"
    }

    /** The user pressed Insert and the batch or cleanup pass is running. */
    data class Refining(val transcript: String) : DictationState

    /**
     * Dictation stopped. [transcript] holds whatever was heard before the failure. [commitFailed]
     * marks the one case where the transcript exists but insertion into the field failed, so the
     * panel offers a single retry instead of two buttons that do the same commit.
     */
    data class Failed(
        val reason: FailureReason,
        val detail: String,
        val transcript: String,
        val provider: Provider? = null,
        val commitFailed: Boolean = false,
    ) : DictationState
}

/** Why dictation failed. Each reason maps to one recovery action in the panel. */
enum class FailureReason {
    /** Recovery: open the app to grant the microphone. */
    MicrophoneDenied,

    /** Recovery: open the app to sign in again. */
    SignedOut,

    /** Recovery: retry. */
    Network,

    /** The provider refused or failed. Recovery: retry. */
    Provider,
}

/**
 * The order providers are listed in everywhere in the UI: alphabetical, chosen deliberately so it
 * is not the accident of enum declaration order and carries no bias toward either account.
 */
val providerOrder = listOf(Provider.ChatGpt, Provider.Claude)

/**
 * The provider to actually use: the one the user picked if they are signed into it, otherwise the
 * first signed-in provider in [providerOrder]. Keeps dictation working when only the other account
 * is connected.
 */
fun resolveSignedIn(preferred: Provider, signedIn: Set<Provider>): Provider =
    if (preferred in signedIn) preferred
    else providerOrder.firstOrNull { it in signedIn } ?: preferred

/**
 * The provider a fresh install defaults to: whichever account the user is signed into, or the first
 * in [providerOrder] when none is (no hardcoded Claude bias).
 */
fun defaultProvider(signedIn: Set<Provider>): Provider =
    providerOrder.firstOrNull { it in signedIn } ?: providerOrder.first()

/** The model and reasoning effort one provider refines with, as the API ids it sends. */
data class RefinementChoice(val model: String, val effort: String)

/** Desktop's defaults, moved to the current models: no reasoning for OpenAI, low for Claude. */
val Provider.defaultRefinement: RefinementChoice
    get() =
        when (this) {
            Provider.ChatGpt -> RefinementChoice("gpt-6-luna", "none")
            Provider.Claude -> RefinementChoice("claude-sonnet-5", "low")
        }

/** The refinement models Settings offers, as API id to label; the default comes first. */
val Provider.refinementModels: Map<String, String>
    get() =
        when (this) {
            Provider.ChatGpt ->
                mapOf(
                    "gpt-6-luna" to "GPT-6 Luna",
                    "gpt-6-sol" to "GPT-6 Sol",
                    "gpt-5.6-luna" to "GPT-5.6 Luna",
                )
            Provider.Claude ->
                mapOf("claude-sonnet-5" to "Claude Sonnet 5", "claude-opus-5" to "Claude Opus 5")
        }

/**
 * The efforts each endpoint accepts: OpenAI's `reasoning.effort` can turn reasoning off, while
 * Anthropic's adaptive-thinking `output_config.effort` starts at low.
 */
val Provider.refinementEfforts: List<String>
    get() =
        when (this) {
            Provider.ChatGpt -> listOf("none", "low", "medium", "high")
            Provider.Claude -> listOf("low", "medium", "high")
        }

/** Everything the user can change in Settings. */
data class SpeecherSettings(
    val transcriptionProvider: Provider = providerOrder.first(),
    val refinementEnabled: Boolean = true,
    val refinementProvider: Provider = providerOrder.first(),
    /** Whether both Insert buttons re-transcribe ChatGPT dictation with GPT Transcribe first. */
    val transcribePassEnabled: Boolean = true,
    val chatGptRefinement: RefinementChoice = Provider.ChatGpt.defaultRefinement,
    val claudeRefinement: RefinementChoice = Provider.Claude.defaultRefinement,
    val vocabulary: List<String> = emptyList(),
    /** Place the chip on the keyboard's mic key; off uses the custom position below. */
    val chipDockOnMic: Boolean = true,
    /** The custom chip position, as a pixel offset from the keyboard's bottom-right corner. */
    val chipOffsetX: Int? = null,
    val chipOffsetY: Int? = null,
    /** Keep the display awake while a dictation is running. */
    val keepScreenOn: Boolean = true,
) {
    fun refinement(provider: Provider): RefinementChoice =
        if (provider == Provider.Claude) claudeRefinement else chatGptRefinement

    fun withRefinement(provider: Provider, choice: RefinementChoice): SpeecherSettings =
        if (provider == Provider.Claude) copy(claudeRefinement = choice)
        else copy(chatGptRefinement = choice)
}

/** What the setup checklist needs to know. Each flag is one step. */
data class SetupStatus(
    val signedIn: Set<Provider>,
    val microphoneGranted: Boolean,
    val keyboardEnabled: Boolean,
    val chipEnabled: Boolean,
) {
    val complete: Boolean
        get() = signedIn.isNotEmpty() && microphoneGranted && keyboardEnabled && chipEnabled
}
