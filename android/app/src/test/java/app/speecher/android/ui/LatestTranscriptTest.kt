package app.speecher.android.ui

import app.speecher.android.dictation.LatestTranscript
import org.junit.Assert.assertEquals
import org.junit.Test

class LatestTranscriptTest {
    @Test
    fun `the summary counts words as the desktop does and names the app`() {
        assertEquals(
            "4 words, Slack",
            latestTranscriptSummary(LatestTranscript("Ship it — today, please!", "Slack")),
        )
        assertEquals("1 word", latestTranscriptSummary(LatestTranscript("Done.", null)))
    }
}
