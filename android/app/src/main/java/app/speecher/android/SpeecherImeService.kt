package app.speecher.android

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.inputmethodservice.InputMethodService
import android.view.HapticFeedbackConstants
import android.view.View
import android.view.WindowInsets
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.platform.ComposeView
import androidx.core.view.WindowCompat
import app.speecher.android.dictation.ActiveDictation
import app.speecher.android.dictation.DictationSession
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.Recovery
import app.speecher.android.dictation.recovery
import app.speecher.android.dictation.shownPanelSize
import app.speecher.android.dictation.targetApp
import app.speecher.android.settings.SpeecherSettings
import app.speecher.android.ui.DictationPanel
import app.speecher.android.ui.SpeecherTheme

/** The id Android uses for this keyboard: the short `package/.Class` form of its component. */
fun speecherImeId(context: Context): String =
    ComponentName(context, SpeecherImeService::class.java).flattenToShortString()

class SpeecherImeService : InputMethodService() {
    private val owner = ServiceViewOwner()
    private var panelState = mutableStateOf<DictationState>(DictationState.Listening())
    /** The settings of the session the panel shows, kept after it ends while the panel hides. */
    private val settings = mutableStateOf(SpeecherSettings())
    /** Whether the minimize control flipped this session's panel away from its chosen size. */
    private val sizeToggled = mutableStateOf(false)
    private var panel: View? = null

    override fun onCreate() {
        super.onCreate()
        ActiveDictation.session?.let { show(it, it.engine.state) }
        ActiveDictation.observe = ::show
        ActiveDictation.onInserted = ::switchBack
    }

    override fun onEvaluateInputViewShown(): Boolean {
        super.onEvaluateInputViewShown()
        return true
    }

    override fun onCreateInputView(): View =
        ComposeView(this).also { view ->
            panel = view
            // Draw edge-to-edge so the nav-bar inset reaches Compose instead of being consumed
            // first; otherwise navigationBarsPadding() collapses to 0 and the panel's background
            // stops short of the bottom edge, leaving the app behind the keyboard showing through.
            window.window?.let { WindowCompat.setDecorFitsSystemWindows(it, false) }
            // Compose looks up its owners from the window root, which the IME framework owns.
            window.window?.decorView?.let(owner::attach)
            owner.attach(view)
            view.setContent {
                val size =
                    shownPanelSize(
                        settings.value.panelSize,
                        sizeToggled.value,
                        panelState.value,
                    )
                // A new height needs a layout pass, and that pass is what recomputes the insets.
                LaunchedEffect(size) { view.requestLayout() }
                SpeecherTheme {
                    DictationPanel(
                        panelState.value,
                        settings.value.shownButtonLayout,
                        size,
                        onToggleSize = { sizeToggled.value = !sizeToggled.value },
                        onCancel = ::switchBack,
                        onInsert = {
                            val session = ActiveDictation.session
                            session?.bindInsert()
                            if (session?.engine?.insert() == true)
                                view.vibrateForStartOrStop(settings.value)
                        },
                        onInsertRefined = {
                            val session = ActiveDictation.session
                            session?.bindInsert()
                            if (session?.engine?.insertRefined(session.cleanup) == true)
                                view.vibrateForStartOrStop(settings.value)
                        },
                        onRecover = ::recover,
                        onPause = { ActiveDictation.session?.engine?.pause() },
                        onResume = { ActiveDictation.session?.engine?.resume() },
                        transcriptionPreview = settings.value.transcriptionPreviewEnabled,
                        refinementPreview = settings.value.refinementPreviewEnabled,
                    )
                }
            }
        }

