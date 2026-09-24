package app.speecher.android.dictation

import android.view.inputmethod.InputConnection

/** Both Android services share one process and one active dictation session. */
object ActiveDictation {
    @Volatile var engine: DictationEngine? = null
    @Volatile var connection: InputConnection? = null
    @Volatile var target: TargetApp? = null
    @Volatile var state: DictationState = DictationState.Connecting
    @Volatile var settings = SpeecherSettings()
    @Volatile var observe: ((DictationState) -> Unit)? = null
    @Volatile var onInserted: (() -> Unit)? = null
    /** What the chip read of the target window at tap, if the user opted in and it arrived. */
    @Volatile var screen: ScreenCapture? = null
    /** A base64 JPEG screenshot from the tap, if the user opted in and it arrived. */
    @Volatile var screenshotJpeg: String? = null

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
