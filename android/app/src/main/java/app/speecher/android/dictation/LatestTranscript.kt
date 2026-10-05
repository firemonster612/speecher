package app.speecher.android.dictation

/** The text Speecher last tried to insert and the app's name, when it had a field. */
data class LatestTranscript(val text: String, val app: String?)
