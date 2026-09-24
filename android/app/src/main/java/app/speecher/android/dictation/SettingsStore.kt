package app.speecher.android.dictation

import android.content.Context
import androidx.core.content.edit
import app.speecher.android.auth.TokenStore
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import kotlin.enums.enumEntries
import org.json.JSONArray

class SettingsStore(private val context: Context) {
    private val preferences =
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE)

    fun load(): SpeecherSettings {
        // With nothing stored yet, fall back to the account the user is signed into rather than a
        // fixed provider, so neither Claude nor ChatGPT is favoured on a fresh install.
        val default = defaultProvider(TokenStore(context).signedIn())
        return SpeecherSettings(
            transcriptionProvider = enumOf(preferences.getString("transcription", null), default),
            refinementEnabled = preferences.getBoolean("refinement", true),
            refinementProvider = enumOf(preferences.getString("refinementProvider", null), default),
            transcribePassEnabled = preferences.getBoolean("transcribePass", true),
            chatGptRefinement = loadRefinement(Provider.ChatGpt),
            claudeRefinement = loadRefinement(Provider.Claude),
            vocabulary =
                JSONArray(preferences.getString("vocabulary", "[]")).let { items ->
                    List(items.length()) { index -> items.getString(index) }
                },
            chipDockOnMic = preferences.getBoolean("chipDockOnMic", true),
            chipOffsetX = preferences.getInt("chipOffsetX", NO_OFFSET).takeIf { it != NO_OFFSET },
            chipOffsetY = preferences.getInt("chipOffsetY", NO_OFFSET).takeIf { it != NO_OFFSET },
            keepScreenOn = preferences.getBoolean("keepScreenOn", true),
            useTargetContext = preferences.getBoolean("useTargetContext", true),
            includeScreenText = preferences.getBoolean("includeScreenText", false),
            includeScreenshot = preferences.getBoolean("includeScreenshot", false),
            defaultWritingProfile =
                enumOf(preferences.getString("defaultWritingProfile", null), WritingProfile.Other),
            writingProfiles =
                WritingProfile.entries.associateWith { profile ->
                    val default = WritingProfileSettings()
                    WritingProfileSettings(
                        enumOf(
                            preferences.getString("${profile.name}Cleanup", null),
                            default.cleanupStrength,
                        ),
                        enumOf(preferences.getString("${profile.name}Tone", null), default.tone),
                    )
                },
        )
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
            putString("vocabulary", JSONArray(settings.vocabulary).toString())
            putBoolean("chipDockOnMic", settings.chipDockOnMic)
            settings.chipOffsetX?.let { putInt("chipOffsetX", it) } ?: remove("chipOffsetX")
            settings.chipOffsetY?.let { putInt("chipOffsetY", it) } ?: remove("chipOffsetY")
            putBoolean("keepScreenOn", settings.keepScreenOn)
            putBoolean("useTargetContext", settings.useTargetContext)
            putBoolean("includeScreenText", settings.includeScreenText)
            putBoolean("includeScreenshot", settings.includeScreenshot)
            putString("defaultWritingProfile", settings.defaultWritingProfile.name)
            settings.writingProfiles.forEach { (profile, choice) ->
                putString("${profile.name}Cleanup", choice.cleanupStrength.name)
                putString("${profile.name}Tone", choice.tone.name)
            }
        }
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

/** The constant [name] names, or [default] when it is missing or a later release renamed it. */
private inline fun <reified E : Enum<E>> enumOf(name: String?, default: E): E =
    enumEntries<E>().firstOrNull { it.name == name } ?: default
