package app.speecher.android.settings

import android.content.Context
import androidx.core.content.edit
import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.SpeechProvider
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CorrectionEvidence
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import app.speecher.protocol.LearnedCorrection
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.Replacement
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(qualifiers = "xhdpi")
class SettingsUpgradeTest {
    @Test
    fun `a chip offset saved for the smaller chip window moves once to keep the pill in place`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true
        ) {
            putBoolean("chipDockOnMic", false)
            putInt("chipOffsetX", -200)
            putInt("chipOffsetY", -100)
        }
        val store = SettingsStore(context)
        store.load()
        val settings = store.load()
        assertEquals(-204, settings.chipOffsetX)
        assertEquals(-112, settings.chipOffsetY)
    }

    @Test
    fun `providers saved as accounts load as the same speech and cleanup choices`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true
        ) {
            putString("transcription", "Claude")
            putString("refinementProvider", "Claude")
        }
        val settings = SettingsStore(context).load()
        assertEquals(
            SpeechProvider.Claude to CleanupProvider.Claude,
            settings.transcriptionProvider to settings.refinementProvider,
        )
    }

    @Test
    fun `a model saved on Haiku 4_5 loads on Haiku 5_5, which replaced it`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true
        ) {
            putString("ClaudeRefinementModel", "claude-haiku-4-5-20251001")
        }
        assertEquals("claude-haiku-5-5", SettingsStore(context).load().claudeRefinement.model)
    }

    @Test
    fun `lists stored by the previous release load as they were`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true
        ) {
            putString(
                "vocabulary",
                """[{"term":"Speecher","context":"","profiles":[],"source":"manual",""" +
                    """"frequency":0,"lastUsedMs":0,"priority":true},""" +
                    """{"term":"Kubernetes","context":"The platform.","profiles":["work"],""" +
                    """"source":"csv","frequency":2,"lastUsedMs":1700,"keyTerm":false}]""",
            )
            putString("replacements", """[{"phrase":"sign off","text":"Regards,\nEfox"}]""")
            putString("customProfiles", """["custom_standup"]""")
            putString("custom_standupName", "Standup")
            putString(
                "appRules",
                """[{"match":"zulip","category":"","profile":"custom_standup"},""" +
                    """{"match":"mattermost","category":"browser","profile":""}]""",
            )
            putString(
                "customTones",
                """[{"id":"custom_terse","name":"Terse","instruction":"Short."}]""",
            )
            // Gentle stands for a base a later release added.
            putString(
                "customCleanupLevels",
                """[{"id":"custom_notes","name":"Notes","base":"CustomOnly",""" +
                    """"instructions":"Bullets."},{"id":"custom_soft","name":"Soft",""" +
                    """"base":"Gentle","instructions":"Softly."}]""",
            )
            putString(
                "learnedCorrections",
                """[{"id":"a1","original":"github","corrected":"GitHub",""" +
                    """"applicationId":"","createdAtMs":100,"confidence":0.98,"enabled":false,""" +
                    """"evidenceCount":3,"lastObservedAtMs":400}]""",
            )
            putString(
                "correctionEvidence",
                """[{"original":"speaker","corrected":"Speecher","applicationId":"com.Slack",""" +
                    """"count":1,"firstObservedAtMs":100,"lastObservedAtMs":100,"confidence":0.75}]""",
            )
        }
        val store = SettingsStore(context)
        val settings = store.load()
        val standup = WritingProfile("custom_standup")
        assertEquals(
            listOf(
                VocabularyWord("Speecher", priority = true),
                VocabularyWord(
                    "Kubernetes",
                    "The platform.",
                    setOf(WritingProfile.Work),
                    keyTerm = false,
                    source = "csv",
                    frequency = 2,
                    lastUsedMs = 1700,
                ),
            ),
            settings.vocabulary,
        )
        assertEquals(listOf(Replacement("sign off", "Regards,\nEfox")), settings.replacements)
        assertEquals("Standup", settings.writingProfiles.getValue(standup).name)
        assertEquals(
            listOf(
                RecognitionRule("zulip", null, standup),
                RecognitionRule("mattermost", AppCategory.Browser, null),
            ),
            settings.appRules,
        )
        assertEquals(listOf(CustomTone("custom_terse", "Terse", "Short.")), settings.customTones)
        assertEquals(
            listOf(
                CustomCleanupLevel("custom_notes", "Notes", CleanupStrength.CustomOnly, "Bullets."),
                CustomCleanupLevel("custom_soft", "Soft", CleanupStrength.Balanced, "Softly."),
            ),
            settings.customCleanupLevels,
        )
        assertEquals(
            listOf(LearnedCorrection("a1", "github", "GitHub", "", 100, 0.98, false, 3, 400)),
            settings.learnedCorrections,
        )

        // The stored edit was seen once; seeing it again learns it, from its first sighting.
        store.recordCorrection(CorrectionEvidence("speaker", "Speecher", 0.75), "com.Slack", 200)
        assertEquals(
            LearnedCorrection("", "speaker", "Speecher", "com.Slack", 100, 0.75, true, 2, 200),
            store.load().learnedCorrections.first().copy(id = ""),
        )
    }
}
