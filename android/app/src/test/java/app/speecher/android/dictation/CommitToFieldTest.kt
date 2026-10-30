package app.speecher.android.dictation

import android.os.Looper
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.SurroundingText
import app.speecher.protocol.CorrectionWindow
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class CommitToFieldTest {
    private val context = RuntimeEnvironment.getApplication()
    private val watched = mutableListOf<Pair<CorrectionWindow, String>>()

    @Before
    fun watchCorrections() {
        ActiveDictation.watchCorrections = { window, app -> watched += window to app }
    }

    @After
    fun reset() {
        ActiveDictation.watchCorrections = null
        latestTranscript = null
    }

    /**
     * A session dictating into Example's field, "Hi " before its caret, which takes the text when
     * [accepts].
     */
    private fun session(accepts: Boolean = true) =
        DictationSession(
                context,
                TargetApp("com.example", "Example", null, secure = false),
                { _, _ -> },
                {},
            )
            .apply {
                startInput(
                    object : BaseInputConnection(View(context), true) {
                        override fun getSurroundingText(
                            beforeLength: Int,
                            afterLength: Int,
                            flags: Int,
                        ) = SurroundingText("Hi ", 3, 3, 0)

                        override fun commitText(text: CharSequence?, newCursorPosition: Int) =
                            accepts
                    },
                    restarting = false,
                )
                bindField()
            }

    @Test
    fun `words that go in are kept and their field watched for a correction`() {
        val session = session()

        assertTrue(commitToField(context, session, "hello"))
        shadowOf(Looper.getMainLooper()).idle()

        assertEquals(LatestTranscript("hello", "Example"), latestTranscript)
        assertEquals(listOf(CorrectionWindow("hello", "Hi ", "") to "com.example"), watched)
        session.end()
    }

    @Test
    fun `words the field refuses are kept but not watched`() {
        val session = session(accepts = false)

        assertFalse(commitToField(context, session, "hello"))
        shadowOf(Looper.getMainLooper()).idle()

        assertEquals(LatestTranscript("hello", "Example"), latestTranscript)
        assertEquals(emptyList<Pair<CorrectionWindow, String>>(), watched)
        session.end()
    }

    @Test
    fun `words for a field the person left since tapping Insert are kept but not inserted`() {
        val session = session()
        val other = BaseInputConnection(View(context), true)
        session.startInput(other, restarting = false)

        // The new field's connection is never seen with the old field's binding.
        assertSame(other, session.connection)
        assertNull(session.insertConnection)
        assertFalse(commitToField(context, session, "hello"))
        shadowOf(Looper.getMainLooper()).idle()

        assertEquals(LatestTranscript("hello", "Example"), latestTranscript)
        assertEquals(emptyList<Pair<CorrectionWindow, String>>(), watched)
        session.end()
    }

    @Test
    fun `a chip service destroyed before the watch starts watches nothing`() {
        val session = session()

        commitToField(context, session, "hello")
        // What the chip service does as it is destroyed.
        ActiveDictation.watchCorrections = null
        shadowOf(Looper.getMainLooper()).idle()

        assertEquals(emptyList<Pair<CorrectionWindow, String>>(), watched)
        session.end()
    }
}
