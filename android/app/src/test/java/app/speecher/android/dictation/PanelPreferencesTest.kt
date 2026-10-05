package app.speecher.android.dictation

import app.speecher.android.ui.withPreviews
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class PanelPreferencesTest {
    @Test
    fun `media pauses, vibration is off and both previews show by default, and changes persist`() {
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        val loaded = store.load()
        assertEquals(
            listOf(true, false, true, true),
            listOf(
                loaded.pauseMedia,
                loaded.vibrationEnabled,
                loaded.transcriptionPreviewEnabled,
                loaded.refinementPreviewEnabled,
            ),
        )
        store.save(
            SpeecherSettings(
                pauseMedia = false,
                vibrationEnabled = true,
                transcriptionPreviewEnabled = false,
                refinementPreviewEnabled = false,
            )
        )
        val saved = store.load()
        assertEquals(
            listOf(false, true, false, false),
            listOf(
                saved.pauseMedia,
                saved.vibrationEnabled,
                saved.transcriptionPreviewEnabled,
                saved.refinementPreviewEnabled,
            ),
        )
    }

    @Test
    fun `a preview turned off shows none of its words, and a failure keeps its transcript`() {
        val listening = DictationState.Listening("move the", "review", 0.5f)
        assertEquals(
            DictationState.Listening("", "", 0.5f),
            listening.withPreviews(transcription = false, refinement = true),
        )
        val refining = DictationState.Refining("move the review", "Move the review.")
        assertEquals(
            DictationState.Refining("move the review", ""),
            refining.withPreviews(transcription = true, refinement = false),
        )
        assertEquals(
            DictationState.Refining("", "Move the review."),
            refining.withPreviews(transcription = false, refinement = true),
        )
        val failed =
            DictationState.Failed(FailureReason.MicrophoneUnavailable, "", "move the review")
        assertEquals(failed, failed.withPreviews(transcription = false, refinement = false))
    }
}
