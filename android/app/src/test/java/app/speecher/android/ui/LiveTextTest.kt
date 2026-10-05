package app.speecher.android.ui

import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.FailureReason
import org.junit.Assert.assertEquals
import org.junit.Test

class LiveTextTest {
    @Test
    fun `a live text switch turned off hides its words, the transcript under a failure too`() {
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
        assertEquals(
            DictationState.Failed(FailureReason.MicrophoneUnavailable, "", ""),
            DictationState.Failed(FailureReason.MicrophoneUnavailable, "", "move the review")
                .withPreviews(transcription = false, refinement = true),
        )
    }
}
