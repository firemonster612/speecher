package app.speecher.android.settings

import android.content.Context
import android.content.SharedPreferences
import android.content.SharedPreferences.OnSharedPreferenceChangeListener
import androidx.core.content.edit
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import app.speecher.android.auth.TokenStore
import app.speecher.android.provider.OpenAiSpeed
import app.speecher.android.provider.Provider
import app.speecher.android.provider.RefinementChoice
import app.speecher.android.provider.cleanup
import app.speecher.android.provider.defaultProvider
import app.speecher.android.provider.defaultRefinement
import app.speecher.android.provider.speech
import app.speecher.android.update.IntervalUnit
import app.speecher.android.update.UpdateChannel
import app.speecher.android.update.checkIntervalMinutes
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CorrectionEvidence
import app.speecher.protocol.Corrections
import app.speecher.protocol.DEFAULT_SPEECH_ENDPOINT_PATH
import app.speecher.protocol.ENGLISH_LANGUAGE
import app.speecher.protocol.LearnedCorrection
import app.speecher.protocol.PendingCorrection
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.SpeechEndpoint
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.normalizedVocabulary
import app.speecher.protocol.withEvidence
import app.speecher.protocol.withUsage
import java.util.UUID
import kotlin.enums.enumEntries
import kotlin.math.roundToInt
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.decodeFromJsonElement
import kotlinx.serialization.json.jsonArray

