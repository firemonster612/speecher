package app.speecher.android.dictation

import android.content.Context
import android.os.Handler
import android.os.Looper
import app.speecher.android.settings.SettingsStore

/**
 * Commits [text] into [session]'s field, with what goes along with an insertion: the text is kept
 * as the latest transcript whether or not it goes in, and once it does its words count as
 * vocabulary use and the chip service watches the field for the person's correction. A password
 * field's text is neither kept nor watched. False when there is no field or it refused the text.
 */
fun commitToField(context: Context, session: DictationSession, text: String): Boolean {
    // Kept before the commit, as the desktop does, so words that fail to go in stay too.
    val target = session.target
    if (target?.secure != true) latestTranscript = LatestTranscript(text, target?.label)
    val connection = session.connection
    // Read before the commit: the text on each side pins the words for an edit after it.
    val window =
        connection
            ?.takeIf {
                session.settings.correctionLearningEnabled &&
                    target?.secure == false &&
                    ActiveDictation.watchCorrections != null
            }
            ?.let { correctionWindow(it, text) }
    val committed = connection?.commitText(text, 1) == true
    if (committed) {
        SettingsStore(context).recordVocabularyUsage(text)
        // Read when it runs: the chip service may have gone in the meantime.
        if (window != null && target != null)
            Handler(Looper.getMainLooper()).post {
                ActiveDictation.watchCorrections?.invoke(window, target.packageName)
            }
    }
    return committed
}
