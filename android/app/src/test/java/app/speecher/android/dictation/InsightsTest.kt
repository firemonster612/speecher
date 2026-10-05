package app.speecher.android.dictation

import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import java.io.File
import java.time.LocalDate
import java.time.LocalDateTime
import java.util.Locale
import java.util.concurrent.CountDownLatch
import kotlin.io.path.createTempDirectory
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class InsightsTest {
    // The figures are formatted for the default locale; these are the English ones.
    private val locale = Locale.getDefault()

    @Before fun pinLocale() = Locale.setDefault(Locale.US)

    @After fun restoreLocale() = Locale.setDefault(locale)

    @Test
    fun `words are segments with a letter or number, not punctuation`() {
        assertEquals(4, countWords("It's 9 o'clock — done."))
    }

    private val file = File(createTempDirectory().toFile(), "insights.jsonl")

    @Test
    fun `the log appends desktop lines, mends a cut-off line and skips it on reading`() {
        file.writeText("{\"finishedAt\":\"2025-10")
        val record =
            DictationRecord(
                LocalDateTime.of(2025, 10, 3, 9, 55),
                38000,
                90,
                "Thunderbird",
                "custom_notes",
                "Notes",
                listOf("ChatGpt"),
                listOf("Claude"),
            )
        recordInsight(file, record)
        assertEquals(listOf(record), runBlocking { loadInsights(file) })
        assertEquals(
            "{\"finishedAt\":\"2025-10\n" +
                "{\"finishedAt\":\"2025-10-03T09:55:00\",\"audioMs\":38000,\"words\":90," +
                "\"app\":\"Thunderbird\",\"profile\":\"custom_notes\",\"profileName\":\"Notes\"," +
                "\"speechProviders\":[\"ChatGpt\"],\"refinementProviders\":[\"Claude\"]}\n",
            file.readText(),
        )
    }

    @Test
    fun `a clear waits for the records queued before it, so none comes back`() {
        val gate = CountDownLatch(1)
        insightsDispatcher.executor.execute { gate.await() }
        recordInsight(file, record("2026-10-05", 9, 60_000, 150))
        runBlocking {
            // Asked while the record is still waiting its turn.
            val clearing = async(start = CoroutineStart.UNDISPATCHED) { clearInsights(file) }
            gate.countDown()
            assertTrue(clearing.await())
            assertEquals(emptyList<DictationRecord>(), loadInsights(file))
        }
        assertFalse(file.exists())
    }

    @Test
    fun `a record names the app it went into and keeps a custom profile's name`() {
        val notes = WritingProfile("custom_notes")
        val withNotes =
            SpeecherSettings().let {
                it.copy(
                    writingProfiles =
                        it.writingProfiles + (notes to WritingProfileSettings(name = "Notes")),
                    defaultWritingProfile = notes,
                )
            }
        val at = LocalDateTime.of(2026, 10, 5, 9, 0)
        fun recorded(settings: SpeecherSettings, target: TargetApp?) =
            dictationRecord(
                    Inserted("Two words", 1_000, SpeechProvider.Claude, CleanupProvider.ChatGpt),
                    settings,
                    target,
                )
                .copy(finishedAt = at)
        assertEquals(
            DictationRecord(
                at,
                1_000,
                2,
                "org.example.app",
                "custom_notes",
                "Notes",
                listOf("Claude"),
                listOf("ChatGpt"),
            ),
            recorded(withNotes, TargetApp("org.example.app", "", null, false)),
        )
        assertEquals(
            DictationRecord(
                at,
                1_000,
                2,
                "Example",
                "other",
                "",
                listOf("Claude"),
                listOf("ChatGpt"),
            ),
            recorded(SpeecherSettings(), TargetApp("org.example.app", "Example", null, false)),
        )
        assertEquals("Unknown app", recorded(SpeecherSettings(), null).app)
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