class SettingsStore(private val context: Context) {
    private val preferences =
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE)

    fun load(): SpeecherSettings {
        upgrade()
        // With nothing stored yet, fall back to the account the user is signed into rather than a
        // fixed provider, so neither Claude nor ChatGPT is favoured on a fresh install.
        val default = defaultProvider(TokenStore(context).signedIn())
        // A hand-edited zero would check without pause, and a negative crash the wait.
        val updateCheckMinutes =
            preferences
                .getInt("updateCheckMinutes", IntervalUnit.Days.minutes)
                .coerceIn(checkIntervalMinutes)
        return SpeecherSettings(
                transcriptionProvider =
                    enumOf(preferences.getString("transcription", null), default.speech),
                refinementEnabled = preferences.getBoolean("refinement", true),
                refinementProvider =
                    enumOf(preferences.getString("refinementProvider", null), default.cleanup),
                transcribePassEnabled = preferences.getBoolean("transcribePass", true),
                speechEndpoint =
                    SpeechEndpoint(
                        preferences.getString("speechEndpointUrl", "")!!,
                        preferences.getString("speechEndpointPath", DEFAULT_SPEECH_ENDPOINT_PATH)!!,
                        preferences.getString("speechEndpointModel", "")!!,
                    ),
                spokenLanguage = preferences.getString("spokenLanguage", ENGLISH_LANGUAGE)!!,
                chatGptRefinement = loadRefinement(Provider.ChatGpt),
                claudeRefinement = loadRefinement(Provider.Claude),
                // Before Speed there was only the fast-mode switch.
                chatGptSpeed =
                    enumOf(
                        preferences.getString("openAiSpeed", null),
                        if (preferences.getBoolean("openAiFastMode", true)) OpenAiSpeed.Fast
                        else OpenAiSpeed.Standard,
                    ),
                claudeFastMode = preferences.getBoolean("anthropicFastMode", true),
                vocabulary = loadVocabulary(),
                replacements = list("replacements"),
                chipDockOnMic = preferences.getBoolean("chipDockOnMic", true),
                chipOffsetX =
                    preferences.getInt("chipOffsetX", NO_OFFSET).takeIf { it != NO_OFFSET },
                chipOffsetY =
                    preferences.getInt("chipOffsetY", NO_OFFSET).takeIf { it != NO_OFFSET },
                keepScreenOn = preferences.getBoolean("keepScreenOn", true),
                pauseMedia = preferences.getBoolean("pauseMedia", true),
                vibrationEnabled = preferences.getBoolean("vibrationEnabled", false),
                transcriptionPreviewEnabled =
                    preferences.getBoolean("transcriptionPreviewEnabled", true),
                refinementPreviewEnabled = preferences.getBoolean("refinementPreviewEnabled", true),
                useTargetContext = preferences.getBoolean("useTargetContext", true),
                includeScreenText = preferences.getBoolean("includeScreenText", false),
                includeScreenshot = preferences.getBoolean("includeScreenshot", false),
                defaultWritingProfile =
                    profileOf(preferences.getString("defaultWritingProfile", null)),
                writingProfiles =
                    (WritingProfile.entries + customProfileIds()).associateWith { profile ->
                        val key = profile.key
                        val default = WritingProfileSettings()
                        WritingProfileSettings(
                            enumOf(
                                preferences.getString("${key}Cleanup", null),
                                default.cleanupStrength,
                            ),
                            enumOf(preferences.getString("${key}Tone", null), default.tone),
                            preferences.getString("${key}Instructions", "")!!,
                            preferences.getString("${key}CustomCleanup", null),
                            preferences.getString("${key}CustomTone", null),
                            preferences.getString("${key}Name", "")!!,
                            preferences.getString("${key}OutputLanguage", "")!!,
                        )
                    },
                appRules = list<StoredRule>("appRules").map { it.rule },
                additionalInstructions = preferences.getString("additionalInstructions", "")!!,
                customSystemPromptEnabled =
                    preferences.getBoolean("customSystemPromptEnabled", false),
                customSystemPrompt = preferences.getString("customSystemPrompt", "")!!,
                customTones = list("customTones"),
                customCleanupLevels = list("customCleanupLevels"),
                buttonLayout =
                    enumOf(
                        preferences.getString("buttonLayout", null),
                        ButtonLayout.RefinedPrimary,
                    ),
                panelSize = enumOf(preferences.getString("panelSize", null), PanelSize.Full),
                autoCheckUpdates = preferences.getBoolean("autoCheckUpdates", true),
                updateChannel =
                    enumOf(preferences.getString("updateChannel", null), UpdateChannel.Stable),
                updateCheckMinutes = updateCheckMinutes,
                // Only a unit the interval is a whole number of, or 5 minutes would read "0 days".
                updateCheckUnit =
                    preferences
                        .getString("updateCheckUnit", null)
                        ?.let { name -> IntervalUnit.entries.firstOrNull { it.name == name } }
                        ?.takeIf { updateCheckMinutes % it.minutes == 0 },
                insightsEnabled = insightsEnabled(),
                correctionLearningEnabled = correctionLearningEnabled(),
                learnedCorrections = loadCorrections().learned,
            )
            // So no profile names a tone or level that is gone, and no rule a profile.
            .withCustomChoices()
            .withWritingProfiles()
    }

    /** Just [SpeecherSettings.insightsEnabled], read without loading the rest. */
    fun insightsEnabled(): Boolean = preferences.getBoolean("insightsEnabled", true)

    fun save(settings: SpeecherSettings) {
        val deletedProfiles = customProfileIds() - settings.writingProfiles.keys
        preferences.edit(commit = true) {
            deletedProfiles.forEach { profile ->
                profileFields.forEach { field -> remove("${profile.key}$field") }
            }
            putString("transcription", settings.transcriptionProvider.name)
            putBoolean("refinement", settings.refinementEnabled)
            putString("refinementProvider", settings.refinementProvider.name)
            putBoolean("transcribePass", settings.transcribePassEnabled)
            putString("speechEndpointUrl", settings.speechEndpoint.baseUrl)
            putString("speechEndpointPath", settings.speechEndpoint.path)
            putString("speechEndpointModel", settings.speechEndpoint.model)
            putString("spokenLanguage", settings.spokenLanguage)
            Provider.entries.forEach { provider ->
                val choice = settings.refinement(provider)
                putString("${provider.name}RefinementModel", choice.model)
                putString("${provider.name}RefinementEffort", choice.effort)
            }
            putString("openAiSpeed", settings.chatGptSpeed.name)
            putBoolean("anthropicFastMode", settings.claudeFastMode)
            putList("vocabulary", settings.vocabulary)
            putList("replacements", settings.replacements)
            putBoolean("chipDockOnMic", settings.chipDockOnMic)
            settings.chipOffsetX?.let { putInt("chipOffsetX", it) } ?: remove("chipOffsetX")
            settings.chipOffsetY?.let { putInt("chipOffsetY", it) } ?: remove("chipOffsetY")
            putBoolean("keepScreenOn", settings.keepScreenOn)
            putBoolean("pauseMedia", settings.pauseMedia)
            putBoolean("vibrationEnabled", settings.vibrationEnabled)
            putBoolean("transcriptionPreviewEnabled", settings.transcriptionPreviewEnabled)
            putBoolean("refinementPreviewEnabled", settings.refinementPreviewEnabled)
            putBoolean("useTargetContext", settings.useTargetContext)
            putBoolean("includeScreenText", settings.includeScreenText)
            putBoolean("includeScreenshot", settings.includeScreenshot)
            putString("defaultWritingProfile", settings.defaultWritingProfile.key)
            settings.writingProfiles.forEach { (profile, choice) ->
                val key = profile.key
                putString("${key}Cleanup", choice.cleanupStrength.name)
                putString("${key}Tone", choice.tone.name)
                putString("${key}Instructions", choice.instructions)
                putString("${key}CustomCleanup", choice.customCleanupLevel)
                putString("${key}CustomTone", choice.customTone)
                putString("${key}OutputLanguage", choice.outputLanguage)
                if (!profile.isBuiltIn) putString("${key}Name", choice.name)
            }
            putList("customProfiles", settings.writingProfiles.keys.filterNot { it.isBuiltIn })
            putList("appRules", settings.appRules.map(::StoredRule))
            putString("additionalInstructions", settings.additionalInstructions)
            putBoolean("customSystemPromptEnabled", settings.customSystemPromptEnabled)
            putString("customSystemPrompt", settings.customSystemPrompt)
            putList("customTones", settings.customTones)
            putList("customCleanupLevels", settings.customCleanupLevels)
            putString("buttonLayout", settings.buttonLayout.name)
            putString("panelSize", settings.panelSize.name)
            putBoolean("autoCheckUpdates", settings.autoCheckUpdates)
            putString("updateChannel", settings.updateChannel.name)
            putInt("updateCheckMinutes", settings.updateCheckMinutes)
            settings.updateCheckUnit?.let { putString("updateCheckUnit", it.name) }
                ?: remove("updateCheckUnit")
            putBoolean("insightsEnabled", settings.insightsEnabled)
            putBoolean("correctionLearning", settings.correctionLearningEnabled)
            putInt("version", VERSION)
        }
    }

    /**
     * Calls [onChange] on the main thread whenever a setting is stored, by this store or another,
     * until [lifecycle] is destroyed. The dictation engine stores use counts while a screen of the
     * app may be open beside the target, and that screen saving the copy it loaded earlier would
     * wipe them.
     */
    fun observe(lifecycle: Lifecycle, onChange: () -> Unit) {
        val listener = OnSharedPreferenceChangeListener { _, _ -> onChange() }
        // Preferences hold a listener weakly; the lifecycle observer keeps this one.
        preferences.registerOnSharedPreferenceChangeListener(listener)
        lifecycle.addObserver(
            object : DefaultLifecycleObserver {
                override fun onDestroy(owner: LifecycleOwner) =
                    preferences.unregisterOnSharedPreferenceChangeListener(listener)
            }
        )
    }

    /**
     * Counts a use of each word [text] contains, as the desktop does for every dictation it
     * delivers. Writes only the vocabulary, leaving every other setting as stored.
     */
    fun recordVocabularyUsage(text: String, nowMs: Long = System.currentTimeMillis()) {
        val words = loadVocabulary()
        val used = withUsage(words, text, nowMs)
        if (used != words) preferences.edit { putList("vocabulary", normalizedVocabulary(used)) }
    }

    /**
     * Learns from [evidence] of an edit in the app [applicationId], unless learning is off. Writes
     * only the corrections and the edits waiting to be seen again, leaving every other setting as
     * stored.
     */
    fun recordCorrection(
        evidence: CorrectionEvidence,
        applicationId: String,
        nowMs: Long = System.currentTimeMillis(),
    ) {
        if (!correctionLearningEnabled()) return
        val corrections = loadCorrections()
        val next =
            withEvidence(corrections, evidence, applicationId, nowMs) {
                UUID.randomUUID().toString()
            }
        if (next != corrections)
            preferences.edit {
                putList("learnedCorrections", next.learned)
                putList("correctionEvidence", next.pending)
            }
    }

    /**
     * Applies [edit] to the stored learned corrections, newest first, rather than to a copy a
     * screen loaded earlier, which may lack one learned since.
     */
    fun editCorrections(edit: (List<LearnedCorrection>) -> List<LearnedCorrection>) {
        val learned = loadCorrections().learned
        preferences.edit { putList("learnedCorrections", edit(learned)) }
    }

    /** Just [SpeecherSettings.correctionLearningEnabled], read without loading the rest. */
    fun correctionLearningEnabled(): Boolean = preferences.getBoolean("correctionLearning", true)

    /**
     * Keeps what settings from an earlier release meant, once. That release placed the chip by the
     * corner of a 52 x 36 dp window, now 56 x 48 dp with the same pill centred, so a saved offset
     * moves up and left by half the difference to keep the pill put.
     */
    private fun upgrade() {
        if (preferences.getInt("version", 1) >= VERSION) return
        val density = context.resources.displayMetrics.density
        preferences.edit(commit = true) {
            if (preferences.contains("chipOffsetX") && preferences.contains("chipOffsetY")) {
                putInt(
                    "chipOffsetX",
                    preferences.getInt("chipOffsetX", 0) - (2 * density).roundToInt(),
                )
                putInt(
                    "chipOffsetY",
                    preferences.getInt("chipOffsetY", 0) - (6 * density).roundToInt(),
                )
            }
            putInt("version", VERSION)
        }
    }

    private fun loadVocabulary(): List<VocabularyWord> =
        json
            .parseToJsonElement(preferences.getString("vocabulary", "[]")!!)
            .jsonArray
            .map {
                // Earlier releases stored each word as its bare term.
                if (it is JsonPrimitive) VocabularyWord(it.content)
                else json.decodeFromJsonElement(it)
            }
            .let(::normalizedVocabulary)

    /**
     * The desktop's CorrectionSettingsCodec, with its keys; an entry missing its text is dropped.
     */
    private fun loadCorrections(): Corrections =
        Corrections(
            list<LearnedCorrection>("learnedCorrections").filter {
                it.id.isNotEmpty() && it.original.isNotBlank() && it.corrected.isNotBlank()
            },
            list<PendingCorrection>("correctionEvidence").filter {
                it.original.isNotEmpty() && it.corrected.isNotEmpty() && it.count > 0
            },
        )

    private fun customProfileIds(): List<WritingProfile> = list("customProfiles")

    private inline fun <reified T> list(key: String): List<T> =
        json.decodeFromString(preferences.getString(key, "[]")!!)

    private inline fun <reified T> SharedPreferences.Editor.putList(key: String, items: List<T>) {
        putString(key, json.encodeToString(items))
    }

    private fun loadRefinement(provider: Provider): RefinementChoice {
        val default = provider.defaultRefinement
        val model = preferences.getString("${provider.name}RefinementModel", default.model)!!
        return RefinementChoice(
            // A stored choice follows its model's replacement in the picker.
            replacedModels[model] ?: model,
            preferences.getString("${provider.name}RefinementEffort", default.effort)!!,
        )
    }

    private companion object {
        const val NO_OFFSET = Int.MIN_VALUE
        const val VERSION = 2
        /** What each Writing Profile stores, each under its key prefix. */
        val profileFields =
            listOf(
                "Cleanup",
                "Tone",
                "Instructions",
                "CustomCleanup",
                "CustomTone",
                "Name",
                "OutputLanguage",
            )

        /**
         * Every key stays in what is stored, so an earlier release reads it back. Keys it doesn't
         * know are skipped and an enum constant it doesn't know loads as the property's default, so
         * what a later release stored still loads.
         */
        val json = Json {
            encodeDefaults = true
            ignoreUnknownKeys = true
            coerceInputValues = true
        }
        val replacedModels =
            mapOf(
                "claude-sonnet-5" to "claude-sonnet-5-5",
                "gpt-6-sol" to "gpt-6.1-sol",
                "claude-haiku-4-5" to "claude-haiku-5-5",
                "claude-haiku-4-5-20251001" to "claude-haiku-5-5",
            )
    }
}

