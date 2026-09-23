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
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityWindowInfo
import androidx.compose.ui.platform.ComposeView
import app.speecher.android.dictation.ActiveDictation
import app.speecher.android.dictation.DictationEngine
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.SettingsStore
import app.speecher.android.dictation.createDictationEngine
import app.speecher.android.ui.DictationChip
import app.speecher.android.ui.SpeecherTheme
import kotlin.math.abs

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
        val size = (44 * resources.displayMetrics.density).toInt()
        val margin = (6 * resources.displayMetrics.density).toInt()
        val kb = Rect().also(keyboard::getBoundsInScreen)
        val mic = keyboard.micRect()
        val existing = chip
        // Sit on the keyboard's own voice button when we can read it, else on the top-right strip
        // where it usually lives. If detection blips to null while the chip is already up, hold the
        // last spot rather than jump to the fallback, so it never flips back and forth.
        val center =
            when {
                mic != null -> mic.centerX() to mic.centerY()
                existing != null -> return
                else -> (kb.right - size / 2 - margin) to (kb.top + size / 2 + margin)
            }
        val x = center.first - size / 2
        val y = center.second - size / 2
        // The suggestion strip fires window changes on every keystroke; ignore small jitter so a
        // settled chip never bounces.
        if (existing != null && abs(x - chipX) < JITTER && abs(y - chipY) < JITTER) return
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
                    gravity = Gravity.TOP or Gravity.START
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

    /** The screen bounds of the keyboard's own voice-input button, if it exposes one. */
    private fun AccessibilityWindowInfo.micRect(): Rect? {
        val voice = root?.let(::findVoiceNode) ?: return null
        return Rect().also(voice::getBoundsInScreen).takeIf { !it.isEmpty }
    }

    private fun findVoiceNode(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        val label = node.contentDescription?.toString()?.lowercase()
        if (label != null && ("voice" in label || "microphone" in label)) return node
        for (i in 0 until node.childCount) {
            node.getChild(i)?.let(::findVoiceNode)?.let {
                return it
            }
        }
        return null
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

    private companion object {
        const val JITTER = 24
    }
}
