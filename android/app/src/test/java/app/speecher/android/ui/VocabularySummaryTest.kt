package app.speecher.android.ui

import app.speecher.android.dictation.SpeechProvider
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.protocol.VocabularyWord
import org.junit.Assert.assertEquals
import org.junit.Test

class VocabularySummaryTest {
    private fun settings(provider: SpeechProvider, words: List<VocabularyWord>) =
        SpeecherSettings(transcriptionProvider = provider, vocabulary = words)

    @Test
    fun `the limit says what Claude takes as key terms, and ChatGPT only refinement`() {
        val words = listOf("Speecher", "Kotlin").map(::VocabularyWord)
        assertEquals("2 of 100 key terms", vocabularyLimit(settings(SpeechProvider.Claude, words)))
        assertEquals(
            "3 terms. 2 are key terms, and all are used for refinement.",
            vocabularyLimit(
                settings(SpeechProvider.Claude, words + VocabularyWord("Grafana", keyTerm = false))
            ),
        )
        assertEquals(
            "1 term, used for refinement",
            vocabularyLimit(settings(SpeechProvider.ChatGpt, words.take(1))),
        )
        assertEquals(
            "1001 terms, the first 1000 are used for refinement",
            vocabularyLimit(
                settings(SpeechProvider.ChatGpt, List(1001) { VocabularyWord("t$it") })
            ),
        )
    }

    @Test
    fun `key terms are the words that fit the Claude header, in the order it is cut from`() {
        // By term: 1010 + 1 + 6 bytes for Kotlin, + 1 + 4 for KWin, leaving no room for Speecher.
        val words = listOf("Speecher", "a".repeat(1010), "Kotlin", "KWin").map(::VocabularyWord)
        assertEquals(
            setOf("a".repeat(1010), "Kotlin", "KWin"),
            keyTerms(settings(SpeechProvider.Claude, words)),
        )
        assertEquals(emptySet<String>(), keyTerms(settings(SpeechProvider.ChatGpt, words)))
    }
}
