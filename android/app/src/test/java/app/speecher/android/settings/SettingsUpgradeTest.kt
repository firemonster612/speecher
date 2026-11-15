package app.speecher.android.settings

import android.content.Context
import androidx.core.content.edit
import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.SpeechProvider
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CorrectionEvidence
import app.speecher.protocol.LearnedCorrection
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.RefinementRule
import app.speecher.protocol.Replacement
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.resolveRefinementContext
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
            commit = true,
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
            commit = true,
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
            commit = true,
        ) {
            putString("ClaudeRefinementModel", "claude-haiku-4-5-20251001")
        }
        assertEquals("claude-haiku-5-5", SettingsStore(context).load().claudeRefinement.model)
    }

    @Test
    fun `instructions, custom tones and custom levels become rules on where they were used, once`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true,
        ) {
            putInt("version", 2)
            putString("additionalInstructions", "Spell it Speecher.")
            putString("WorkInstructions", "Use the team glossary.")
            putString("OtherInstructions", " ")
            putString("customProfiles", """["custom_lab"]""")
            putString("custom_labName", "Lab notes")
            putString("custom_labInstructions", "Write lab notes.")
            putString("custom_labCleanup", "Balanced")
            putString("custom_labCustomCleanup", "custom_notes")
            putString("custom_labCustomTone", "custom_pirate")
            putString("EmailCustomCleanup", "custom_soft")
            putString(
                "customTones",
                """[{"id":"custom_pirate","name":"Pirate","instruction":"Talk like a pirate."}]""",
            )
            // The second level's name is taken by the Work profile's rule.
            putString(
                "customCleanupLevels",
                """[{"id":"custom_notes","name":"Notes","base":"CustomOnly",""" +
                    """"instructions":"Bullets."},{"id":"custom_soft",""" +
                    """"name":"work instructions","base":"StrongPolish","instructions":"Softly."}]""",
            )
        }
        val store = SettingsStore(context)
        val settings = store.load()
        val rules = settings.refinementRules
        // Each id names its source, as the desktop's migration names it.
        assertEquals(
            listOf(
                RefinementRule(
                    "custom_rule_every_profile",
                    "Instructions for every profile",
                    "Spell it Speecher.",
                    true,
                ),
                RefinementRule(
                    "custom_rule_work_instructions",
                    "Work instructions",
                    "Use the team glossary.",
                ),
                RefinementRule(
                    "custom_rule_custom_lab_instructions",
                    "Lab notes instructions",
                    "Write lab notes.",
                ),
                RefinementRule(
                    "custom_rule_custom_pirate",
                    "Pirate tone",
                    "Write in this tone: Pirate. Talk like a pirate.",
                ),
                RefinementRule("custom_rule_custom_notes", "Notes", "Bullets."),
                RefinementRule("custom_rule_custom_soft", "work instructions 2", "Softly."),
            ),
            rules,
        )
        assertEquals(
            SpeecherSettings().writingProfiles +
                mapOf(
                    WritingProfile.Work to
                        WritingProfileSettings(
                            rules = mapOf("custom_rule_work_instructions" to true),
                        ),
                    WritingProfile.Email to
                        WritingProfileSettings(
                            CleanupStrength.StrongPolish,
                            rules = mapOf("custom_rule_custom_soft" to true),
                        ),
                    WritingProfile("custom_lab") to
                        WritingProfileSettings(
                            CleanupStrength.LightCleanup,
                            rules =
                                mapOf(
                                    "custom_rule_custom_lab_instructions" to true,
                                    "custom_rule_custom_pirate" to true,
                                    "custom_rule_custom_notes" to true,
                                ),
                            name = "Lab notes",
                        ),
                ),
            settings.writingProfiles,
        )
        // The global instructions, then the profile's, as the prompt had them before rules.
        assertEquals(
            listOf("Spell it Speecher.", "Use the team glossary."),
            resolveRefinementContext(
                    "",
                    "",
                    null,
                    null,
                    WritingProfile.Work,
                    settings.writingProfiles,
                    rules,
                )
                .userRules,
        )
        assertEquals(settings, store.load())
    }

    @Test
    fun `rules saved before a downgrade survive the upgrade back`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true,
        ) {
            putInt("version", 2)
            putString("additionalInstructions", "Spell it Speecher.")
            putString(
                "refinementRules",
                """[{"id":"custom_rule_mine","name":"Mine","instruction":"Be brief.",""" +
                    """"everyProfile":true}]""",
            )
        }
        assertEquals(
            listOf(RefinementRule("custom_rule_mine", "Mine", "Be brief.", true)),
            SettingsStore(context).load().refinementRules,
        )
    }

    @Test
    fun `lists stored by the previous release load as they were`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true,
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
