package app.speecher.android.dictation

import android.content.Context
import androidx.core.content.edit
import app.speecher.android.auth.TokenStore
import org.json.JSONArray

class SettingsStore(private val context: Context) {
    private val preferences =
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE)

    fun load(): SpeecherSettings {
        // With nothing stored yet, fall back to the account the user is signed into rather than a
        // fixed provider, so neither Claude nor ChatGPT is favoured on a fresh install.
        val default = defaultProvider(TokenStore(context).signedIn())
        return SpeecherSettings(
            transcriptionProvider =
                Provider.valueOf(preferences.getString("transcription", default.name)!!),
            refinementEnabled = preferences.getBoolean("refinement", true),
            refinementProvider =
                Provider.valueOf(preferences.getString("refinementProvider", default.name)!!),
            vocabulary =
                JSONArray(preferences.getString("vocabulary", "[]")).let { items ->
                    List(items.length()) { index -> items.getString(index) }
                },
            chipOffsetX = preferences.getInt("chipOffsetX", NO_OFFSET).takeIf { it != NO_OFFSET },
            chipOffsetY = preferences.getInt("chipOffsetY", NO_OFFSET).takeIf { it != NO_OFFSET },
        )
    }

    fun save(settings: SpeecherSettings) {
        preferences.edit(commit = true) {
            putString("transcription", settings.transcriptionProvider.name)
            putBoolean("refinement", settings.refinementEnabled)
            putString("refinementProvider", settings.refinementProvider.name)
            putString("vocabulary", JSONArray(settings.vocabulary).toString())
            settings.chipOffsetX?.let { putInt("chipOffsetX", it) } ?: remove("chipOffsetX")
            settings.chipOffsetY?.let { putInt("chipOffsetY", it) } ?: remove("chipOffsetY")
        }
    }

    private companion object {
        const val NO_OFFSET = Int.MIN_VALUE
    }
}
