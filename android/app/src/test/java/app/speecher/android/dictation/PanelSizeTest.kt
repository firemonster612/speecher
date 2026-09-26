package app.speecher.android.dictation

import androidx.compose.ui.unit.dp
import androidx.core.content.edit
import app.speecher.android.ui.panelHeight
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class PanelSizeTest {
    @Test
    fun `the size persists and an unknown one falls back to full`() {
        val context = RuntimeEnvironment.getApplication()
        val store = SettingsStore(context)
        assertEquals(PanelSize.Full, store.load().panelSize)
        store.save(SpeecherSettings(panelSize = PanelSize.Compact))
        assertEquals(PanelSize.Compact, store.load().panelSize)
        context.getSharedPreferences("speecher-settings", 0).edit(commit = true) {
            putString("panelSize", "Huge")
        }
        assertEquals(PanelSize.Full, store.load().panelSize)
    }

    @Test
    fun `the panel opens at the chosen size and the control toggles the bar`() {
        val listening = DictationState.Listening()
        assertEquals(PanelSize.Full, shownPanelSize(PanelSize.Full, false, listening))
        assertEquals(PanelSize.Compact, shownPanelSize(PanelSize.Compact, false, listening))
        assertEquals(PanelSize.Minimized, shownPanelSize(PanelSize.Minimized, false, listening))
        assertEquals(PanelSize.Minimized, shownPanelSize(PanelSize.Full, true, listening))
        assertEquals(PanelSize.Minimized, shownPanelSize(PanelSize.Compact, true, listening))
        assertEquals(PanelSize.Full, shownPanelSize(PanelSize.Minimized, true, listening))
    }

    @Test
    fun `a failure expands the bar so its recovery shows`() {
        val failed = DictationState.Failed(FailureReason.Network, "", "")
        assertEquals(PanelSize.Full, shownPanelSize(PanelSize.Minimized, false, failed))
        assertEquals(PanelSize.Compact, shownPanelSize(PanelSize.Compact, true, failed))
    }

    @Test
    fun `each size has its height`() {
        assertEquals(304f, panelHeight(PanelSize.Full, 800.dp).value, 0.01f)
        assertEquals(360f, panelHeight(PanelSize.Full, 1200.dp).value, 0.01f)
        assertEquals(240f, panelHeight(PanelSize.Full, 500.dp).value, 0.01f)
        assertEquals(184f, panelHeight(PanelSize.Compact, 800.dp).value, 0.01f)
        assertEquals(56f, panelHeight(PanelSize.Minimized, 800.dp).value, 0.01f)
    }
}
