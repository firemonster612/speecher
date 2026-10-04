package app.speecher.android.update

import android.content.Context
import app.speecher.android.dictation.SettingsStore
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.ui.updateCheckLabel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class CheckIntervalTest {
    @Test
    fun `a preset shows its name and a custom interval its count, without one for a single unit`() {
        assertEquals("Every week", updateCheckLabel(10_080, null))
        assertEquals("Every 7 days", updateCheckLabel(10_080, IntervalUnit.Days))
        assertEquals("Every 8 hours", updateCheckLabel(480, IntervalUnit.Hours))
        assertEquals("Every day", updateCheckLabel(1_440, IntervalUnit.Days))
        assertEquals("Every 90 minutes", updateCheckLabel(90, IntervalUnit.Minutes))
    }

    @Test
    fun `a preset opens the dialog in the largest unit that divides it`() {
        assertEquals(IntervalUnit.Days, fittingUnit(10_080))
        assertEquals(IntervalUnit.Hours, fittingUnit(360))
        assertEquals(IntervalUnit.Minutes, fittingUnit(30))
    }

    @Test
    fun `each unit allows 5 minutes to 30 days`() {
        assertFalse(4 in IntervalUnit.Minutes.counts)
        assertTrue(1_440 in IntervalUnit.Minutes.counts)
        assertFalse(1_441 in IntervalUnit.Minutes.counts)
        assertTrue(720 in IntervalUnit.Hours.counts)
        assertFalse(721 in IntervalUnit.Hours.counts)
        assertFalse(0 in IntervalUnit.Days.counts)
        assertFalse(31 in IntervalUnit.Days.counts)
    }

    @Test
    fun `a check is due an interval after the last one`() {
        assertEquals(0L, untilCheck(0, 1_000_000, 60_000))
        assertEquals(15_000L, untilCheck(1_000_000, 1_045_000, 60_000))
        assertEquals(0L, untilCheck(1_000_000, 1_060_000, 60_000))
        // A clock set back an hour waits one interval, not the hour as well.
        assertEquals(60_000L, untilCheck(1_000_000, 1_000_000 - 3_600_000, 60_000))
    }

    @Test
    fun `the interval persists with the unit it was entered in, every day by default`() {
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        assertEquals(1_440, store.load().updateCheckMinutes)
        assertNull(store.load().updateCheckUnit)
        store.save(SpeecherSettings(updateCheckMinutes = 480, updateCheckUnit = IntervalUnit.Hours))
        assertEquals(480, store.load().updateCheckMinutes)
        assertEquals(IntervalUnit.Hours, store.load().updateCheckUnit)
        store.save(SpeecherSettings(updateCheckMinutes = 15))
        assertNull(store.load().updateCheckUnit)
    }

    @Test
    fun `a hand-edited interval outside 5 minutes to 30 days loads clamped`() {
        val context = RuntimeEnvironment.getApplication()
        val store = SettingsStore(context)
        val preferences = context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE)
        preferences.edit().putInt("updateCheckMinutes", 0).commit()
        assertEquals(5, store.load().updateCheckMinutes)
        preferences.edit().putInt("updateCheckMinutes", 100_000).commit()
        assertEquals(43_200, store.load().updateCheckMinutes)
        preferences
            .edit()
            .putInt("updateCheckMinutes", 0)
            .putString("updateCheckUnit", "Days")
            .commit()
        assertNull(store.load().updateCheckUnit)
    }
}
