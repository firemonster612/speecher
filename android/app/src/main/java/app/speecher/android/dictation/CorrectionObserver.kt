package app.speecher.android.dictation

import android.os.Handler
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.view.inputmethod.InputConnection
import app.speecher.protocol.CORRECTION_CONTEXT_CHARS
import app.speecher.protocol.CORRECTION_FIRST_SAMPLE_MS
import app.speecher.protocol.CORRECTION_SAMPLE_INTERVAL_MS
import app.speecher.protocol.CORRECTION_WINDOW_MS
import app.speecher.protocol.CorrectionEvidence
import app.speecher.protocol.CorrectionTracker
import app.speecher.protocol.CorrectionWindow
import app.speecher.protocol.insertionLanded

/**
 * [inserted], about to go in at the caret, pinned by the text on each side of it; null when a
 * selection is there for it to replace, or the field will not say what surrounds the caret.
 */
fun correctionWindow(connection: InputConnection, inserted: String): CorrectionWindow? {
    val around =
        connection.getSurroundingText(CORRECTION_CONTEXT_CHARS, CORRECTION_CONTEXT_CHARS, 0)
            ?: return null
    val caret = around.selectionStart
    if (caret != around.selectionEnd || caret !in 0..around.text.length) return null
    return CorrectionWindow(
        inserted,
        around.text.substring(0, caret),
        around.text.substring(caret),
    )
}

/**
 * When the watch looks for the field the insertion went into. Right after Insert the focused node
 * can still be the window's root, as Compose reports focus late, so it looks a few times.
 */
private val LANDING_CHECKS_MS = listOf(250L, 500L, 1_000L, 1_500L)

/**
 * Watches the field an insertion went into for the person's correction, reading it on [handler]'s
 * thread from [CORRECTION_FIRST_SAMPLE_MS] after the insertion, every
 * [CORRECTION_SAMPLE_INTERVAL_MS], until [CORRECTION_WINDOW_MS], as the desktop does over AT-SPI.
 * The accessibility service owns it because the keyboard cannot see the edit: it hands back to the
 * person's own keyboard on Insert. [focusedField] finds the field with input focus, [learning] is
 * the live setting, [watchTextChanges] turns the service's text-change events on while a field is
 * watched and off after, and [onCorrection] gets each correction and the app's package.
 */
class CorrectionObserver(
    private val handler: Handler,
    private val focusedField: () -> AccessibilityNodeInfo?,
    private val learning: () -> Boolean,
    private val watchTextChanges: (Boolean) -> Unit,
    private val onCorrection: (CorrectionEvidence, String) -> Unit,
) {
    /** The field the insertion went into, once found. */
    private var field: AccessibilityNodeInfo? = null
    private var window: CorrectionWindow? = null
    private var tracker: CorrectionTracker? = null
    private var applicationId = ""
    private val checkLanding = Runnable { checkLanding() }
    private val read = Runnable { sample() }

    /**
     * Starts watching for an edit to [window] in the app [applicationId], ending any earlier watch.
     */
    fun observe(window: CorrectionWindow, applicationId: String) {
        cancel()
        this.window = window
        tracker = CorrectionTracker(window)
        this.applicationId = applicationId
        LANDING_CHECKS_MS.forEach { handler.postDelayed(checkLanding, it) }
        val delays =
            CORRECTION_FIRST_SAMPLE_MS..CORRECTION_WINDOW_MS step CORRECTION_SAMPLE_INTERVAL_MS
        delays.forEach { handler.postDelayed(read, it) }
    }

    /**
     * Ends the watch when a field in the watched field's window is emptied, as when a message is
     * sent: what is typed next is a new message, not an edit of the dictation, and a poll could
     * miss the empty field. Another field emptied there ends it too, which only costs a correction.
     */
    fun textChanged(event: AccessibilityEvent) {
        // Its window, not its node: the event's source costs a round trip, on every keystroke.
        if (field?.windowId != event.windowId) return
        // Judged from the event: by the time it is handled the field may hold a new message. A
        // field showing its hint after a send reports the hint as its text, but removed all of it.
        val emptied =
            event.text.all { it.isNullOrEmpty() } ||
                (event.addedCount == 0 &&
                    event.removedCount > 0 &&
                    event.removedCount == event.beforeText?.length)
        if (emptied) cancel()
    }

    fun cancel() {
        if (field != null) watchTextChanges(false)
        handler.removeCallbacks(checkLanding)
        handler.removeCallbacks(read)
        field = null
        window = null
        tracker = null
    }

    // Focus may have moved on since the insertion, so the field must show the inserted words.
    private fun checkLanding() {
        val window = window ?: return
        val focused =
            focusedField()?.takeIf { it.isEditable && it.packageName?.toString() == applicationId }
                ?: return
        if (text(focused)?.let { insertionLanded(it, window) } == true) {
            field = focused
            watchTextChanges(true)
            handler.removeCallbacks(checkLanding)
        }
    }

    /** [field]'s text now; null when it has gone, or hides its text. */
    private fun text(field: AccessibilityNodeInfo): String? {
        if (!field.refresh() || field.isPassword) return null
        return if (field.isShowingHintText) "" else field.text?.toString().orEmpty()
    }

    private fun sample() {
        val tracker = tracker ?: return
        // Turning learning off ends the watch before the field is read again, as on the desktop.
        val text = field?.takeIf { learning() }?.let(::text)
        if (text == null) {
            cancel()
            return
        }
        val evidence = tracker.sample(text)
        if (!tracker.active) cancel()
        evidence?.let { onCorrection(it, applicationId) }
    }
}