/** A [RecognitionRule] as stored, with an empty id for no app type or no profile. */
@Serializable
private data class StoredRule(val match: String, val category: String, val profile: String) {
    constructor(
        rule: RecognitionRule
    ) : this(rule.match, rule.category?.id.orEmpty(), rule.profile?.id.orEmpty())

    val rule: RecognitionRule
        get() =
            RecognitionRule(
                match,
                AppCategory.entries.firstOrNull { it.id == category },
                profile.takeIf(String::isNotEmpty)?.let(::WritingProfile),
            )
}

/**
 * The preference key prefix of a profile. The built-ins keep the enum constant names earlier
 * releases stored them under; a custom profile uses its id.
 */
private val WritingProfile.key: String
    get() = builtInKeys[this] ?: id

private val builtInKeys =
    mapOf(
        WritingProfile.Work to "Work",
        WritingProfile.Email to "Email",
        WritingProfile.Personal to "Personal",
        WritingProfile.AiCoding to "AiCoding",
        WritingProfile.Other to "Other",
    )

/** The profile a stored key names, or Other for none. */
private fun profileOf(key: String?): WritingProfile =
    builtInKeys.entries.firstOrNull { it.value == key }?.key
        ?: key?.takeIf { it.startsWith("custom_") }?.let(::WritingProfile)
        ?: WritingProfile.Other

/** The constant [name] names, or [default] when it is missing or a later release renamed it. */
private inline fun <reified E : Enum<E>> enumOf(name: String?, default: E): E =
    enumEntries<E>().firstOrNull { it.name == name } ?: default
