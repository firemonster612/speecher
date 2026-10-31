package app.speecher.android.dictation

import android.os.Looper
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.SurroundingText
import app.speecher.protocol.CorrectionWindow
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class CommitToFieldTest {
    private val context = RuntimeEnvironment.getApplication()

    /**
     * Whether "hello" went into Example's field, "Hi " before its caret, what was kept and what
     * watched: the field takes it when it [accepts]; the person may [leave] the field after tapping
     * Insert, and the chip service may be [gone] before the watch starts.
     */
    private fun commit(accepts: Boolean = true, leave: Boolean = false, gone: Boolean = false) =
        mutableListOf<Pair<CorrectionWindow, String>>().let { watched ->
            ActiveDictation.watchCorrections = { window, app -> watched += window to app }
            latestTranscript = null
            val target = TargetApp("com.example", "Example", null, secure = false)
            val session = DictationSession(context, target, { _, _ -> }, {})
            val field =
                object : BaseInputConnection(View(context), true) {
                    override fun getSurroundingText(before: Int, after: Int, flags: Int) =
                        SurroundingText("Hi ", 3, 3, 0)

                    override fun commitText(text: CharSequence?, cursor: Int) = accepts
                }
            session.startInput(field, restarting = false)
            session.bindField()
            if (leave) {
                val other = BaseInputConnection(View(context), true)
                session.startInput(other, restarting = false)
                // The new field's connection is never seen with the old field's binding.
                assertSame(other, session.connection)
            }
            val committed = commitToField(context, session, "hello")
            if (gone) ActiveDictation.watchCorrections = null
            shadowOf(Looper.getMainLooper()).idle()
            session.end()
            ActiveDictation.watchCorrections = null
            listOf(committed, latestTranscript, watched)
        }

    @Test
    fun `words are kept whether or not they go in, and only words that go in are watched`() {
        val kept = LatestTranscript("hello", "Example")
        val watch = listOf(CorrectionWindow("hello", "Hi ", "") to "com.example")
        assertEquals(listOf(true, kept, watch), commit())
        assertEquals(listOf(false, kept, emptyList<Any>()), commit(accepts = false))
        assertEquals(listOf(false, kept, emptyList<Any>()), commit(leave = true))
        assertEquals(listOf(true, kept, emptyList<Any>()), commit(gone = true))
    }
}
