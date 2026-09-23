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

    /** The user asked for a refined insert and the cleanup pass is running. */
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

/** Everything the user can change in Settings. */
data class SpeecherSettings(
    val transcriptionProvider: Provider = providerOrder.first(),
    val refinementEnabled: Boolean = true,
    val refinementProvider: Provider = providerOrder.first(),
    val vocabulary: List<String> = emptyList(),
    /** The dragged chip position, as a pixel offset from the keyboard's bottom-right corner. */
    val chipOffsetX: Int? = null,
    val chipOffsetY: Int? = null,
)

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
