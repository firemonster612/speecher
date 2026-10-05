package app.speecher.android.dictation

import android.content.Context
import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.text.BreakIterator
import java.text.NumberFormat
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.time.temporal.ChronoUnit
import java.util.concurrent.Executors
import kotlin.math.abs
import kotlin.math.floor
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.longOrNull
import kotlinx.serialization.json.put

/**
 * What one inserted dictation leaves behind for Insights, never the text or the audio. [profile] is
 * the Writing Profile's id; [profileName] a custom profile's name when the record was made, so it
 * still reads after the profile is deleted, and empty for a built-in. The providers are the ones
 * whose words went in, by [Provider] name: no refinement provider when the transcript went in as
 * heard.
 */
data class DictationRecord(
    val finishedAt: LocalDateTime,
    val audioMillis: Long,
    val words: Int,
    val app: String,
    val profile: String,
    val profileName: String = "",
    val speechProviders: List<String> = emptyList(),
    val refinementProviders: List<String> = emptyList(),
)

/**
 * Words as a reader counts them: word-boundary segments that contain a letter or number, so
 * punctuation is not a word and unspaced scripts still count.
 */
fun countWords(text: String): Int {
    val words = BreakIterator.getWordInstance().apply { setText(text) }
    var start = words.first()
    var count = 0
    var end = words.next()
    while (end != BreakIterator.DONE) {
        if (text.substring(start, end).any(Char::isLetterOrDigit)) count++
        start = end
        end = words.next()
    }
    return count
}

// The desktop's line format, one JSON object per line, finishedAt in local time without a zone:
// {"finishedAt":"2025-10-03T09:55:00","audioMs":38000,"words":90,"app":"Mail","profile":"email"}
// The optional keys, and the provider lists, appear only when not empty.
private fun encode(record: DictationRecord): String = buildJsonObject {
    put("finishedAt", record.finishedAt.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME))
    put("audioMs", record.audioMillis)
    put("words", record.words)
    put("app", record.app)
    put("profile", record.profile)
    if (record.profileName.isNotEmpty()) put("profileName", record.profileName)
    if (record.speechProviders.isNotEmpty())
        put("speechProviders", JsonArray(record.speechProviders.map(::JsonPrimitive)))
    if (record.refinementProviders.isNotEmpty())
        put("refinementProviders", JsonArray(record.refinementProviders.map(::JsonPrimitive)))
}
    .toString()

private fun decode(line: String): DictationRecord? = runCatching {
    val record = Json.parseToJsonElement(line).jsonObject
    fun text(key: String) = record[key]?.jsonPrimitive?.content.orEmpty()
    fun list(key: String) = record[key]?.jsonArray?.map { it.jsonPrimitive.content }.orEmpty()
    DictationRecord(
        LocalDateTime.parse(text("finishedAt")),
        record["audioMs"]?.jsonPrimitive?.longOrNull ?: 0,
        record["words"]?.jsonPrimitive?.intOrNull ?: 0,
        text("app"),
        text("profile"),
        text("profileName"),
        list("speechProviders"),
        list("refinementProviders"),
    )
}
    .getOrNull()

/** The insights history, private to the app. */
fun insightsFile(context: Context) = File(context.filesDir, "insights.jsonl")

/**
 * Every insights file operation runs here, one at a time in the order asked, so a clear lands after
 * any record queued before it and cannot be undone by it.
 */
internal val insightsDispatcher = Executors.newSingleThreadExecutor().asCoroutineDispatcher()

/**
 * Queues [record] for the end of [file]. Insights are a side record: failing to keep one is logged
 * and never fails the insert.
 */
fun recordInsight(file: File, record: DictationRecord) =
    insightsDispatcher.executor.execute {
        runCatching { appendInsight(file, record) }
            .onFailure { Log.w("Speecher", "insights log append failed", it) }
    }

private fun appendInsight(file: File, record: DictationRecord) {
    // A line cut short by a crash would swallow this record on reload.
    val brokenLine =
        file.length() > 0 &&
            RandomAccessFile(file, "r").use {
                it.seek(file.length() - 1)
                it.read() != '\n'.code
            }
    file.appendText((if (brokenLine) "\n" else "") + encode(record) + "\n")
}

/**
 * The records in [file], skipping any line that does not read; none when there is no file, or when
 * it cannot be read, which is logged.
 */
suspend fun loadInsights(file: File): List<DictationRecord> =
    withContext(insightsDispatcher) {
        if (!file.exists()) return@withContext emptyList()
        runCatching { file.readLines().filter(String::isNotBlank).mapNotNull(::decode) }
            .onFailure { Log.w("Speecher", "insights log read failed", it) }
            .getOrDefault(emptyList())
    }

/** Deletes the history. False, with the history kept, when the file could not be deleted. */
suspend fun clearInsights(file: File): Boolean =
    withContext(insightsDispatcher) { !file.exists() || file.delete() }

/** The record of [inserted], made now. */
fun dictationRecord(
    inserted: Inserted,
    settings: SpeecherSettings,
    target: TargetApp?,
): DictationRecord {
    val profile = writingProfile(settings, target)
    return DictationRecord(
        LocalDateTime.now().truncatedTo(ChronoUnit.SECONDS),
        inserted.audioMillis,
        countWords(inserted.text),
        target?.label.orEmpty().ifEmpty { target?.packageName.orEmpty() }.ifEmpty { "Unknown app" },
        profile.id,
        if (profile.isBuiltIn) "" else settings.writingProfiles[profile]?.name.orEmpty(),
        listOf(inserted.speech.name),
        listOfNotNull(inserted.cleanup?.name),
    )
}

