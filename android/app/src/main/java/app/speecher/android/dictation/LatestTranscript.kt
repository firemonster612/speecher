package app.speecher.android.dictation

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** The text Speecher last tried to insert and the app's name, when it had a field. */
data class LatestTranscript(val text: String, val app: String?)

/**
 * Outlives the session, in memory only, so Home can copy the words again after they went into the
 * wrong field. A password field's text is never kept.
 */
var latestTranscript by mutableStateOf<LatestTranscript?>(null)
