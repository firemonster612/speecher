package app.speecher.android.dictation

import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class DictationSessionTest {
    private fun session() =
        DictationSession(RuntimeEnvironment.getApplication(), null, { _, _ -> }, {})

    @Test
    fun `ending drops the screen captures and any that arrive later`() {
        val session = session()
        session.screen = ScreenCapture("Chat", "private text")
        session.screenshotJpeg = "jpeg"

        session.end()
        session.screen = ScreenCapture("Chat", "late text")
        session.screenshotJpeg = "late jpeg"

        assertNull(session.screen)
        assertNull(session.screenshotJpeg)
    }
}
