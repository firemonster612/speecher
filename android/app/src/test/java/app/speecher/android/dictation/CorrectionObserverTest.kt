package app.speecher.android.dictation

import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import app.speecher.protocol.CORRECTION_WINDOW_MS
import app.speecher.protocol.CorrectionEvidence
import app.speecher.protocol.CorrectionWindow
import java.time.Duration
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class CorrectionObserverTest {
    private val view = View(RuntimeEnvironment.getApplication())
    private val field =
        AccessibilityNodeInfo(view).apply {
            isEditable = true
            packageName = "com.Slack"
            text = "ping the team on speaker"
            shadowOf(this).setRefreshReturnValue(true)
        }
    private val learned = mutableListOf<CorrectionEvidence>()
    private var learning = true
    private val observer =
        CorrectionObserver(Handler(Looper.getMainLooper()), { field }, { learning }, {}) {
            evidence,
            _ ->
            learned += evidence
        }

    /** Dictated into an empty field, then corrected a second in. */
    private fun dictateAndCorrect(app: String = "com.Slack", meanwhile: () -> Unit = {}) {
        observer.observe(CorrectionWindow("ping the team on speaker", "", ""), app)
        idle(1_000)
        meanwhile()
        field.text = "ping the team on Speecher"
        idle(CORRECTION_WINDOW_MS)
    }

    private fun idle(millis: Long) =
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(millis))

    @Test
    fun `an edit to the field the words went into is learned`() {
        dictateAndCorrect()
        assertEquals(listOf(CorrectionEvidence("speaker", "Speecher", 0.75)), learned)
    }

    @Test
    fun `a focused field in another app is not watched`() {
        dictateAndCorrect(app = "com.google.android.gm")
        assertEquals(emptyList<CorrectionEvidence>(), learned)
    }

    @Test
    fun `turning learning off ends the watch`() {
        dictateAndCorrect { learning = false }
        assertEquals(emptyList<CorrectionEvidence>(), learned)
    }

    @Test
    fun `emptying the field ends the watch, even once a new message fills it`() {
        dictateAndCorrect {
            val sent =
                AccessibilityEvent(AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED).apply { text.add("") }
            sent.setSource(view)
            observer.textChanged(sent)
        }
        assertEquals(emptyList<CorrectionEvidence>(), learned)
    }
}
