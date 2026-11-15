package app.speecher.android.ui

import app.speecher.android.settings.SpeecherSettings
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.builtInRefinementRules
import org.junit.Assert.assertEquals
import org.junit.Test

class ProfileChoicesTest {
    @Test
    fun `every built-in rule has a label, in the rules' order`() {
        assertEquals(builtInRefinementRules.map { it.id }, builtInRuleLabels.keys.toList())
    }

    @Test
    fun `profile choices list the built-ins, then the custom profiles`() {
        val settings =
            SpeecherSettings(
                writingProfiles =
                    SpeecherSettings().writingProfiles +
                        (WritingProfile("custom_standup") to
                            WritingProfileSettings(name = "Standup")),
            )
        assertEquals(
            listOf("Work", "Email", "Personal", "AI coding", "Other", "Standup"),
            profileChoices(settings).values.toList(),
        )
    }
}
