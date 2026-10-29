package app.speecher.android.dictation

import android.content.Context
import android.view.inputmethod.InputConnection
import app.speecher.android.auth.TokenStore
import app.speecher.android.provider.cleanup
import app.speecher.android.provider.resolveSignedIn
import app.speecher.android.provider.resolveSpeech
import app.speecher.android.settings.SettingsStore
import app.speecher.protocol.CorrectionWindow

/**
 * One Dictation Session, from the chip tap until the IME inserts, cancels or dismisses it. It
 * settles at the tap what the dictation uses, its settings and both providers, holds the field it
 * dictates into and what the chip captured of the screen, and runs the engine. [end] stops all of
 * it at once.
 */
class DictationSession(
    context: Context,
    /** The app the chip found focused at the tap, until the IME reads the field itself. */
    @Volatile var target: TargetApp?,
    /** Told each state of this session on the main thread, until it ends. */
    onState: (DictationSession, DictationState) -> Unit,
    /** Told on the main thread once the words went in, unless the session ended first. */
    onInserted: () -> Unit,
) {
    val settings = SettingsStore(context).load()
    private val signedIn = TokenStore(context).signedIn()
    val speech = resolveSpeech(settings.transcriptionProvider, signedIn)
    /** What Insert refined cleans up with. */
    val cleanup = resolveSignedIn(settings.refinementProvider.account, signedIn).cleanup
    /** The field's connection while the IME has one. */
    @Volatile var connection: InputConnection? = null
    /**
     * What the chip read of the target window at tap, if the user opted in and it arrived. Dropped
     * when it arrives after the session ended.
     */
    @Volatile
    var screen: ScreenCapture? = null
        @Synchronized
        set(value) {
            if (!ended) field = value
        }

    /** A base64 JPEG screenshot from the tap, kept like [screen]. */
    @Volatile
    var screenshotJpeg: String? = null
        @Synchronized
        set(value) {
            if (!ended) field = value
        }

    @Volatile private var ended = false
    // Last, since the engine's wiring reads the session.
    val engine =
        createDictationEngine(
            context,
            this,
            { if (!ended) onState(this, it) },
            { if (!ended) onInserted() },
        )

    /**
     * Closes the engine and drops what the chip captured of the screen, along with any capture or
     * state that arrives later.
     */
    fun end() {
        synchronized(this) {
            screen = null
            screenshotJpeg = null
            ended = true
        }
        // Outside the lock, so a capture callback never waits on the engine's.
        engine.close()
    }
}

/**
 * Both Android services share one process: the chip service starts each session here, and the IME
 * shows it and ends it. Used on the main thread.
 */
object ActiveDictation {
    /** The running session, until the IME ends it or the chip finds the keyboard can't show. */
    @Volatile
    var session: DictationSession? = null
        private set

    /** Set while the IME exists: shows the running session's state on its panel. */
    @Volatile var observe: ((DictationSession, DictationState) -> Unit)? = null
    /** Set while the IME exists: switches back once the running session's words went in. */
    @Volatile var onInserted: (() -> Unit)? = null
    /**
     * Set while the chip service runs: it watches the field an insertion went into for the person's
     * correction, given the inserted words and the target app's package.
     */
    @Volatile var watchCorrections: ((CorrectionWindow, String) -> Unit)? = null

    /**
     * Ends the running session and starts listening for one dictating into [target]. The mic starts
     * here, on the tap, before the keyboard swap lands, to cover the swap gap.
     */
    fun start(context: Context, target: TargetApp?): DictationSession {
        end()
        val started =
            DictationSession(
                context,
                target,
                { running, state -> observe?.invoke(running, state) },
                { onInserted?.invoke() },
            )
        session = started
        observe?.invoke(started, DictationState.Listening())
        started.engine.start(started.speech)
        return started
    }

    fun end() {
        session?.end()
        session = null
    }
}