    /**
     * Tells Android the panel covers only itself: the app behind resizes to the panel's top, and
     * touches anywhere else in the IME window, including the gesture-navigation strip under the
     * panel, go to the app or the system.
     */
    override fun onComputeInsets(outInsets: Insets) {
        super.onComputeInsets(outInsets)
        val view = panel?.takeIf { it.isAttachedToWindow && !isFullscreenMode } ?: return
        val location = IntArray(2).also(view::getLocationInWindow)
        val top = location[1]
        val navigationBar =
            view.rootWindowInsets?.getInsets(WindowInsets.Type.navigationBars())?.bottom ?: 0
        outInsets.contentTopInsets = top
        outInsets.visibleTopInsets = top
        outInsets.touchableInsets = Insets.TOUCHABLE_INSETS_REGION
        outInsets.touchableRegion.set(
            location[0],
            top,
            location[0] + view.width,
            top + view.height - navigationBar,
        )
    }

    override fun onStartInput(attribute: EditorInfo?, restarting: Boolean) {
        super.onStartInput(attribute, restarting)
        ActiveDictation.session?.let { session ->
            session.connection = currentInputConnection
            session.target = attribute?.let { targetApp(it, packageManager) }
            if (!restarting) session.fieldsStarted++
        }
    }

    override fun onFinishInput() {
        ActiveDictation.session?.let { session ->
            session.connection = null
            session.target = null
        }
        super.onFinishInput()
    }

    /**
     * A hide is a dismissal only if the panel stays hidden. Apps that recreate on rotation hide and
     * re-show the keyboard within a few hundred milliseconds, and that must not end the dictation.
     */
    private val dismiss = Runnable {
        ActiveDictation.end()
        sizeToggled.value = false
        keepScreenOn(null)
        if (
            android.provider.Settings.Secure.getString(
                contentResolver,
                android.provider.Settings.Secure.DEFAULT_INPUT_METHOD,
            ) == speecherImeId(this)
        )
            switchBack()
    }
    private val handler = android.os.Handler(android.os.Looper.getMainLooper())

    override fun onWindowShown() {
        handler.removeCallbacks(dismiss)
        super.onWindowShown()
        keepScreenOn(panelState.value.takeIf { ActiveDictation.session != null })
    }

    override fun onWindowHidden() {
        super.onWindowHidden()
        handler.postDelayed(dismiss, DISMISS_GRACE_MILLIS)
    }

    private fun show(session: DictationSession, state: DictationState) {
        settings.value = session.settings
        panelState.value = state
        keepScreenOn(state)
    }

    /**
     * Holds the display awake through a running session, [state] being its latest; null or a
     * failure means no session is running. The window flag lapses with the panel, unlike a wake
     * lock.
     */
    private fun keepScreenOn(state: DictationState?) {
        val flag = WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
        val panel = window.window ?: return
        if (settings.value.keepScreenOn && state != null && state !is DictationState.Failed)
            panel.addFlags(flag)
        else panel.clearFlags(flag)
    }

    /** Retries the failure in place, or switches back and hands its recovery to the app. */
    private fun recover() {
        val recovery = (panelState.value as? DictationState.Failed)?.reason?.recovery ?: return
        if (recovery == Recovery.Retry) {
            val session = ActiveDictation.session
            session?.bindInsert()
            if (session?.engine?.retry() == true) panel?.vibrateForStartOrStop(settings.value)
            return
        }
        switchBack()
        startActivity(
            Intent(this, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                .putExtra(RECOVERY, recovery)
        )
    }

    private fun switchBack() {
        ActiveDictation.end()
        sizeToggled.value = false
        keepScreenOn(null)
        ImeSwap(this).switchBack(this)
    }

    override fun onDestroy() {
        handler.removeCallbacks(dismiss)
        // Without its panel nothing can insert or cancel the dictation, so it ends with the IME.
        ActiveDictation.end()
        ActiveDictation.observe = null
        ActiveDictation.onInserted = null
        panel = null
        owner.destroy()
        super.onDestroy()
    }
}

private const val DISMISS_GRACE_MILLIS = 750L

/**
 * The desktop's start and stop sounds, as a vibration when the person turned it on in the session's
 * [settings]. The view's haptics follow the phone's touch feedback setting.
 */
fun View.vibrateForStartOrStop(settings: SpeecherSettings) {
    if (settings.vibrationEnabled) performHapticFeedback(HapticFeedbackConstants.CONFIRM)
}
