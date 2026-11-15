package app.speecher.android.settings

import app.speecher.protocol.AppCategory
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.RefinementRule
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
    fun `deleting a rule drops each profile's switch for it, and a switch for none loads without it`() {
        val kept = RefinementRule("custom_rule_kept", "Kept")
        val gone = RefinementRule("custom_rule_gone", "Gone")
        val switches = mapOf(kept.id to true, gone.id to true, "markdown" to false)
        val settings =
            SpeecherSettings(
                writingProfiles =
                    SpeecherSettings().writingProfiles +
                        (WritingProfile.Email to WritingProfileSettings(rules = switches)),
                refinementRules = listOf(kept, gone),
            )
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        store.save(settings.copy(refinementRules = listOf(kept)))
        assertEquals(
            mapOf(kept.id to true, "markdown" to false),
            store.load().writingProfiles.getValue(WritingProfile.Email).rules,
        )
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
            ),
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
}
