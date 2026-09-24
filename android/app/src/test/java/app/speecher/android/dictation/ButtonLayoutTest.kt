package app.speecher.android.dictation

import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class ButtonLayoutTest {
    @Test
    fun `each layout shows its actions, the filled one last`() {
        assertEquals(
            listOf(InsertAction.Insert, InsertAction.InsertRefined),
            ButtonLayout.RefinedPrimary.actions,
        )
        assertEquals(listOf(InsertAction.Insert), ButtonLayout.InsertOnly.actions)
        assertEquals(listOf(InsertAction.InsertRefined), ButtonLayout.RefinedOnly.actions)
    }

    @Test
    fun `refinement off shows only Insert`() {
        val settings = SpeecherSettings(buttonLayout = ButtonLayout.RefinedOnly)
        assertEquals(ButtonLayout.RefinedOnly, settings.shownButtonLayout)
        assertEquals(
            ButtonLayout.InsertOnly,
            settings.copy(refinementEnabled = false).shownButtonLayout,
        )
    }

    @Test
    fun `the layout persists and defaults to refined primary`() {
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        assertEquals(ButtonLayout.RefinedPrimary, store.load().buttonLayout)
        store.save(SpeecherSettings(buttonLayout = ButtonLayout.InsertOnly))
        assertEquals(ButtonLayout.InsertOnly, store.load().buttonLayout)
    }
}
