package app.speecher.android.ui

import app.speecher.android.dictation.SpeecherSettings
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import org.junit.Assert.assertEquals
import org.junit.Test

class ProfileChoicesTest {
    @Test
    fun `profile choices list the built-ins, then the custom tones and levels`() {
        val settings =
            SpeecherSettings(
                customTones = listOf(CustomTone("custom_terse", "Terse", "Short.")),
                customCleanupLevels =
                    listOf(
                        CustomCleanupLevel("custom_notes", "Notes", CleanupStrength.Balanced, "")
                    ),
            )
        assertEquals(
            mapOf(
                "none" to "None",
                "light_cleanup" to "Light",
                "balanced" to "Medium",
                "strong_polish" to "High",
                "custom_notes" to "Notes",
            ),
            cleanupChoices(settings),
        )
        assertEquals(
            listOf(
                "none",
                "formal",
                "casual",
                "very_casual",
                "excited",
                "gen_z",
                "custom_terse",
            ),
            toneChoices(settings).keys.toList(),
        )
    }
}
