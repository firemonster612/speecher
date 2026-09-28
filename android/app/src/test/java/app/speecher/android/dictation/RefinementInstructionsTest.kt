package app.speecher.android.dictation

import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class RefinementInstructionsTest {
    @Test
    fun `instructions and the custom prompt persist, and start empty and off`() {
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        assertEquals(SpeecherSettings().writingProfiles, store.load().writingProfiles)
        assertEquals(listOf("", "false", ""), store.load().let(::instructionFields))
        val saved =
            SpeecherSettings(
                writingProfiles =
                    SpeecherSettings().writingProfiles +
                        (WritingProfile.Email to
                            WritingProfileSettings(instructions = "Sign off with Best,\nEnzo")),
                additionalInstructions = "Spell it Speecher.",
                customSystemPromptEnabled = true,
                customSystemPrompt = "Clean up my dictation.",
            )
        store.save(saved)
        val loaded = store.load()
        assertEquals(
            "Sign off with Best,\nEnzo",
            loaded.writingProfiles.getValue(WritingProfile.Email).instructions,
        )
        assertEquals(
            listOf("Spell it Speecher.", "true", "Clean up my dictation."),
            instructionFields(loaded),
        )
    }

    private fun instructionFields(settings: SpeecherSettings) =
        listOf(
            settings.additionalInstructions,
            settings.customSystemPromptEnabled.toString(),
            settings.customSystemPrompt,
        )
}
