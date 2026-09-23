package app.speecher.android

import android.accessibilityservice.AccessibilityService
import android.content.Intent
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.WindowInsets
import android.view.WindowManager
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityWindowInfo
import androidx.compose.ui.platform.ComposeView
import app.speecher.android.auth.TokenStore
import app.speecher.android.dictation.ActiveDictation
import app.speecher.android.dictation.DictationEngine
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.SettingsStore
import app.speecher.android.dictation.createDictationEngine
import app.speecher.android.dictation.resolveSignedIn
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
    // The dragged chip's top-left, as a pixel offset from the keyboard's bottom-right corner. Null
    // means auto-place it: on the keyboard's mic node, else a safe fallback corner.
    private var chipOffsetX: Int? = null
    private var chipOffsetY: Int? = null
    @Volatile private var dragging = false
    private var passwordFocused = false
    private val refresh = Runnable { updateChip() }

    override fun onServiceConnected() {
        super.onServiceConnected()
        SettingsStore(this).load().let {
            chipOffsetX = it.chipOffsetX
            chipOffsetY = it.chipOffsetY
        }
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
        // A drag owns the position; don't let a keystroke's window change recompute it out from
        // under the user's finger.
        if (dragging) return
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
        val density = resources.displayMetrics.density
        val width = (CHIP_WIDTH_DP * density).toInt()
        val height = (CHIP_HEIGHT_DP * density).toInt()
        val margin = (MARGIN_DP * density).toInt()
        val kb = Rect().also(keyboard::getBoundsInScreen)
        val existing = chip
        val offsetX = chipOffsetX
        val offsetY = chipOffsetY
        val topLeft =
            if (offsetX != null && offsetY != null) {
                // The user placed it; keep it anchored to the keyboard's bottom-right corner so it
                // survives the keyboard changing height or hiding and re-showing.
                (kb.right + offsetX) to (kb.bottom + offsetY)
            } else {
                val mic = keyboard.micRect()
                // Sit on the keyboard's own voice button when we can read it. Otherwise use the
                // bottom-right corner, clear of the top toolbar/suggestion strip that many
                // keyboards fill with their own controls; the user can drag it from there. If
                // detection blips to null while the chip is already up, hold the last spot.
                val center =
                    when {
                        mic != null -> mic.centerX() to mic.centerY()
                        existing != null -> return
                        else -> (kb.right - width / 2 - margin) to (kb.bottom - height / 2 - margin)
                    }
                (center.first - width / 2) to (center.second - height / 2)
            }
        val (x, y) = clampToDisplay(topLeft.first, topLeft.second, width, height)
        // The suggestion strip fires window changes on every keystroke; ignore small jitter so a
        // settled chip never bounces.
        val jitter = (JITTER_DP * density).toInt()
        if (existing != null && abs(x - chipX) < jitter && abs(y - chipY) < jitter) return
        val params = chipParams(x, y, width, height)
        chipX = x
        chipY = y
        if (existing != null) {
            window.updateViewLayout(existing, params)
            return
        }
        val view = ComposeView(this)
        owner.attach(view)
        view.setContent {
            SpeecherTheme {
                DictationChip(
                    onTap = ::onChipTap,
                    onDrag = ::onChipDrag,
                    onDragEnd = ::onChipDragEnd,
                )
            }
        }
        window.addView(view, params)
        chip = view
    }

    private fun chipParams(x: Int, y: Int, width: Int, height: Int) =
        WindowManager.LayoutParams(
                width,
                height,
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

    /** Keep the chip on the visible display, clear of the cutout, so a drag can't lose it. */
    private fun clampToDisplay(x: Int, y: Int, width: Int, height: Int): Pair<Int, Int> {
        val metrics = window.currentWindowMetrics
        val cutout = metrics.windowInsets.getInsets(WindowInsets.Type.displayCutout())
        val bounds = metrics.bounds
        val left = bounds.left + cutout.left
        val top = bounds.top + cutout.top
        return x.coerceIn(left, (bounds.right - cutout.right - width).coerceAtLeast(left)) to
            y.coerceIn(top, (bounds.bottom - cutout.bottom - height).coerceAtLeast(top))
    }

    /** Moves the chip window by ([dx], [dy]) pixels, the finger's travel from its grab point. */
    private fun onChipDrag(dx: Float, dy: Float) {
        dragging = true
        val view = chip ?: return
        val params = view.layoutParams as? WindowManager.LayoutParams ?: return
        val (x, y) =
            clampToDisplay(
                params.x + dx.toInt(),
                params.y + dy.toInt(),
                params.width,
                params.height,
            )
        params.x = x
        params.y = y
        chipX = x
        chipY = y
        window.updateViewLayout(view, params)
    }

    private fun onChipDragEnd() {
        dragging = false
        val keyboard =
            windows.firstOrNull { it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD } ?: return
        val kb = Rect().also(keyboard::getBoundsInScreen)
        val offsetX = chipX - kb.right
        val offsetY = chipY - kb.bottom
        chipOffsetX = offsetX
        chipOffsetY = offsetY
        val store = SettingsStore(this)
        store.save(store.load().copy(chipOffsetX = offsetX, chipOffsetY = offsetY))
    }

    /** The screen bounds of the keyboard's own voice-input button, if it exposes one. */
    private fun AccessibilityWindowInfo.micRect(): Rect? {
        val voice = root?.let(::findVoiceNode) ?: return null
        return Rect().also(voice::getBoundsInScreen).takeIf { !it.isEmpty }
    }

    private fun findVoiceNode(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        // Match the label, the visible text and the view id: content descriptions are localized, so
        // no fixed word set finds every keyboard's mic key. Missing it is normal, not exceptional —
        // the caller just falls back to a corner the user can drag from.
        val labels =
            listOfNotNull(
                node.contentDescription?.toString(),
                node.text?.toString(),
                node.viewIdResourceName,
            )
        if (labels.any { label -> VOICE_TOKENS.any { it in label.lowercase() } }) return node
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
        val signedIn = TokenStore(this).signedIn()
        engine.start(resolveSignedIn(settings.transcriptionProvider, signedIn))
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
        const val CHIP_WIDTH_DP = 52f
        const val CHIP_HEIGHT_DP = 36f
        const val MARGIN_DP = 6f
        const val JITTER_DP = 8f
        val VOICE_TOKENS = listOf("voice", "microphone", "mic", "dictat", "speak", "speech")
    }
}
