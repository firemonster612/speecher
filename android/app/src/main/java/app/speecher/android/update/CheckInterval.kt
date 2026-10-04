package app.speecher.android.update

/** A unit the Custom interval dialog offers, and the counts of it allowed: 5 minutes to 30 days. */
enum class IntervalUnit(val minutes: Int, val counts: IntRange) {
    Minutes(1, 5..1_440),
    Hours(60, 1..720),
    Days(1_440, 1..30),
}

/** How long a check may wait, in minutes: 5 minutes to 30 days. */
val checkIntervalMinutes = 5..30 * 1_440

/** How long a failed check waits before trying again: an hour, or the interval if shorter. */
const val RETRY_MILLIS = 3_600_000L

/** The largest unit [minutes] is a whole number of, so a week opens the dialog as 7 days. */
fun fittingUnit(minutes: Int): IntervalUnit =
    IntervalUnit.entries.last { minutes % it.minutes == 0 }

/**
 * Milliseconds until the next update check is due, [interval] after [lastCheck]. Never more than
 * [interval], so a clock set back does not hold checks off.
 */
fun untilCheck(lastCheck: Long, now: Long, interval: Long): Long =
    (lastCheck + interval - now).coerceIn(0, interval)
