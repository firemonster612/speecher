package app.speecher.android.dictation

import app.speecher.protocol.Replacement
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class ReplacementSettingsTest {
    @Test
    fun `replacements and multi-line snippets persist`() {
        val store = SettingsStore(RuntimeEnvironment.getApplication())
        val replacements =
            listOf(
                Replacement("my email", "efox@example.com"),
                Replacement("sign off", "Regards,\nEfox"),
            )
        store.save(SpeecherSettings(replacements = replacements))
        assertEquals(replacements, store.load().replacements)
    }
}
