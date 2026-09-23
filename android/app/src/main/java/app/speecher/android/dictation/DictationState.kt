package app.speecher.android.dictation

import app.speecher.protocol.OAuthProvider

/** The two accounts Speecher can sign in to. Each one can transcribe and refine. */
enum class Provider {
    Claude,
    ChatGpt,
}

val Provider.oauth: OAuthProvider
    get() = if (this == Provider.Claude) OAuthProvider.Claude else OAuthProvider.ChatGpt

/** What the dictation panel shows. The engine produces it; the UI only renders it. */
sealed interface DictationState {
    /** The panel is up and the microphone is not yet streaming. */
    data object Connecting : DictationState

    /**
     * Streaming. [transcript] is the live preview, and [level] is the input loudness from 0 to 1.
     */
    data class Listening(val transcript: String, val level: Float) : DictationState

    /** The user asked for a refined insert and the cleanup pass is running. */
    data class Refining(val transcript: String) : DictationState

    /** Dictation stopped. [transcript] holds whatever was heard before the failure. */
    data class Failed(
        val reason: FailureReason,
        val detail: String,
        val transcript: String,
        val provider: Provider? = null,
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

/** Everything the user can change in Settings. */
data class SpeecherSettings(
    val transcriptionProvider: Provider = Provider.Claude,
    val refinementEnabled: Boolean = true,
    val refinementProvider: Provider = Provider.Claude,
    val vocabulary: List<String> = emptyList(),
)

/** What the setup checklist needs to know. Each flag is one step. */
data class SetupStatus(
    val signedIn: Set<Provider>,
    val microphoneGranted: Boolean,
    val keyboardEnabled: Boolean,
    val chipEnabled: Boolean,
    val swapGranted: Boolean,
) {
    val complete: Boolean
        get() =
            signedIn.isNotEmpty() &&
                microphoneGranted &&
                keyboardEnabled &&
                chipEnabled &&
                swapGranted
}
