package app.speecher.android.dictation

import app.speecher.android.update.IntervalUnit
import app.speecher.android.update.UpdateChannel
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import app.speecher.protocol.ENGLISH_LANGUAGE
import app.speecher.protocol.LearnedCorrection
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.ProviderFailureKind
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.Replacement
import app.speecher.protocol.SpeechEndpoint
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.withoutDeleted

/** The two accounts Speecher can sign in to. Each one can transcribe and refine. */
enum class Provider {
    Claude,
    ChatGpt,
}

val Provider.oauth: OAuthProvider
    get() = if (this == Provider.Claude) OAuthProvider.Claude else OAuthProvider.ChatGpt

/** The provider's name as people know it, in the app and on the browser's sign-in page. */
val Provider.label: String
    get() =
        when (this) {
            Provider.Claude -> "Claude"
            Provider.ChatGpt -> "ChatGPT"
        }

/**
 * Who turns speech into the transcript: an account's speech service, or a server the person chose,
 * which has no [account]. The values are saved by name, the names the accounts were saved by
 * before.
 */
enum class SpeechProvider(val account: Provider?) {
    ChatGpt(Provider.ChatGpt),
    Claude(Provider.Claude),
    Endpoint(null),
}

/** The provider's name in the app and the panel. */
val SpeechProvider.label: String
    get() = account?.label ?: "Custom Endpoint"

/** Who cleans up the transcript. As with [SpeechProvider], each one is an account for now. */
enum class CleanupProvider(val account: Provider) {
    ChatGpt(Provider.ChatGpt),
    Claude(Provider.Claude),
}

/** The account's speech service. */
val Provider.speech: SpeechProvider
    get() = SpeechProvider.entries.first { it.account == this }

/** The account's cleanup service. */
val Provider.cleanup: CleanupProvider
    get() = CleanupProvider.entries.first { it.account == this }

/** Only ChatGPT has a batch speech-to-text pass; both Insert buttons can run it. */
val SpeechProvider.hasBatchTranscription: Boolean
    get() = this == SpeechProvider.ChatGpt

/** A Custom Endpoint transcribes the whole recording once it stops, so no words show before. */
val SpeechProvider.transcribesAfterStop: Boolean
    get() = this == SpeechProvider.Endpoint

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
     * diagnostic and never shown, except a spoken language mismatch's, which is already its words.
     * [editsSelection] is set when the dictation was editing the field's selection: the words are
     * instructions, so the panel offers no Insert and the field stays as it was.
     */
    data class Failed(
        val reason: FailureReason,
        val detail: String,
        val transcript: String,
        val editsSelection: Boolean = false,
    ) : DictationState
}

/** Why dictation failed. Each reason maps to one recovery action in the panel. */
sealed interface FailureReason {
    /** Recovery: open the app to grant the microphone. */
    data object MicrophoneDenied : FailureReason

    /** Another app may hold the microphone. Recovery: retry. */
    data object MicrophoneUnavailable : FailureReason

    /** The provider can't listen for the saved spoken language. Recovery: open the app. */
    data object SpokenLanguage : FailureReason

    /**
     * The transcript exists but insertion into the field failed, so the panel offers a single retry
     * instead of two buttons that do the same commit.
     */
    data object Commit : FailureReason

    /** A selection edit with the profile's cleanup set to None. Recovery: open the app. */
    data object SelectionNeedsCleanup : FailureReason

