package app.speecher.android.dictation

import app.speecher.protocol.NearbyText
import org.junit.Assert.assertEquals
import org.junit.Test

class RefinementContextTest {
    private val messages = TargetApp("com.google.android.apps.messaging", "Messages", null, false)

    private val dinner = NearbyText("Dinner at ", " works for me", 10, 15)

    private fun textSent(settings: SpeecherSettings, target: TargetApp) =
        refinementContext(settings, target) { dinner }.nearbyText

    @Test
    fun `text around the caret is read only with context on and outside password fields`() {
        assertEquals(
            listOf(dinner, null, null),
            listOf(
                textSent(SpeecherSettings(), messages),
                textSent(SpeecherSettings(useTargetContext = false), messages),
                textSent(SpeecherSettings(), messages.copy(secure = true)),
            ),
        )
    }

    @Test
    fun `surrounding text splits at the selection, offset into the field when known`() {
        assertEquals(
            listOf(
                NearbyText("Dinner at ", " works for me", 110, 115),
                NearbyText("Dinner at ", " works for me", -1, -1),
            ),
            listOf(
                nearbyText("Dinner at seven works for me", 10, 15, 100),
                nearbyText("Dinner at seven works for me", 10, 15, -1),
            ),
        )
    }
}
