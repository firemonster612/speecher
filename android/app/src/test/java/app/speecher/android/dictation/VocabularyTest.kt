package app.speecher.android.dictation

import android.content.Context
import androidx.core.content.edit
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class VocabularyTest {
    private val standup = WritingProfile("custom_standup")
    private val kubernetes =
        VocabularyWord("Kubernetes", "The container platform.", setOf(WritingProfile.Work))

    @Test
    fun `words stored as bare terms load as words for every profile, and context and profiles persist`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true
        ) {
            putString("vocabulary", """["Speecher","Priya Raman"]""")
        }
        val store = SettingsStore(context)
        assertEquals(
            listOf(VocabularyWord("Speecher"), VocabularyWord("Priya Raman")),
            store.load().vocabulary,
        )
        store.save(SpeecherSettings(vocabulary = listOf(VocabularyWord("Speecher"), kubernetes)))
        assertEquals(listOf(VocabularyWord("Speecher"), kubernetes), store.load().vocabulary)
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
            listOf(VocabularyWord("Speecher"), kubernetes),
            settings.vocabularyFor(WritingProfile.Work),
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
