package app.speecher.android.update

import org.junit.Assert.assertEquals
import org.junit.Test

class CheckIntervalTest {
    @Test
    fun `an interval opens in the largest unit that divides it, each allowing 5 minutes to 30 days`() {
        assertEquals(IntervalUnit.entries.reversed(), listOf(10_080, 360, 30).map(::fittingUnit))
        assertEquals(listOf(5..1_440, 1..720, 1..30), IntervalUnit.entries.map { it.counts })
    }

    @Test
    fun `a check is due an interval after the last one, however far the clock is set back`() {
        // Never checked, part-way, exactly due, and a clock set back an hour, which waits one
        // interval, not the hour as well.
        val lastAndNow =
            listOf(
                0L to 1_000_000L,
                1_000_000L to 1_045_000L,
                1_000_000L to 1_060_000L,
                1_000_000L to 1_000_000L - 3_600_000,
            )
        assertEquals(
            listOf(0L, 15_000L, 0L, 60_000L),
            lastAndNow.map { (last, now) -> untilCheck(last, now, 60_000) },
        )
    }
}