enum class InsightsRange(val label: String) {
    Last7Days("Last 7 days"),
    Last30Days("Last 30 days"),
    ThisYear("This year"),
    AllTime("All time"),
}

/**
 * The numbers Insights shows for one period. [wordsDelta] and [dictationsDelta] are the percent
 * change against the previous period of equal length, only for the 7 and 30 day periods and only
 * when that period has data; [deltaPeriod] names it.
 */
data class InsightsSummary(
    val words: Long,
    val dictations: Int,
    val audioMillis: Long,
    val activeDays: Int,
    val averageAudioMillis: Long,
    val dictationsPerActiveDay: Double,
    val wordsPerMinute: Long,
    val deltaPeriod: String?,
    val wordsDelta: Long?,
    val dictationsDelta: Long?,
)

// JavaScript's Math.round, which the desktop's numbers come from: halves go up.
private fun roundHalfUp(value: Double): Long = floor(value + 0.5).toLong()

private fun List<DictationRecord>.between(from: LocalDate?, to: LocalDate) = filter {
    val date = it.finishedAt.toLocalDate()
    (from == null || date >= from) && date <= to
}

private fun percentChange(current: Long, previous: Long): Long? =
    if (previous == 0L) null else roundHalfUp((current - previous).toDouble() / previous * 100)

fun summarize(
    records: List<DictationRecord>,
    range: InsightsRange,
    today: LocalDate,
): InsightsSummary {
    val (from, deltaPeriod) =
        when (range) {
            InsightsRange.Last7Days -> today.minusDays(6) to "week"
            InsightsRange.Last30Days -> today.minusDays(29) to "30 days"
            InsightsRange.ThisYear -> today.withDayOfYear(1) to null
            InsightsRange.AllTime -> null to null
        }
    val period = records.between(from, today)
    val words = period.sumOf { it.words.toLong() }
    val audioMillis = period.sumOf { it.audioMillis }
    val dictations = period.size
    val activeDays = period.map { it.finishedAt.toLocalDate() }.distinct().size
    val previous =
        if (from == null || deltaPeriod == null) null
        else {
            val span = ChronoUnit.DAYS.between(from, today) + 1
            records.between(from.minusDays(span), from.minusDays(1))
        }
    return InsightsSummary(
        words = words,
        dictations = dictations,
        audioMillis = audioMillis,
        activeDays = activeDays,
        averageAudioMillis =
            if (dictations == 0) 0 else roundHalfUp(audioMillis.toDouble() / dictations),
        dictationsPerActiveDay = if (activeDays == 0) 0.0 else dictations.toDouble() / activeDays,
        wordsPerMinute = if (audioMillis == 0L) 0 else roundHalfUp(words / (audioMillis / 60000.0)),
        deltaPeriod = deltaPeriod,
        wordsDelta = previous?.let { percentChange(words, it.sumOf { r -> r.words.toLong() }) },
        dictationsDelta = previous?.let { percentChange(dictations.toLong(), it.size.toLong()) },
    )
}

/** One stat tile: the figure large with its unit small, then the lines under it. */
data class InsightTile(
    val title: String,
    val value: String,
    val unit: String = "",
    val lines: List<String>,
)

private fun formatNumber(value: Long): String = NumberFormat.getIntegerInstance().format(value)

private fun oneDecimal(value: Double): String =
    NumberFormat.getNumberInstance()
        .apply {
            minimumFractionDigits = 1
            maximumFractionDigits = 1
        }
        .format(value)

/** "▲ 29% vs previous 30 days", "▼ 4% vs previous week", "Same as previous week"; null without. */
fun deltaText(delta: Long?, period: String?): String? =
    when {
        delta == null || period == null -> null
        delta == 0L -> "Same as previous $period"
        else -> "${if (delta > 0) "▲" else "▼"} ${abs(delta)}% vs previous $period"
    }

/** "4.0 hours" from an hour up, else "34 min". */
fun audioTotalText(audioMillis: Long): String =
    if (audioMillis >= HOUR_MILLIS) "${oneDecimal(audioMillis.toDouble() / HOUR_MILLIS)} hours"
    else "${roundHalfUp(audioMillis / 60000.0)} min"

/** "m:ss". */
fun clockText(millis: Long): String {
    val seconds = (millis + 500) / 1000
    return "${seconds / 60}:${(seconds % 60).toString().padStart(2, '0')}"
}

/** Words, dictations, audio and pace, in the order Insights shows them. */
fun insightTiles(summary: InsightsSummary): List<InsightTile> {
    val audio = audioTotalText(summary.audioMillis)
    return listOf(
        InsightTile(
            "Words dictated",
            formatNumber(summary.words),
            lines = listOfNotNull(deltaText(summary.wordsDelta, summary.deltaPeriod)),
        ),
        InsightTile(
            "Dictations",
            formatNumber(summary.dictations.toLong()),
            lines =
                listOfNotNull(
                    if (summary.activeDays == 0) "Nothing yet"
                    else "${oneDecimal(summary.dictationsPerActiveDay)} a day when you dictate",
                    deltaText(summary.dictationsDelta, summary.deltaPeriod),
                ),
        ),
        InsightTile(
            "Audio transcribed",
            audio.substringBeforeLast(' '),
            audio.substringAfterLast(' '),
            listOf(
                if (summary.dictations == 0) "Nothing yet"
                else "Average dictation ${clockText(summary.averageAudioMillis)}"
            ),
        ),
        InsightTile(
            "Pace",
            formatNumber(summary.wordsPerMinute),
            "wpm",
            listOf(
                if (summary.dictations == 0) "No dictation in this period."
                else "Your speaking pace"
            ),
        ),
    )
}

private const val HOUR_MILLIS = 3_600_000L
