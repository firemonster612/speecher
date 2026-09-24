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

    fun clearScreen() {
        screen = null
        screenshotJpeg = null
    }
}
