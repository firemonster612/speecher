package app.speecher.protocol

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Test

class VocabularyTest {
    @Test
    fun `normalising merges spellings of a term into the first and orders by send priority`() {
        assertEquals(
            listOf(
                VocabularyWord(
                    "kubernetes",
                    "The container platform.",
                    priority = true,
                    frequency = 5,
                    lastUsedMs = 9,
                ),
                VocabularyWord("PR", priority = true),
                VocabularyWord("Grafana Loki"),
                VocabularyWord("Ilık"),
                VocabularyWord("Zed", keyTerm = false, priority = true),
                VocabularyWord("ılık"),
            ),
            normalizedVocabulary(
                listOf(
                    VocabularyWord(" Grafana \t Loki ", source = " "),
                    VocabularyWord("Zed", keyTerm = false, priority = true),
                    VocabularyWord("kubernetes", frequency = 2),
                    VocabularyWord(
                        "Kubernetes",
                        " The container platform. ",
                        setOf(WritingProfile.Work),
                        keyTerm = false,
                        priority = true,
                        frequency = 5,
                        lastUsedMs = 9,
                    ),
                    VocabularyWord("PR", priority = true),
                    // Distinct on the desktop: Qt folds I to i but leaves the dotless ı alone.
                    VocabularyWord("ılık"),
                    VocabularyWord("Ilık"),
                    VocabularyWord("   "),
                )
            ),
        )
    }

    @Test
    fun `speech hints stop at 100 key terms and 500 tokens, skipping a term that does not fit`() {
        assertEquals(
            List(100) { "t$it" },
            speechTerms(List(101) { VocabularyWord("t$it", frequency = 200 - it) }),
        )
        val long = List(499) { "w" }.joinToString(" ")
        assertEquals(
            listOf(long, "one"),
            speechTerms(
                listOf(
                    VocabularyWord(long, frequency = 4),
                    VocabularyWord("not sent", keyTerm = false, frequency = 3),
                    VocabularyWord("two words", frequency = 2),
                    VocabularyWord("one", frequency = 1),
                )
            ),
        )
    }

    @Test
    fun `a use counts for each term the text holds as whole words, ignoring case and spacing`() {
        val words = listOf("Kubernetes", "Grafana Loki", "PR", "Lúcia").map { VocabularyWord(it) }
        assertEquals(
            listOf(
                VocabularyWord("Kubernetes", frequency = 1, lastUsedMs = 1700),
                VocabularyWord("Grafana Loki", frequency = 1, lastUsedMs = 1700),
                VocabularyWord("PR"),
                VocabularyWord("Lúcia", frequency = 1, lastUsedMs = 1700),
            ),
            withUsage(
                words,
                "Told LÚCIA the kubernetes cluster's grafana\nloki is up. PRs next.",
                1700,
            ),
        )
    }

    @Test
    fun `CSV columns come from a header row by name, or in the desktop's order without one`() {
        assertEquals(
            listOf(
                VocabularyWord(
                    "Kubernetes",
                    "The container platform, \"k8s\".\nOr clusters.",
                    priority = true,
                    source = "csv",
                    frequency = 4,
                    lastUsedMs = 1700,
                ),
                VocabularyWord("Grafana", source = "csv"),
            ),
            parseVocabularyCsv(
                "\uFEFFContext,Term,Starred,Frequency,Last Used\r\n" +
                    "\"The container platform, \"\"k8s\"\".\nOr clusters.\",Kubernetes,yes,4,1700\r\n" +
                    " , \r\n" +
                    ",Grafana,no,x,\r\n"
            ),
        )
        assertEquals(
            listOf(
                VocabularyWord(
                    "Speecher",
                    "My app",
                    priority = true,
                    source = "learned",
                    frequency = 2,
                    lastUsedMs = 3,
                )
            ),
            parseVocabularyCsv("Speecher,learned,1,2,3,My app"),
        )
        assertEquals(
            "CSV contains an unterminated quoted field.",
            assertThrows(IllegalArgumentException::class.java) {
                    parseVocabularyCsv("\"Kubernetes")
                }
                .message,
        )
    }
}
