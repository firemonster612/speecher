package app.speecher.android.ui

import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.protocol.VocabularyWord
import org.junit.Assert.assertEquals
import org.junit.Test

class VocabularySummaryTest {
    @Test
    fun `Claude counts the key terms that fit its header, ChatGPT only refinement`() {
        val words =
            listOf("Speecher", "speecher", "Kotlin").map(::VocabularyWord) +
                VocabularyWord("Grafana", keyTerm = false)
        assertEquals(
            "Names and terms Speecher should spell your way. Claude takes the 2 marked Key term, " +
                "and refinement uses every word for the dictation's Writing Profile.",
            vocabularySummary(
                SpeecherSettings(transcriptionProvider = Provider.Claude, vocabulary = words)
            ),
        )
        assertEquals(
            "Names and terms Speecher should spell your way. ChatGPT dictation takes no key " +
                "terms, and the first 1000 are used for refinement.",
            vocabularySummary(
                SpeecherSettings(
                    transcriptionProvider = Provider.ChatGpt,
                    vocabulary = List(1001) { VocabularyWord("t$it") },
                )
            ),
        )
    }

    @Test
    fun `key terms are the words that fit the Claude header`() {
        // 8 + 1 + 1010 bytes leaves room for KWin (to exactly 1024) but not Kotlin.
        val words =
            listOf("Speecher", "speecher", "a".repeat(1010), "Kotlin", "KWin").map(::VocabularyWord)
        assertEquals(
            setOf("Speecher", "a".repeat(1010), "KWin"),
            keyTerms(SpeecherSettings(transcriptionProvider = Provider.Claude, vocabulary = words)),
        )
        assertEquals(
            emptySet<String>(),
            keyTerms(
                SpeecherSettings(transcriptionProvider = Provider.ChatGpt, vocabulary = words)
            ),
        )
    }
}
