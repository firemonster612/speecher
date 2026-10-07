package app.speecher.android.dictation

import android.content.Context
import android.os.Looper
import androidx.core.content.edit
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.refinementVocabulary
import app.speecher.protocol.speechTerms
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class VocabularyTest {
    private val standup = WritingProfile("custom_standup")
    private val kubernetes =
        VocabularyWord("Kubernetes", "The container platform.", setOf(WritingProfile.Work))

    @Test
    fun `words stored as bare terms load as key terms for every profile, and the rest persists`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true
        ) {
            putString("vocabulary", """["Speecher","Priya Raman"]""")
        }
        val store = SettingsStore(context)
        assertEquals(
            listOf(VocabularyWord("Priya Raman"), VocabularyWord("Speecher")),
            store.load().vocabulary,
        )
        val words =
            listOf(
                VocabularyWord("Speecher", priority = true),
                kubernetes.copy(source = "csv", frequency = 2, lastUsedMs = 1_791_020_460_000),
                VocabularyWord("Grafana", keyTerm = false),
            )
        store.save(SpeecherSettings(vocabulary = words))
        assertEquals(words, store.load().vocabulary)
    }

    @Test
    fun `an inserted dictation counts a use of each word it contains`() {
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        store.save(SpeecherSettings(vocabulary = listOf(VocabularyWord("PR"), kubernetes)))
        store.recordVocabularyUsage("Scaled the Kubernetes cluster.", nowMs = 1700)
        assertEquals(
            listOf(kubernetes.copy(frequency = 1, lastUsedMs = 1700), VocabularyWord("PR")),
            store.load().vocabulary,
        )
    }

    @Test
    fun `a screen observing the store keeps the uses a dictation counts while it is open`() {
        val app = RuntimeEnvironment.getApplication()
        SettingsStore(app).save(SpeecherSettings(vocabulary = listOf(kubernetes)))
        val screen = SettingsStore(app)
        var settings = screen.load()
        val owner =
            object : LifecycleOwner {
                override val lifecycle = LifecycleRegistry(this)
            }
        screen.observe(owner.lifecycle) { settings = screen.load() }
        SettingsStore(app).recordVocabularyUsage("Scaled the Kubernetes cluster.", nowMs = 1700)
        shadowOf(Looper.getMainLooper()).idle()
        screen.save(settings.copy(keepScreenOn = false))
        assertEquals(
            listOf(kubernetes.copy(frequency = 1, lastUsedMs = 1700)),
            screen.load().vocabulary,
        )
    }

    @Test
    fun `a dictation uses the words for every profile and those limited to its own`() {
        val settings =
            SpeecherSettings(
                vocabulary =
                    listOf(
                        VocabularyWord("Speecher"),
                        kubernetes,
                        VocabularyWord("Lúcia", "My sister.", setOf(WritingProfile.Personal)),
                    )
            )
        assertEquals(
            listOf(kubernetes, VocabularyWord("Speecher")),
            refinementVocabulary(settings.vocabulary, WritingProfile.Work, emptyList()),
        )
    }

    @Test
    fun `the speech service gets the profile's key terms, priority ones first`() {
        val settings =
            SpeecherSettings(
                vocabulary =
                    listOf(
                        VocabularyWord("Speecher"),
                        VocabularyWord("Grafana", keyTerm = false),
                        VocabularyWord("PR", priority = true),
                        VocabularyWord("Lúcia", profiles = setOf(WritingProfile.Personal)),
                    )
            )
        assertEquals(
            listOf("PR", "Speecher"),
            speechTerms(settings.vocabulary, WritingProfile.Work, emptyList()),
        )
    }

    @Test
    fun `deleting a profile drops it from the words limited to it`() {
        val settings =
            SpeecherSettings(
                writingProfiles =
                    SpeecherSettings().writingProfiles + (standup to WritingProfileSettings()),
                vocabulary =
                    listOf(
                        VocabularyWord("PR", "Pull request.", setOf(WritingProfile.Work, standup)),
                        VocabularyWord("Jira", "", setOf(standup)),
                    ),
            )
        assertEquals(
            listOf(
                VocabularyWord("PR", "Pull request.", setOf(WritingProfile.Work)),
                VocabularyWord("Jira"),
            ),
            settings.withWritingProfiles(settings.writingProfiles - standup).vocabulary,
        )
    }
}
