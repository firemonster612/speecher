package app.speecher.android.settings

import android.content.Context
import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.OpenAiSpeed
import app.speecher.android.provider.RefinementChoice
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.update.IntervalUnit
import app.speecher.android.update.UpdateChannel
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.RefinementRule
import app.speecher.protocol.Replacement
import app.speecher.protocol.SpeechEndpoint
import app.speecher.protocol.Tone
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import java.lang.reflect.Modifier
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class SettingsStoreTest {
    @Test
    fun `every setting saved loads back, and saving the defaults loads them on a fresh install`() {
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        assertEquals(SpeecherSettings(), store.load())

        val standup = WritingProfile("custom_standup")
        val saved =
            SpeecherSettings(
                transcriptionProvider = SpeechProvider.Endpoint,
                refinementEnabled = false,
                refinementProvider = CleanupProvider.Claude,
                transcribePassEnabled = false,
                speechEndpoint = SpeechEndpoint("http://192.168.1.20:8080", "/v1/audio", "large"),
                spokenLanguage = "de",
                chatGptRefinement = RefinementChoice("gpt-6.1-sol", "low"),
                claudeRefinement = RefinementChoice("claude-opus-5-5", "high"),
                chatGptSpeed = OpenAiSpeed.Standard,
                claudeFastMode = false,
                vocabulary =
                    listOf(
                        VocabularyWord(
                            "Kubernetes",
                            "The container platform.",
                            setOf(standup),
                            keyTerm = false,
                            priority = true,
                            source = "csv",
                            frequency = 2,
                            lastUsedMs = 1_791_020_460_000,
                        ),
                    ),
                replacements = listOf(Replacement("sign off", "Regards,\nEfox")),
                chipDockOnMic = false,
                chipOffsetX = -200,
                chipOffsetY = -100,
                keepScreenOn = false,
                pauseMedia = false,
                vibrationEnabled = true,
                transcriptionPreviewEnabled = false,
                refinementPreviewEnabled = false,
                useTargetContext = false,
                includeScreenText = true,
                includeScreenshot = true,
                defaultWritingProfile = standup,
                writingProfiles =
                    SpeecherSettings().writingProfiles +
                        mapOf(
                            WritingProfile.Email to
                                WritingProfileSettings(
                                    CleanupStrength.StrongPolish,
                                    Tone.Formal,
                                    mapOf("custom_rule_sign" to true, "headings" to false),
                                    outputLanguage = "Spanish",
                                ),
                            standup to
                                WritingProfileSettings(
                                    rules = mapOf("custom_rule_spell" to false),
                                    name = "Standup",
                                ),
                        ),
                appRules = listOf(RecognitionRule("zulip", AppCategory.Browser, standup)),
                customSystemPromptEnabled = true,
                customSystemPrompt = "Clean up my dictation.",
                refinementRules =
                    listOf(
                        RefinementRule("custom_rule_spell", "Spelling", "Spell it Speecher.", true),
                        RefinementRule("custom_rule_sign", "Sign off with Best,\nEnzo"),
                    ),
                buttonLayout = ButtonLayout.InsertOnly,
                panelSize = PanelSize.Compact,
                autoCheckUpdates = false,
                updateChannel = UpdateChannel.Nightly,
                updateCheckMinutes = 480,
                updateCheckUnit = IntervalUnit.Hours,
                insightsEnabled = false,
                correctionLearningEnabled = false,
            )
        // So a setting added later fails here until this test gives it a value, and then until
        // the store keeps it. Save leaves learned corrections as stored, so they are not here.
        val unchanged =
            SpeecherSettings::class
                .java
                .declaredFields
                .filterNot { Modifier.isStatic(it.modifiers) || it.name == "learnedCorrections" }
                .onEach { it.isAccessible = true }
                .filter { it.get(saved) == it.get(SpeecherSettings()) }
                .map { it.name }
        assertEquals(emptyList<String>(), unchanged)

        store.save(saved)
        assertEquals(saved, store.load())
        store.save(SpeecherSettings())
        assertEquals(SpeecherSettings(), store.load())
        // The deleted profile's settings go with it.
        assertEquals(
            emptyList<String>(),
            RuntimeEnvironment.getApplication()
                .getSharedPreferences("speecher-settings", Context.MODE_PRIVATE)
                .all
                .keys
                .filter { it.startsWith(standup.id) },
        )
    }

    @Test
    fun `a hand-edited update interval outside 5 minutes to 30 days loads clamped`() {
        val context = RuntimeEnvironment.getApplication()
        val preferences = context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE)
        fun loaded(minutes: Int) =
            preferences.edit().putInt("updateCheckMinutes", minutes).commit().let {
                SettingsStore(context).load()
            }
        assertEquals(43_200, loaded(100_000).updateCheckMinutes)
        preferences.edit().putString("updateCheckUnit", "Days").commit()
        assertEquals(5 to null, loaded(0).let { it.updateCheckMinutes to it.updateCheckUnit })
    }
}
