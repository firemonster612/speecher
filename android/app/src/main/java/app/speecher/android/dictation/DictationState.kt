package app.speecher.android.dictation

import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.Provider
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.provider.label
import app.speecher.android.settings.PanelSize
import app.speecher.protocol.ProviderFailureKind
import java.io.Serializable

/** What the dictation panel shows. The engine produces it; the UI only renders it. */
sealed interface DictationState {
    /**
     * The microphone is live from the tap, while the connection may still be opening. [committed]
     * is the finalised text and [interim] the recogniser's current guess for the word in progress;
     * keeping them apart lets the preview grow append-only instead of reflowing whenever an interim
     * shrinks. [level] is the input loudness from 0 to 1. [reconnecting] is set while a dropped
     * speech stream is being reopened; the microphone keeps recording meanwhile. [paused] is set
     * while the person paused: the microphone is off and the words so far are kept. [stopping] is
     * set once Insert is tapped: the microphone is off and the last words are on their way.
     * [textAfterStop] is set when the speech service only transcribes once the recording stops, so
     * Insert is there before any words are.
     */
    data class Listening(
        val committed: String = "",
        val interim: String = "",
        val level: Float = 0f,
        val reconnecting: Boolean = false,
        val paused: Boolean = false,
        val stopping: Boolean = false,
        val textAfterStop: Boolean = false,
    ) : DictationState {
        /** The whole live preview: committed text with the interim word appended. */
        val text: String
            get() =
                if (committed.isEmpty()) interim
                else if (interim.isEmpty()) committed else "$committed $interim"
    }

    /**
     * The user pressed Insert and the batch or cleanup pass is running: ChatGPT's second
     * transcription pass while [transcribingAgain], then the cleanup. [refined] is the cleanup text
     * streamed so far, empty until its first token arrives. [editsSelection] is set while the
     * cleanup edits the field's selection: the words are instructions, so Insert cannot take them.
     */
    data class Refining(
        val transcript: String,
        val refined: String = "",
        val transcribingAgain: Boolean = false,
        val editsSelection: Boolean = false,
    ) : DictationState

    /**
     * Dictation stopped. [transcript] holds whatever was heard before the failure. [detail] is
     * diagnostic and never shown. [editsSelection] is set when the dictation was editing the
     * field's selection: the words are instructions, so the panel offers no Insert and the field
     * stays as it was.
     */
    data class Failed(
        val reason: FailureReason,
        val detail: String,
        val transcript: String,
        val editsSelection: Boolean = false,
    ) : DictationState
}

/**
 * Whether the panel offers Insert. While listening, until it is tapped, once there are words or
 * when the service transcribes only after the stop. During the cleanup, which Insert stops to take
 * the words as heard. After a failure that kept words, unless its recovery commits them already.
 * Never during the batch pass, and never for a selection edit, whose words are instructions.
 */
val DictationState.offersInsert: Boolean
    get() =
        when (this) {
            is DictationState.Listening -> !stopping && (text.isNotBlank() || textAfterStop)
            is DictationState.Refining -> !transcribingAgain && !editsSelection
            is DictationState.Failed ->
                reason != FailureReason.Commit && !editsSelection && transcript.isNotBlank()
        }

/** Why dictation failed. Each reason has one [recovery]. */
sealed interface FailureReason {
    /** Recovery: open the app to grant the microphone. */
    data object MicrophoneDenied : FailureReason

    /** Another app may hold the microphone. Recovery: retry. */
    data object MicrophoneUnavailable : FailureReason

    /**
     * [provider] can't listen for the saved spoken [language], a code. Recovery: open the
     * Transcription settings to choose another.
     */
    data class SpokenLanguage(val provider: SpeechProvider, val language: String) : FailureReason

    /**
     * The transcript exists but insertion into the field failed, so the panel offers a single retry
     * instead of two buttons that do the same commit.
     */
    data object Commit : FailureReason

