package app.speecher.android.dictation

import android.text.InputType
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.NearbyText
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import org.junit.Assert.assertEquals
import org.junit.Test

class RefinementContextTest {
    private val messages =
        TargetApp("com.google.android.apps.messaging", "Messages", null, false, "text", "Message")

    private val dinner = NearbyText("Dinner at ", " works for me", 10, 15)

    private val screen = ScreenCapture("Sam", "Are we still on for tonight?")

    private fun sent(settings: SpeecherSettings, target: TargetApp) =
        refinementContext(settings, target, screen, "AAAA") {
                if (
                    settings.writingProfiles.values.any {
                        it.cleanupStrength == CleanupStrength.None
                    }
                )
                    error("read the field with no cleanup to do")
                dinner
            }
            .let {
                listOf(
                    it.nearbyText,
                    it.controlRole,
                    it.fieldHint,
                    it.windowTitle,
                    it.screenText,
                    it.screenshotJpeg,
                )
            }

    @Test
    fun `field and screen context is sent only with context on, outside password fields, when refining`() {
        assertEquals(
            listOf(
                listOf(dinner, "text", "Message", "Sam", "Are we still on for tonight?", "AAAA"),
                listOf(null, "text", "", "", "", null),
                listOf(null, "text", "", "", "", null),
                listOf(null, "text", "", "", "", null),
            ),
            listOf(
                sent(SpeecherSettings(), messages),
                sent(SpeecherSettings(useTargetContext = false), messages),
                sent(SpeecherSettings(), messages.copy(secure = true)),
                sent(
                    SpeecherSettings(
                        writingProfiles =
                            WritingProfile.entries.associateWith {
                                WritingProfileSettings(CleanupStrength.None)
                            }
                    ),
                    messages,
                ),
            ),
        )
    }

    @Test
    fun `surrounding text splits at the selection, offset into the field when known, or is dropped when out of range`() {
        assertEquals(
            listOf(
                NearbyText("Dinner at ", " works for me", 110, 115),
                NearbyText("Dinner at ", " works for me", -1, -1),
                null,
                null,
            ),
            listOf(
                nearbyText("Dinner at seven works for me", 10, 15, 100),
                nearbyText("Dinner at seven works for me", 10, 15, -1),
                nearbyText("Dinner at seven works for me", 10, 40, 100),
                nearbyText("Dinner at seven works for me", -1, -1, -1),
            ),
        )
    }

    @Test
    fun `control role names the text variation, else the input class`() {
        assertEquals(
            listOf("email subject", "multi-line text", "number", ""),
            listOf(
                controlRole(
                    InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_EMAIL_SUBJECT
                ),
                controlRole(InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE),
                controlRole(InputType.TYPE_CLASS_NUMBER),
                controlRole(InputType.TYPE_NULL),
            ),
        )
    }
}
