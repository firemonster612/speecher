package app.speecher.android.dictation

import android.view.inputmethod.InputConnection

/** Both Android services share one process and one active dictation session. */
object ActiveDictation {
    @Volatile var engine: DictationEngine? = null
    @Volatile var connection: InputConnection? = null
    @Volatile var state: DictationState = DictationState.Connecting
    @Volatile var settings = SpeecherSettings()
    @Volatile var observe: ((DictationState) -> Unit)? = null
    @Volatile var onInserted: (() -> Unit)? = null
}