    /**
     * A selection edit with the profile's cleanup set to None. Recovery: open the Profiles
     * settings.
     */
    data object SelectionNeedsCleanup : FailureReason

    /**
     * The field's selection is no longer the text that was edited, so the revision is held back.
     * Recovery: retry, once the same text is selected again.
     */
    data object SelectionChanged : FailureReason

    /**
     * The speech Custom Endpoint is on the local network, which Android keeps Speecher from
     * reaching until the person allows it. Recovery: open the Transcription settings, whose
     * connection test asks.
     */
    data object LocalNetworkDenied : FailureReason

    /**
     * A speech or cleanup provider could not do its part, as [kind]. Recovery: sign in to [account]
     * again when the sign-in is the problem, or fix a server's settings, otherwise retry.
     */
    sealed interface ProviderFailed : FailureReason {
        /** The provider's account, or null for a server the person chose. */
        val account: Provider?
        val label: String
        val kind: ProviderFailureKind
    }

    data class Speech(val provider: SpeechProvider, override val kind: ProviderFailureKind) :
        ProviderFailed {
        override val account: Provider?
            get() = provider.account

        override val label: String
            get() = provider.label
    }

    data class Cleanup(val provider: CleanupProvider, override val kind: ProviderFailureKind) :
        ProviderFailed {
        override val account: Provider
            get() = provider.account

        override val label: String
            get() = provider.account.label
    }
}

/**
 * Whether signing in fixes a failure of this kind: the sign-in was turned down, or there is none.
 * Retrying would only fail again.
 */
val ProviderFailureKind.needsSignIn: Boolean
    get() = this == ProviderFailureKind.Authentication || this == ProviderFailureKind.Unavailable

/**
 * What the panel's recovery button does for a failure. [Retry] runs in place; the others switch
 * back and open Speecher, which receives them as its [RECOVERY][app.speecher.android.RECOVERY]
 * extra and shows where the person can do what the case names.
 */
sealed interface Recovery : Serializable {
    data object Retry : Recovery

    data class SignIn(val account: Provider) : Recovery

    /** The fix is in Speecher itself rather than a sign-in. */
    sealed interface OpenApp : Recovery

    data object AllowMicrophone : OpenApp

    data object ChooseSpokenLanguage : OpenApp

    /** Check a Custom Endpoint's key and address, or test it to allow the local network. */
    data object CheckSpeechServer : OpenApp

    /** Choose a cleanup level for the Writing Profile. */
    data object ChooseCleanup : OpenApp
}

val FailureReason.recovery: Recovery
    get() =
        when (this) {
            FailureReason.MicrophoneUnavailable,
            FailureReason.Commit,
            FailureReason.SelectionChanged -> Recovery.Retry
            FailureReason.MicrophoneDenied -> Recovery.AllowMicrophone
            is FailureReason.SpokenLanguage -> Recovery.ChooseSpokenLanguage
            FailureReason.SelectionNeedsCleanup -> Recovery.ChooseCleanup
            FailureReason.LocalNetworkDenied -> Recovery.CheckSpeechServer
            is FailureReason.ProviderFailed ->
                if (!kind.needsSignIn) Recovery.Retry
                // A server the person chose has no sign-in; its settings hold the key.
                else account?.let(Recovery::SignIn) ?: Recovery.CheckSpeechServer
        }

/**
 * The size the panel shows: the [chosen] one it opens at, or, once [toggled] by the minimize
 * control, the other side of it (Full and Compact collapse to the bar, the bar expands to Full). A
 * failure never shows as the bar, so its recovery action is always in view.
 */
fun shownPanelSize(chosen: PanelSize, toggled: Boolean, state: DictationState): PanelSize {
    val expanded = if (chosen == PanelSize.Minimized) PanelSize.Full else chosen
    val minimized = (chosen == PanelSize.Minimized) != toggled
    return if (minimized && state !is DictationState.Failed) PanelSize.Minimized else expanded
}
