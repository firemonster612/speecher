package app.speecher.android

import android.accessibilityservice.AccessibilityService
import android.content.Intent
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.WindowManager
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityWindowInfo
import androidx.compose.ui.platform.ComposeView
import app.speecher.android.dictation.ActiveDictation
import app.speecher.android.dictation.DictationEngine
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.SettingsStore
import app.speecher.android.dictation.createDictationEngine
import app.speecher.android.ui.DictationChip
import app.speecher.android.ui.SpeecherTheme

class SpeecherChipService : AccessibilityService() {
    private val handler = Handler(Looper.getMainLooper())
    private val owner = ServiceViewOwner()
    private val window by lazy { getSystemService(WINDOW_SERVICE) as WindowManager }
    private var chip: ComposeView? = null
    private var chipX = Int.MIN_VALUE
    private var chipY = Int.MIN_VALUE
    private var passwordFocused = false
    private val refresh = Runnable { updateChip() }

    override fun onServiceConnected() {
        super.onServiceConnected()
        // Recover a swap a dead process never undid: our keyboard is default with nothing
        // dictating.
        val swap = ImeSwap(this)
        if (swap.stranded()) {
            swap.previousId?.let { softKeyboardController.switchToInputMethod(it) }
            swap.clear()
        }
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent) {
        if (event.eventType == AccessibilityEvent.TYPE_VIEW_FOCUSED) {
            passwordFocused = event.source?.isPassword == true
        }
        handler.removeCallbacks(refresh)
        handler.postDelayed(refresh, 50)
    }

    override fun onInterrupt() {
        removeChip()
    }

    private fun updateChip() {
        val ownIme = speecherImeId(this)
        if (
            passwordFocused ||
                Settings.Secure.getString(contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD) ==
                    ownIme
        ) {
            removeChip()
            return
        }
        val keyboard = windows.firstOrNull { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD }
        if (keyboard == null) {
            removeChip()
            return
        }
        val bounds = Rect()
        keyboard.getBoundsInScreen(bounds)
        val size = (44 * resources.displayMetrics.density).toInt()
        val margin = (6 * resources.displayMetrics.density).toInt()
        // Sit on the keyboard's top-right strip, over Gboard's own mic, so it never covers the app.
        val x = resources.displayMetrics.widthPixels - bounds.right + margin
        val y = bounds.top + margin
        val existing = chip
        // The suggestion strip fires window changes on every keystroke; don't move a settled chip.
        if (existing != null && x == chipX && y == chipY) return
        val params =
            WindowManager.LayoutParams(
                    size,
                    size,
                    WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                        WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
                    android.graphics.PixelFormat.TRANSLUCENT,
                )
                .apply {
                    gravity = Gravity.TOP or Gravity.END
                    this.x = x
                    this.y = y
                    fitInsetsTypes = 0
                    layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                }
        chipX = x
        chipY = y
        if (existing != null) {
            window.updateViewLayout(existing, params)
            return
        }
        val view = ComposeView(this)
        owner.attach(view)
        view.setContent { SpeecherTheme { DictationChip(onClick = ::onChipTap) } }
        window.addView(view, params)
        chip = view
    }

    private fun onChipTap() {
        ImeSwap(this).rememberPrevious()
        val engine = startDictation()
        val switched = runCatching {
            softKeyboardController.switchToInputMethod(speecherImeId(this))
        }
            .getOrDefault(false)
        if (switched) {
            removeChip()
        } else {
            // Our keyboard isn't enabled; setup isn't finished, so send them there.
            engine.close()
            ActiveDictation.engine = null
            startActivity(
                Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        }
    }

    /** The mic starts on the tap, before the keyboard swap lands, to cover the swap gap. */
    private fun startDictation(): DictationEngine {
        val settings = SettingsStore(this).load()
        ActiveDictation.settings = settings
        ActiveDictation.state = DictationState.Connecting
        ActiveDictation.observe?.invoke(DictationState.Connecting)
        ActiveDictation.engine?.close()
        val engine =
            createDictationEngine(
                this,
                settings,
                { ActiveDictation.connection },
                { state ->
                    ActiveDictation.state = state
                    ActiveDictation.observe?.invoke(state)
                },
                { ActiveDictation.onInserted?.invoke() },
            )
        ActiveDictation.engine = engine
        engine.start(settings.transcriptionProvider)
        return engine
    }

    private fun removeChip() {
        chip?.let(window::removeView)
        chip = null
        chipX = Int.MIN_VALUE
        chipY = Int.MIN_VALUE
    }

    override fun onDestroy() {
        handler.removeCallbacks(refresh)
        removeChip()
        owner.destroy()
        super.onDestroy()
    }
}
