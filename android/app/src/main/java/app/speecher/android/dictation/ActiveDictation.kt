package app.speecher.android.dictation

import android.view.inputmethod.InputConnection
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.speecher.protocol.CorrectionWindow

/** Both Android services share one process and one active dictation session. */
object ActiveDictation {
    @Volatile var engine: DictationEngine? = null
    @Volatile var connection: InputConnection? = null
    @Volatile var target: TargetApp? = null
    @Volatile var state: DictationState = DictationState.Listening()
    @Volatile var settings = SpeecherSettings()
    @Volatile var observe: ((DictationState) -> Unit)? = null
    @Volatile var onInserted: (() -> Unit)? = null
    /**
     * Set while the dictation button's service runs: it watches the field an insertion went into
     * for the person's correction, given the inserted words and the target app's package.
     */
    @Volatile var watchCorrections: ((CorrectionWindow, String) -> Unit)? = null
    /** What the chip read of the target window at tap, if the user opted in and it arrived. */
    @Volatile var screen: ScreenCapture? = null
    /** A base64 JPEG screenshot from the tap, if the user opted in and it arrived. */
    @Volatile var screenshotJpeg: String? = null
    /**
     * Outlives the session, in memory only, so Home can copy the words again after they went into
     * the wrong field. A password field's text is never kept.
     */
    var latest by mutableStateOf<LatestTranscript?>(null)
    /**
     * While Android may be replacing the app, which would end a dictation. Set and read on the main
     * thread, so no dictation starts between the installer's check for one and its commit.
     */
    @Volatile var installingUpdate = false

    /**
     * Ends the running session: the engine closes and what the chip captured of the screen goes
     * with it. Shares a lock with [storeCapture], so a capture that lands late cannot survive.
     */
    @Synchronized
    fun end() {
        engine?.close()
        engine = null
        screen = null
        screenshotJpeg = null
    }

    /** Runs [store] only while [owner] is still the running session's engine. */
    @Synchronized
    fun storeCapture(owner: DictationEngine, store: ActiveDictation.() -> Unit) {
        if (engine === owner) store()
    }
}
