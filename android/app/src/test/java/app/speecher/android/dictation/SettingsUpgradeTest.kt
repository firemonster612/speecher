package app.speecher.android.dictation

import android.content.Context
import androidx.core.content.edit
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(qualifiers = "xhdpi")
class SettingsUpgradeTest {
    @Test
    fun `a chip offset saved for the smaller chip window moves once to keep the pill in place`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true
        ) {
            putBoolean("chipDockOnMic", false)
            putInt("chipOffsetX", -200)
            putInt("chipOffsetY", -100)
        }
        val store = SettingsStore(context)
        store.load()
        val settings = store.load()
        assertEquals(-204, settings.chipOffsetX)
        assertEquals(-112, settings.chipOffsetY)
    }

    @Test
    fun `providers saved as accounts load as the same speech and cleanup choices`() {
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("speecher-settings", Context.MODE_PRIVATE).edit(
            commit = true
        ) {
            putString("transcription", "Claude")
            putString("refinementProvider", "Claude")
        }
        val settings = SettingsStore(context).load()
        assertEquals(
            SpeechProvider.Claude to CleanupProvider.Claude,
            settings.transcriptionProvider to settings.refinementProvider,
        )
    }
}
