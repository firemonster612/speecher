package app.speecher.android.dictation

import app.speecher.protocol.CorrectionEvidence
import app.speecher.protocol.LearnedCorrection
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class LearnedCorrectionsTest {
    private val store = SettingsStore(RuntimeEnvironment.getApplication())
    private val edit = CorrectionEvidence("speaker", "Speecher", 0.75)

    @Test
    fun `an edit seen twice is stored as a correction`() {
        store.recordCorrection(edit, "com.Slack", nowMs = 100)
        assertEquals(emptyList<LearnedCorrection>(), store.load().learnedCorrections)

        store.recordCorrection(edit, "com.Slack", nowMs = 200)
        val learned = store.load().learnedCorrections.single()
        assertEquals(
            LearnedCorrection(
                learned.id,
                "speaker",
                "Speecher",
                "com.Slack",
                100,
                0.75,
                true,
                2,
                200,
            ),
            learned,
        )
    }

    @Test
    fun `saving settings keeps a correction learned since they were loaded`() {
        val loaded = store.load()
        store.recordCorrection(CorrectionEvidence("github", "GitHub", 0.98), "com.Slack")
        store.save(loaded.copy(vibrationEnabled = true))
        assertEquals(listOf("GitHub"), store.load().learnedCorrections.map { it.corrected })
    }

    @Test
    fun `with learning off nothing is recorded`() {
        store.save(SpeecherSettings(correctionLearningEnabled = false))
        store.recordCorrection(CorrectionEvidence("github", "GitHub", 0.98), "com.Slack")
        // A spelling fix would be learned at once with learning on.
        assertEquals(emptyList<LearnedCorrection>(), store.load().learnedCorrections)
    }
}
