package app.speecher.android.dictation

import java.io.File
import java.time.LocalDate
import java.time.LocalDateTime
import kotlin.io.path.createTempDirectory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class InsightsTest {
    @Test
    fun `words are segments with a letter or number, not punctuation`() {
        assertEquals(4, countWords("It's 9 o'clock — done."))
    }

    @Test
    fun `the log appends desktop lines, mends a cut-off line and skips it on reading`() {
        val file = File(createTempDirectory().toFile(), "insights.jsonl")
        file.writeText("{\"finishedAt\":\"2025-10")
        val record =
            DictationRecord(
                LocalDateTime.of(2025, 10, 3, 9, 55),
                38000,
                90,
                "Thunderbird",
                "custom_notes",
                "Notes",
            )
        appendInsight(file, record)
        assertEquals(
            "{\"finishedAt\":\"2025-10\n" +
                "{\"finishedAt\":\"2025-10-03T09:55:00\",\"audioMs\":38000,\"words\":90," +
                "\"app\":\"Thunderbird\",\"profile\":\"custom_notes\",\"profileName\":\"Notes\"}\n",
            file.readText(),
        )
        assertEquals(listOf(record), readInsights(file))
        assertTrue(clearInsights(file))
        assertFalse(file.exists())
    }

    private fun record(day: String, hour: Int, audioMillis: Long, words: Int) =
        DictationRecord(
            LocalDate.parse(day).atTime(hour, 0),
            audioMillis,
            words,
            "Messages",
            "personal",
        )

    private val records =
        listOf(
            record("2026-09-25", 9, 60_000, 200), // The week before.
            record("2026-10-04", 10, 30_000, 50),
            record("2026-10-04", 11, 30_000, 100),
            record("2026-10-05", 9, 60_000, 150),
            record("2026-10-06", 9, 60_000, 999), // After today.
        )
    private val today = LocalDate.of(2026, 10, 5)

    @Test
    fun `the last 7 days compare with the week before`() {
        assertEquals(
            listOf(
                InsightTile("Words dictated", "300", lines = listOf("▲ 50% vs previous week")),
                InsightTile(
                    "Dictations",
                    "3",
                    lines = listOf("1.5 a day when you dictate", "▲ 200% vs previous week"),
                ),
                InsightTile("Audio transcribed", "2", "min", listOf("Average dictation 0:40")),
                InsightTile("Pace", "150", "wpm", listOf("Your speaking pace")),
            ),
            insightTiles(summarize(records, InsightsRange.Last7Days, today)),
        )
    }

    @Test
    fun `the longer periods count up to today without a comparison`() {
        val summary = summarize(records, InsightsRange.AllTime, today)
        assertEquals(500L to 4, summary.words to summary.dictations)
        assertEquals(null to null, summary.wordsDelta to summary.dictationsDelta)
        assertEquals(
            InsightTile("Pace", "0", "wpm", listOf("No dictation in this period.")),
            insightTiles(summarize(records, InsightsRange.ThisYear, LocalDate.of(2027, 1, 1)))
                .last(),
        )
    }

    @Test
    fun `wording for a flat or falling period and long audio`() {
        assertEquals("Same as previous week", deltaText(0, "week"))
        assertEquals("▼ 4% vs previous 30 days", deltaText(-4, "30 days"))
        assertEquals("4.0 hours", audioTotalText(4 * 3_600_000L))
        assertEquals("1:07", clockText(66_600))
    }
}
