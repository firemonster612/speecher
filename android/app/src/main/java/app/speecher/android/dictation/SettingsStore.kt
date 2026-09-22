package app.speecher.android.dictation

import android.content.Context
import androidx.core.content.edit
import org.json.JSONArray

class SettingsStore(context: Context) {
    private val preferences =
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE)

    fun load(): SpeecherSettings =
        SpeecherSettings(
            transcriptionProvider =
                Provider.valueOf(preferences.getString("transcription", Provider.Claude.name)!!),
            refinementEnabled = preferences.getBoolean("refinement", true),
            refinementProvider =
                Provider.valueOf(
                    preferences.getString("refinementProvider", Provider.Claude.name)!!
                ),
            vocabulary =
                JSONArray(preferences.getString("vocabulary", "[]")).let { items ->
                    List(items.length()) { index -> items.getString(index) }
                },
        )

    fun save(settings: SpeecherSettings) {
        preferences.edit(commit = true) {
            putString("transcription", settings.transcriptionProvider.name)
            putBoolean("refinement", settings.refinementEnabled)
            putString("refinementProvider", settings.refinementProvider.name)
            putString("vocabulary", JSONArray(settings.vocabulary).toString())
        }
    }
}
