package app.speecher.android.dictation

import org.junit.Assert.assertEquals
import org.junit.Test

class RefinementContextTest {
    private val messages = TargetApp("com.google.android.apps.messaging", "Messages", null, false)

    private fun textSent(settings: SpeecherSettings, target: TargetApp) =
        refinementContext(settings, target) { "Dinner plan" }.textBeforeCaret

    @Test
    fun `text before the caret is read only with context on and outside password fields`() {
        assertEquals(
            listOf("Dinner plan", null, null),
            listOf(
                textSent(SpeecherSettings(), messages),
                textSent(SpeecherSettings(useTargetContext = false), messages),
                textSent(SpeecherSettings(), messages.copy(secure = true)),
            ),
        )
    }
}
