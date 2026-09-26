package app.speecher.android

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.inputmethodservice.InputMethodService
import android.view.View
import android.view.WindowInsets
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.platform.ComposeView
import androidx.core.view.WindowCompat
import app.speecher.android.auth.TokenStore
import app.speecher.android.dictation.ActiveDictation
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.FailureReason
import app.speecher.android.dictation.resolveSignedIn
import app.speecher.android.dictation.shownPanelSize
import app.speecher.android.dictation.targetApp
import app.speecher.android.ui.DictationPanel
import app.speecher.android.ui.SpeecherTheme

/** The id Android uses for this keyboard: the short `package/.Class` form of its component. */
fun speecherImeId(context: Context): String =
    ComponentName(context, SpeecherImeService::class.java).flattenToShortString()

class SpeecherImeService : InputMethodService() {
    private val owner = ServiceViewOwner()
    private var panelState = mutableStateOf<DictationState>(DictationState.Listening())
    /** Whether the minimize control flipped this session's panel away from its chosen size. */
    private val sizeToggled = mutableStateOf(false)
    private var panel: View? = null

    override fun onCreate() {
        super.onCreate()
        ImeSwap(this).let { if (it.stranded()) it.switchBack(this) }
        panelState.value = ActiveDictation.state
        ActiveDictation.observe = ::showState
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
                        ActiveDictation.settings.panelSize,
                        sizeToggled.value,
                        panelState.value,
                    )
                // A new height needs a layout pass, and that pass is what recomputes the insets.
                LaunchedEffect(size) { view.requestLayout() }
                SpeecherTheme {
                    DictationPanel(
                        panelState.value,
                        ActiveDictation.settings.shownButtonLayout,
                        size,
                        onToggleSize = { sizeToggled.value = !sizeToggled.value },
                        onCancel = ::switchBack,
                        onInsert = { ActiveDictation.engine?.insert() },
                        onInsertRefined = {
                            ActiveDictation.engine?.insertRefined(
                                resolveSignedIn(
                                    ActiveDictation.settings.refinementProvider,
                                    TokenStore(this).signedIn(),
                                )
                            )
                        },
                        onRecover = ::recover,
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
        ActiveDictation.connection = currentInputConnection
        ActiveDictation.target = attribute?.let { targetApp(it, packageManager) }
    }

    override fun onFinishInput() {
        ActiveDictation.connection = null
        ActiveDictation.target = null
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
        keepScreenOn(panelState.value.takeIf { ActiveDictation.engine != null })
    }

    override fun onWindowHidden() {
        super.onWindowHidden()
        handler.postDelayed(dismiss, DISMISS_GRACE_MILLIS)
    }

    fun showState(state: DictationState) {
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
        if (
            ActiveDictation.settings.keepScreenOn &&
                state != null &&
                state !is DictationState.Failed
        )
            panel.addFlags(flag)
        else panel.clearFlags(flag)
    }

    /** Network and provider failures retry in place; the others need the app. */
    private fun recover() {
        val failed = panelState.value as? DictationState.Failed ?: return
        if (failed.reason == FailureReason.Network || failed.reason == FailureReason.Provider) {
            ActiveDictation.engine?.retry()
            return
        }
        switchBack()
        val intent = Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (failed.reason == FailureReason.SignedOut)
            intent.putExtra("sign_in_provider", failed.provider?.name)
        startActivity(intent)
    }

    private fun switchBack() {
        ActiveDictation.end()
        sizeToggled.value = false
        keepScreenOn(null)
        ImeSwap(this).switchBack(this)
    }

    override fun onDestroy() {
        handler.removeCallbacks(dismiss)
        ActiveDictation.observe = null
        ActiveDictation.onInserted = null
        panel = null
        owner.destroy()
        super.onDestroy()
    }
}

private const val DISMISS_GRACE_MILLIS = 750L
