package app.speecher.android.dictation

import android.content.Context
import androidx.core.content.edit
import app.speecher.android.auth.TokenStore
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import kotlin.enums.enumEntries
import org.json.JSONArray
import org.json.JSONObject

class SettingsStore(private val context: Context) {
    private val preferences =
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE)

    fun load(): SpeecherSettings {
        // With nothing stored yet, fall back to the account the user is signed into rather than a
        // fixed provider, so neither Claude nor ChatGPT is favoured on a fresh install.
        val default = defaultProvider(TokenStore(context).signedIn())
        return SpeecherSettings(
                transcriptionProvider =
                    enumOf(preferences.getString("transcription", null), default),
                refinementEnabled = preferences.getBoolean("refinement", true),
                refinementProvider =
                    enumOf(preferences.getString("refinementProvider", null), default),
                transcribePassEnabled = preferences.getBoolean("transcribePass", true),
                chatGptRefinement = loadRefinement(Provider.ChatGpt),
                claudeRefinement = loadRefinement(Provider.Claude),
                chatGptFastMode = preferences.getBoolean("openAiFastMode", true),
                claudeFastMode = preferences.getBoolean("anthropicFastMode", true),
                vocabulary =
                    JSONArray(preferences.getString("vocabulary", "[]")).let { items ->
                        List(items.length()) { index -> items.getString(index) }
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
            Provider.entries.forEach { provider ->
                val choice = settings.refinement(provider)
                putString("${provider.name}RefinementModel", choice.model)
                putString("${provider.name}RefinementEffort", choice.effort)
            }
            putBoolean("openAiFastMode", settings.chatGptFastMode)
            putBoolean("anthropicFastMode", settings.claudeFastMode)
            putString("vocabulary", JSONArray(settings.vocabulary).toString())
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
        return RefinementChoice(
            preferences.getString("${provider.name}RefinementModel", default.model)!!,
            preferences.getString("${provider.name}RefinementEffort", default.effort)!!,
        )
    }

    private companion object {
        const val NO_OFFSET = Int.MIN_VALUE
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
