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
        val nows = listOf(1_100_000L, 1_045_000L, 1_000_000L - 3_600_000)
        assertEquals(listOf(0L, 15_000L, 60_000L), nows.map { untilCheck(1_000_000, it, 60_000) })
    }
}