    /**
     * The field's selection is no longer the text that was edited, so the revision is held back.
     * Recovery: retry, once the same text is selected again.
     */
    data object SelectionChanged : FailureReason

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

/** The account to sign in to, when signing in is what fixes this failure. */
val FailureReason.signInAccount: Provider?
    get() = (this as? FailureReason.ProviderFailed)?.takeIf { it.kind.needsSignIn }?.account

/** Whether a server's settings fix this failure: it turned down its key, or it isn't set up. */
val FailureReason.needsServerSettings: Boolean
    get() = this is FailureReason.ProviderFailed && account == null && kind.needsSignIn

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
 * The speech provider a dictation uses: a server as chosen, an account as [resolveSignedIn] has it.
 */
fun resolveSpeech(preferred: SpeechProvider, signedIn: Set<Provider>): SpeechProvider =
    preferred.account?.let { resolveSignedIn(it, signedIn).speech } ?: preferred

/**
 * The provider a fresh install defaults to: whichever account the user is signed into, or the first
 * in [providerOrder] when none is (no hardcoded Claude bias).
 */
fun defaultProvider(signedIn: Set<Provider>): Provider =
    providerOrder.firstOrNull { it in signedIn } ?: providerOrder.first()

/** The model and reasoning effort one provider refines with, as the API ids it sends. */
data class RefinementChoice(val model: String, val effort: String)

/**
 * Desktop's efforts: no reasoning for OpenAI, low for Claude. The OpenAI model is desktop's too,
 * but Claude defaults to Sonnet 5.5 where the desktop defaults to Opus 5.5.
 */
val Provider.defaultRefinement: RefinementChoice
    get() =
        when (this) {
            Provider.ChatGpt -> RefinementChoice("gpt-6-luna", "none")
            Provider.Claude -> RefinementChoice("claude-sonnet-5-5", "low")
        }

/**
 * The refinement models Settings suggests, as API id to label, in the desktop's order. Any other
 * model id can be typed instead.
 */
val Provider.refinementModels: Map<String, String>
    get() =
        when (this) {
            Provider.ChatGpt ->
                mapOf(
                    "gpt-6-luna" to "GPT-6 Luna",
                    "gpt-6.1-sol" to "GPT-6.1 Sol",
                    "gpt-6-astra" to "GPT-6 Astra",
                    "gpt-5.6-luna" to "GPT-5.6 Luna",
                    "gpt-5.6-terra" to "GPT-5.6 Terra",
                    "gpt-5.5" to "GPT-5.5",
                    "gpt-5.4-nano" to "GPT-5.4 nano",
                    "gpt-5.4-mini" to "GPT-5.4 mini",
                    "gpt-5.4" to "GPT-5.4",
                )
            Provider.Claude ->
                mapOf(
                    "claude-opus-5-5" to "Claude Opus 5.5",
                    "claude-opus-5" to "Claude Opus 5",
                    "claude-sonnet-5-5" to "Claude Sonnet 5.5",
                    "claude-haiku-4-5" to "Claude Haiku 4.5",
                )
        }

/** The desktop's warning under a refinement model that may misread dictation, or null. */
fun refinementModelCaution(provider: Provider, model: String): String? =
    "Haiku may treat transcript as instructions."
        .takeIf { provider == Provider.Claude && model.lowercase().contains("haiku") }

/**
 * The efforts Settings offers, as API id to label, as on the desktop. OpenAI's `reasoning.effort`
 * can turn reasoning off and goes as chosen, so a model may reject one, except that GPT-6.1 Sol,
 * which refuses None, gets Low; Anthropic's `output_config.effort` starts at low, and a model
 * without the chosen one gets the nearest it has.
 */
val Provider.refinementEfforts: Map<String, String>
    get() =
        when (this) {
            Provider.ChatGpt ->
                mapOf(
                    "none" to "None",
                    "low" to "Low",
                    "medium" to "Medium",
                    "high" to "High",
                    "xhigh" to "Extra high",
                )
            Provider.Claude ->
                mapOf(
                    "low" to "Low",
                    "medium" to "Medium",
                    "high" to "High",
                    "xhigh" to "Extra high",
                    "max" to "Max",
                )
        }

/** How fast ChatGPT refines; the faster tiers use more of the plan's usage. */
enum class OpenAiSpeed(val label: String) {
    Standard("Standard"),
    Fast("Fast"),
    /** Only for models [app.speecher.protocol.modelSupportsUltrafast] names; Fast on the rest. */
    Ultrafast("Ultrafast"),
}

enum class InsertAction {
    Insert,
    InsertRefined,
}

/**
 * Which Insert buttons the panel offers beside Cancel, left to right; the last is the filled one.
 */
enum class ButtonLayout(val actions: List<InsertAction>) {
    /** A prominent Insert refined with a smaller Insert. */
    RefinedPrimary(listOf(InsertAction.Insert, InsertAction.InsertRefined)),
    InsertOnly(listOf(InsertAction.Insert)),
    RefinedOnly(listOf(InsertAction.InsertRefined)),
}

/** How much of the screen the dictation panel covers. */
enum class PanelSize {
    Full,
    Compact,
    /** A thin bar: recording dot, waveform, the newest words and the primary Insert. */
    Minimized,
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

/** Everything the user can change in Settings. */
data class SpeecherSettings(
    val transcriptionProvider: SpeechProvider = providerOrder.first().speech,
    val refinementEnabled: Boolean = true,
    val refinementProvider: CleanupProvider = providerOrder.first().cleanup,
    /**
     * Whether both Insert buttons re-transcribe ChatGPT dictation with GPT Transcribe first. On by
     * default, as on the desktop: it is more accurate, though slower and an extra request.
     */
    val transcribePassEnabled: Boolean = true,
    /**
     * The server [SpeechProvider.Endpoint] sends recordings to. Its key is kept encrypted apart.
     */
    val speechEndpoint: SpeechEndpoint = SpeechEndpoint(),
    /**
     * The language code the speech service listens for, or
     * [app.speecher.protocol.AUTOMATIC_LANGUAGE] to have it detect the language.
     */
    val spokenLanguage: String = ENGLISH_LANGUAGE,
    val chatGptRefinement: RefinementChoice = Provider.ChatGpt.defaultRefinement,
    val claudeRefinement: RefinementChoice = Provider.Claude.defaultRefinement,
    /**
     * Ask each provider for its faster, slightly costlier tier; Fast by default, as on the desktop.
     */
    val chatGptSpeed: OpenAiSpeed = OpenAiSpeed.Fast,
    val claudeFastMode: Boolean = true,
    val vocabulary: List<VocabularyWord> = emptyList(),
    /** Spoken phrases and the exact text, or Snippet, each inserts; only rules that can apply. */
    val replacements: List<Replacement> = emptyList(),
    /** Place the chip on the keyboard's mic key; off uses the custom position below. */
    val chipDockOnMic: Boolean = true,
    /** The custom chip position, as a pixel offset from the keyboard's bottom-right corner. */
    val chipOffsetX: Int? = null,
    val chipOffsetY: Int? = null,
    /** Keep the display awake while a dictation is running. */
    val keepScreenOn: Boolean = true,
    /** Ask other apps to pause their media while the microphone is on, as on the desktop. */
    val pauseMedia: Boolean = true,
    /** Vibrate when dictation starts and stops, where the desktop plays sounds; off by default. */
    val vibrationEnabled: Boolean = false,
    /** Show the words in the panel as they are heard. */
    val transcriptionPreviewEnabled: Boolean = true,
    /** Show the cleaned-up text in the panel as it streams in. */
    val refinementPreviewEnabled: Boolean = true,
    /**
     * Send the field's placeholder and the text around the caret to the refiner. The app's identity
     * and the field's kind go regardless, to pick the writing profile, as on the desktop.
     */
    val useTargetContext: Boolean = true,
    /** With context on, also read the target window's title and visible text at chip tap. */
    val includeScreenText: Boolean = false,
    /** With context on, also send a screenshot taken at chip tap, for vision models. */
    val includeScreenshot: Boolean = false,
    /** The profile used when the target app does not imply one. */
    val defaultWritingProfile: WritingProfile = WritingProfile.Other,
    /** The built-in profiles, then the custom ones in the order they were added. */
    val writingProfiles: Map<WritingProfile, WritingProfileSettings> =
        WritingProfile.entries.associateWith { WritingProfileSettings() },
    /** The user's application rules, checked before the built-in ones. */
    val appRules: List<RecognitionRule> = emptyList(),
    /** Added to every refinement prompt, before the profile's own instructions. */
    val additionalInstructions: String = "",
    /** Replaces the built-in dictation rules while on; empty stands for the built-in prompt. */
    val customSystemPromptEnabled: Boolean = false,
    val customSystemPrompt: String = "",
    /** Offered after the built-in tones and cleanup levels. */
    val customTones: List<CustomTone> = emptyList(),
    val customCleanupLevels: List<CustomCleanupLevel> = emptyList(),
    val buttonLayout: ButtonLayout = ButtonLayout.RefinedPrimary,
    /** The size the panel opens at. */
    val panelSize: PanelSize = PanelSize.Full,
    /** Whether the app checks for updates by itself while it is open. */
    val autoCheckUpdates: Boolean = true,
    val updateChannel: UpdateChannel = UpdateChannel.Stable,
    /** How often the app checks for an update while it is open. */
    val updateCheckMinutes: Int = IntervalUnit.Days.minutes,
    /** The unit a custom interval was entered in, or null for one of the presets. */
    val updateCheckUnit: IntervalUnit? = null,
    /**
     * Keep word counts, times and app names of each inserted dictation for Insights. Turning it off
     * stops recording; it does not delete what is kept.
     */
    val insightsEnabled: Boolean = true,
    /**
     * Watch the field after an insertion for the person's edit, and learn repeated corrections. On
     * by default, as on the desktop.
     */
    val correctionLearningEnabled: Boolean = true,
    /**
     * Newest first. They apply whether or not learning is on. [SettingsStore.save] leaves them as
     * stored, as the service may have learned one since; edit them with
     * [SettingsStore.editCorrections].
     */
    val learnedCorrections: List<LearnedCorrection> = emptyList(),
) {
    /** Whether dictation transcribes with a server of the person's own, which needs no account. */
    val transcribesWithServer: Boolean
        get() =
            transcriptionProvider == SpeechProvider.Endpoint && speechEndpoint.server.isNotEmpty()

    /**
     * The layout the panel shows. With refinement off there is nothing to refine, so only Insert.
     */
    val shownButtonLayout: ButtonLayout
        get() = if (refinementEnabled) buttonLayout else ButtonLayout.InsertOnly

    fun refinement(provider: Provider): RefinementChoice =
        if (provider == Provider.Claude) claudeRefinement else chatGptRefinement

    fun withRefinement(provider: Provider, choice: RefinementChoice): SpeecherSettings =
        if (provider == Provider.Claude) copy(claudeRefinement = choice)
        else copy(chatGptRefinement = choice)

    fun fastMode(provider: Provider): Boolean =
        if (provider == Provider.Claude) claudeFastMode else chatGptSpeed != OpenAiSpeed.Standard

    /**
     * With these tones and levels in place of the old ones. A profile that used one of those
     * deleted falls back to no tone override or Medium, as on the desktop.
     */
    fun withCustomChoices(
        tones: List<CustomTone> = customTones,
        levels: List<CustomCleanupLevel> = customCleanupLevels,
    ): SpeecherSettings =
        copy(
            customTones = tones,
            customCleanupLevels = levels,
            writingProfiles = writingProfiles.mapValues { it.value.withoutDeleted(tones, levels) },
        )

    /**
     * With these profiles in place of the old ones. A rule that pointed at a deleted profile loses
     * it, and is dropped when it sets no app type either; a fallback that named one becomes Other,
     * as on the desktop. A word limited to a deleted profile loses it, so a word left with none
     * applies to every profile.
     */
    fun withWritingProfiles(
        profiles: Map<WritingProfile, WritingProfileSettings> = writingProfiles
    ): SpeecherSettings =
        copy(
            writingProfiles = profiles,
            appRules =
                appRules
                    .map { if (it.profile in profiles) it else it.copy(profile = null) }
                    .filter { it.category != null || it.profile != null },
            defaultWritingProfile =
                defaultWritingProfile.takeIf { it in profiles } ?: WritingProfile.Other,
            vocabulary = vocabulary.map { it.copy(profiles = it.profiles intersect profiles.keys) },
        )
}

/**
 * What deleting [profile] changes, for the person deleting it to read first: how many rules lose
 * it, and whether the fallback becomes Other. Empty when nothing points at it. The desktop's
 * writingProfileDeletionNotice.
 */
fun SpeecherSettings.profileDeletionNotice(profile: WritingProfile): String {
    val rules = appRules.count { it.profile == profile }
    return listOfNotNull(
            when (rules) {
                0 -> null
                1 -> "1 application rule uses this profile and will lose it."
                else -> "$rules application rules use this profile and will lose it."
            },
            "The fallback profile will become Other.".takeIf { defaultWritingProfile == profile },
        )
        .joinToString(" ")
}

/**
 * What the setup checklist needs to know. Each flag is one step; the first is done by an account or
 * by [ownServer], a speech server of the person's own. [sessionEnded] holds the signed-in providers
 * whose sign-in was rejected during a dictation; they still count as signed in here.
 */
data class SetupStatus(
    val signedIn: Set<Provider>,
    val microphoneGranted: Boolean,
    val keyboardEnabled: Boolean,
    val chipEnabled: Boolean,
    val sessionEnded: Set<Provider> = emptySet(),
    val ownServer: Boolean = false,
) {
    val complete: Boolean
        get() =
            (signedIn.isNotEmpty() || ownServer) &&
                microphoneGranted &&
                keyboardEnabled &&
                chipEnabled

    /** The signed-in providers whose sign-in still works. */
    val working: Set<Provider>
        get() = signedIn - sessionEnded
}
