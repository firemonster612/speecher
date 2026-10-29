package app.speecher.android.dictation

import android.os.Looper
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.SurroundingText
import app.speecher.android.SpeecherChipService
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class DictationSessionTest {
    private fun session() =
        DictationSession(RuntimeEnvironment.getApplication(), null, { _, _ -> }, {})

    @Test
    fun `a destroyed chip service starts no correction watch when the words go in`() {
        val controller = Robolectric.buildService(SpeecherChipService::class.java).create()
        val chip = controller.get()
        SpeecherChipService::class
            .java
            .getDeclaredMethod("onServiceConnected")
            .apply { isAccessible = true }
            .invoke(chip)
        val session = session()
        session.target = TargetApp("com.example", "Example", null, false)
        session.connection =
            object : BaseInputConnection(View(chip), true) {
                override fun getSurroundingText(beforeLength: Int, afterLength: Int, flags: Int) =
                    SurroundingText("", 0, 0, 0)

                override fun commitText(text: CharSequence?, newCursorPosition: Int) = true
            }
        // Insert on a failed commit runs the real commit wiring without a microphone or provider.
        DictationEngine::class
            .java
            .getDeclaredField("state")
            .apply { isAccessible = true }
            .set(session.engine, DictationState.Failed(FailureReason.Commit, "", "hello"))
        controller.destroy()

        session.engine.insert()
        shadowOf(Looper.getMainLooper()).idle()

        val corrections =
            SpeecherChipService::class
                .java
                .getDeclaredField("corrections")
                .apply { isAccessible = true }
                .get(chip)
        val watched =
            CorrectionObserver::class
                .java
                .getDeclaredField("window")
                .apply { isAccessible = true }
                .get(corrections)
        assertNull(watched)
        session.end()
    }

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
