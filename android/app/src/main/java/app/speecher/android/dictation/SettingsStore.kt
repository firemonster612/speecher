package app.speecher.android.dictation

import android.content.Context
import androidx.core.content.edit
import app.speecher.android.auth.TokenStore
import app.speecher.android.update.IntervalUnit
import app.speecher.android.update.checkIntervalMinutes
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import app.speecher.protocol.ENGLISH_LANGUAGE
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import kotlin.enums.enumEntries
import kotlin.math.roundToInt
import org.json.JSONArray
import org.json.JSONObject

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
                    enumOf(preferences.getString("transcription", null), default),
                refinementEnabled = preferences.getBoolean("refinement", true),
                refinementProvider =
                    enumOf(preferences.getString("refinementProvider", null), default),
                transcribePassEnabled = preferences.getBoolean("transcribePass", true),
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
                vocabulary =
                    JSONArray(preferences.getString("vocabulary", "[]")).let { items ->
                        // Earlier releases stored each word as its bare term.
                        List(items.length()) { index ->
                            val word = items.optJSONObject(index)
                            if (word == null) VocabularyWord(items.getString(index))
                            else
                                VocabularyWord(
                                    word.getString("term"),
                                    word.optString("context"),
                                    (word.optJSONArray("profiles") ?: JSONArray()).let { ids ->
                                        List(ids.length()) { WritingProfile(ids.getString(it)) }
                                            .toSet()
                                    },
                                    // Stored only when off, so every earlier word is a key term.
                                    word.optBoolean("keyTerm", true),
                                    word.optBoolean("priority"),
                                )
                        }
                    },
                chipDockOnMic = preferences.getBoolean("chipDockOnMic", true),
                chipOffsetX =
                    preferences.getInt("chipOffsetX", NO_OFFSET).takeIf { it != NO_OFFSET },
                chipOffsetY =
                    preferences.getInt("chipOffsetY", NO_OFFSET).takeIf { it != NO_OFFSET },
                keepScreenOn = preferences.getBoolean("keepScreenOn", true),
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
                appRules =
                    objects("appRules").map {
                        RecognitionRule(
                            it.getString("match"),
                            AppCategory.entries.firstOrNull { category ->
                                category.id == it.optString("category")
                            },
                            it.optString("profile")
                                .takeIf(String::isNotEmpty)
                                ?.let(::WritingProfile),
                        )
                    },
                additionalInstructions = preferences.getString("additionalInstructions", "")!!,
                customSystemPromptEnabled =
                    preferences.getBoolean("customSystemPromptEnabled", false),
                customSystemPrompt = preferences.getString("customSystemPrompt", "")!!,
                customTones =
                    objects("customTones").map {
                        CustomTone(
                            it.getString("id"),
                            it.getString("name"),
                            it.getString("instruction"),
                        )
                    },
                customCleanupLevels =
                    objects("customCleanupLevels").map {
                        CustomCleanupLevel(
                            it.getString("id"),
                            it.getString("name"),
                            enumOf(it.getString("base"), CleanupStrength.Balanced),
                            it.getString("instructions"),
                        )
                    },
                buttonLayout =
                    enumOf(
                        preferences.getString("buttonLayout", null),
                        ButtonLayout.RefinedPrimary,
                    ),
                panelSize = enumOf(preferences.getString("panelSize", null), PanelSize.Full),
                updateCheckMinutes = updateCheckMinutes,
                // Only a unit the interval is a whole number of, or 5 minutes would read "0 days".
                updateCheckUnit =
                    preferences
                        .getString("updateCheckUnit", null)
                        ?.let { name -> IntervalUnit.entries.firstOrNull { it.name == name } }
                        ?.takeIf { updateCheckMinutes % it.minutes == 0 },
            )
            // So no profile names a tone or level that is gone, and no rule a profile.
            .withCustomChoices()
            .withWritingProfiles()
    }

    fun save(settings: SpeecherSettings) {
        preferences.edit(commit = true) {
            putString("transcription", settings.transcriptionProvider.name)
            putBoolean("refinement", settings.refinementEnabled)
            putString("refinementProvider", settings.refinementProvider.name)
            putBoolean("transcribePass", settings.transcribePassEnabled)
            putString("spokenLanguage", settings.spokenLanguage)
            Provider.entries.forEach { provider ->
                val choice = settings.refinement(provider)
                putString("${provider.name}RefinementModel", choice.model)
                putString("${provider.name}RefinementEffort", choice.effort)
            }
            putString("openAiSpeed", settings.chatGptSpeed.name)
            putBoolean("anthropicFastMode", settings.claudeFastMode)
            putString(
                "vocabulary",
                JSONArray(
                        settings.vocabulary.map { word ->
                            JSONObject(
                                    mapOf(
                                        "term" to word.term,
                                        "context" to word.context,
                                        "profiles" to JSONArray(word.profiles.map { it.id }),
                                    )
                                )
                                .apply {
                                    if (!word.keyTerm) put("keyTerm", false)
                                    if (word.priority) put("priority", true)
                                }
                        }
                    )
                    .toString(),
            )
            putBoolean("chipDockOnMic", settings.chipDockOnMic)
            settings.chipOffsetX?.let { putInt("chipOffsetX", it) } ?: remove("chipOffsetX")
            settings.chipOffsetY?.let { putInt("chipOffsetY", it) } ?: remove("chipOffsetY")
            putBoolean("keepScreenOn", settings.keepScreenOn)
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
            putString(
                "customProfiles",
                JSONArray(settings.writingProfiles.keys.filterNot { it.isBuiltIn }.map { it.id })
                    .toString(),
            )
            putString(
                "appRules",
                JSONArray(
                        settings.appRules.map {
                            JSONObject(
                                mapOf(
                                    "match" to it.match,
                                    "category" to it.category?.id.orEmpty(),
                                    "profile" to it.profile?.id.orEmpty(),
                                )
                            )
                        }
                    )
                    .toString(),
            )
            putString("additionalInstructions", settings.additionalInstructions)
            putBoolean("customSystemPromptEnabled", settings.customSystemPromptEnabled)
            putString("customSystemPrompt", settings.customSystemPrompt)
            putString(
                "customTones",
                JSONArray(
                        settings.customTones.map {
                            JSONObject(
                                mapOf(
                                    "id" to it.id,
                                    "name" to it.name,
                                    "instruction" to it.instruction,
                                )
                            )
                        }
                    )
                    .toString(),
            )
            putString(
                "customCleanupLevels",
                JSONArray(
                        settings.customCleanupLevels.map {
                            JSONObject(
                                mapOf(
                                    "id" to it.id,
                                    "name" to it.name,
                                    "base" to it.base.name,
                                    "instructions" to it.instructions,
                                )
                            )
                        }
                    )
                    .toString(),
            )
            putString("buttonLayout", settings.buttonLayout.name)
            putString("panelSize", settings.panelSize.name)
            putInt("updateCheckMinutes", settings.updateCheckMinutes)
            settings.updateCheckUnit?.let { putString("updateCheckUnit", it.name) }
                ?: remove("updateCheckUnit")
            putInt("version", VERSION)
        }
    }

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

    private fun customProfileIds(): List<WritingProfile> =
        JSONArray(preferences.getString("customProfiles", "[]")).let { items ->
            List(items.length()) { index -> WritingProfile(items.getString(index)) }
        }

    private fun objects(key: String): List<JSONObject> =
        JSONArray(preferences.getString(key, "[]")).let { items ->
            List(items.length()) { index -> items.getJSONObject(index) }
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
        val replacedModels =
            mapOf("claude-sonnet-5" to "claude-sonnet-5-5", "gpt-6-sol" to "gpt-6.1-sol")
    }
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
