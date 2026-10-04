package app.speecher.android.update

/** A unit the Custom interval dialog offers, and the counts of it allowed: 5 minutes to 30 days. */
enum class IntervalUnit(val minutes: Int, val counts: IntRange) {
    Minutes(1, 5..1_440),
    Hours(60, 1..720),
    Days(1_440, 1..30),
}

/** The largest unit [minutes] is a whole number of, so a week opens the dialog as 7 days. */
fun fittingUnit(minutes: Int): IntervalUnit =
    IntervalUnit.entries.last { minutes % it.minutes == 0 }

/**
 * Milliseconds until the next update check is due, [interval] after [lastCheck]. Never more than
 * [interval], so a clock set back does not hold checks off.
 */
fun untilCheck(lastCheck: Long, now: Long, interval: Long): Long =
    (lastCheck + interval - now).coerceIn(0, interval)
