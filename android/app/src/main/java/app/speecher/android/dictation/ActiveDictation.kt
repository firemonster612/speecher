package app.speecher.android.dictation

import android.view.inputmethod.InputConnection

/** Both Android services share one process and one active dictation session. */
object ActiveDictation {
    var engine: DictationEngine? = null
    var connection: InputConnection? = null
    var imeActive = false
}
