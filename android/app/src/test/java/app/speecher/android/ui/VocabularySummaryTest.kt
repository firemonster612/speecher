package app.speecher.android.ui

import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SpeecherSettings
import org.junit.Assert.assertEquals
import org.junit.Test

class VocabularySummaryTest {
    @Test
    fun `Claude counts the terms that fit its header, ChatGPT only refinement`() {
        val words = listOf("Speecher", "speecher", "Kotlin")
        assertEquals(
            "Names and terms Speecher should spell your way. Claude takes the first 2 as speech " +
                "hints, and all are used for refinement.",
            vocabularySummary(
                SpeecherSettings(transcriptionProvider = Provider.Claude, vocabulary = words)
            ),
        )
        assertEquals(
            "Names and terms Speecher should spell your way. ChatGPT dictation takes no speech " +
                "hints, and the first 1000 are used for refinement.",
            vocabularySummary(
                SpeecherSettings(
                    transcriptionProvider = Provider.ChatGpt,
                    vocabulary = List(1001) { "t$it" },
                )
            ),
        )
    }
}
