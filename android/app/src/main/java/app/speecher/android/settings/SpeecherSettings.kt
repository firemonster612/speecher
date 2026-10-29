package app.speecher.android.settings

import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.OpenAiSpeed
import app.speecher.android.provider.Provider
import app.speecher.android.provider.RefinementChoice
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.provider.cleanup
import app.speecher.android.provider.defaultRefinement
import app.speecher.android.provider.providerOrder
import app.speecher.android.provider.speech
import app.speecher.android.update.IntervalUnit
import app.speecher.android.update.UpdateChannel
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import app.speecher.protocol.ENGLISH_LANGUAGE
import app.speecher.protocol.LearnedCorrection
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.Replacement
import app.speecher.protocol.SpeechEndpoint
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.withoutDeleted

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
