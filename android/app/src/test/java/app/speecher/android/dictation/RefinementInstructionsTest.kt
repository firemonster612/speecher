package app.speecher.android.dictation

import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.cleanupLevelId
import app.speecher.protocol.toneId
import app.speecher.protocol.withCleanupLevel
import app.speecher.protocol.withTone
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

    @Test
    fun `profiles keep custom tones and levels until they are deleted`() {
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        val tone = CustomTone("custom_terse", "Terse", "Short.")
        val level =
            CustomCleanupLevel("custom_notes", "Notes", CleanupStrength.CustomOnly, "Bullets.")
        val chosen = WritingProfileSettings().withCleanupLevel(level.id).withTone(tone.id)
        store.save(
            SpeecherSettings(
                customTones = listOf(tone),
                customCleanupLevels = listOf(level),
                writingProfiles =
                    SpeecherSettings().writingProfiles + (WritingProfile.Email to chosen),
            )
        )
        val loaded = store.load()
        assertEquals(listOf(tone), loaded.customTones)
        assertEquals(listOf(level), loaded.customCleanupLevels)
        val email = loaded.writingProfiles.getValue(WritingProfile.Email)
        assertEquals(
            listOf("custom_notes", "custom_terse"),
            listOf(email.cleanupLevelId, email.toneId),
        )

        val deleted =
            loaded
                .withCustomChoices(tones = emptyList(), levels = emptyList())
                .writingProfiles
                .getValue(WritingProfile.Email)
        assertEquals(listOf("balanced", "none"), listOf(deleted.cleanupLevelId, deleted.toneId))
    }

    @Test
    fun `deleting a profile clears its rules and the fallback, and says so first`() {
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        val standup = WritingProfile("custom_standup")
        store.save(
            SpeecherSettings(
                writingProfiles =
                    SpeecherSettings().writingProfiles +
                        (standup to WritingProfileSettings(name = "Standup")),
                defaultWritingProfile = standup,
                appRules =
                    listOf(
                        RecognitionRule("zulip", null, standup),
                        RecognitionRule("mattermost", AppCategory.Browser, standup),
                    ),
            )
        )
        val loaded = store.load()
        assertEquals("Standup", loaded.writingProfiles.getValue(standup).name)
        assertEquals(standup, loaded.defaultWritingProfile)
        assertEquals(
            "2 application rules use this profile and will lose it. " +
                "The fallback profile will become Other.",
            loaded.profileDeletionNotice(standup),
        )

        val deleted = loaded.withWritingProfiles(loaded.writingProfiles - standup)
        assertEquals(
            listOf(RecognitionRule("mattermost", AppCategory.Browser, null)),
            deleted.appRules,
        )
        assertEquals(WritingProfile.Other, deleted.defaultWritingProfile)
        assertEquals("", deleted.profileDeletionNotice(standup))
    }

    private fun instructionFields(settings: SpeecherSettings) =
        listOf(
            settings.additionalInstructions,
            settings.customSystemPromptEnabled.toString(),
            settings.customSystemPrompt,
        )
}
