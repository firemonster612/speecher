package app.speecher.android

import android.accessibilityservice.AccessibilityService
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Display
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
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.createDictationEngine
import app.speecher.android.dictation.resolveSignedIn
import app.speecher.android.dictation.screenCapture
import app.speecher.android.dictation.screenshotJpeg
import app.speecher.android.dictation.sharedExecutor
import app.speecher.android.ui.ChipMargin
import app.speecher.android.ui.ChipSize
import app.speecher.android.ui.DictationChip
import app.speecher.android.ui.SavePositionPill
import app.speecher.android.ui.SpeecherTheme
import kotlin.math.abs
import kotlin.math.roundToInt

class SpeecherChipService : AccessibilityService() {
    private val handler = Handler(Looper.getMainLooper())
    private val owner = ServiceViewOwner()
    private val window by lazy { getSystemService(WINDOW_SERVICE) as WindowManager }
    private var chip: ComposeView? = null
    // Offers to keep a drag's position; ignored, it goes away and the drag stays temporary.
    private var pill: ComposeView? = null
    private val dismissPill = Runnable { removePill() }
    private var chipX = Int.MIN_VALUE
    private var chipY = Int.MIN_VALUE
    // The chip window's position when the current drag began.
    private var dragStartX = 0
    private var dragStartY = 0
    // A drag moves the chip only for this showing: until the keyboard hides, updateChip leaves it
    // where the finger put it, then the next showing goes back to the configured place.
    private var nudged = false
    // The configured placement, read when a showing starts so a position set in the app applies
    // the next time the keyboard shows.
    private var placement = SpeecherSettings()
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
        val keyboard = keyboardWindow()
        if (keyboard == null) {
            removeChip()
            return
        }
        if (nudged) return
        val density = resources.displayMetrics.density
        val width = (ChipSize.width.value * density).toInt()
        val height = (ChipSize.height.value * density).toInt()
        val margin = (ChipMargin.value * density).toInt()
        val kb = Rect().also(keyboard::getBoundsInScreen)
        val existing = chip
        if (existing == null) placement = SettingsStore(this).load()
        val offsetX = placement.chipOffsetX
        val offsetY = placement.chipOffsetY
        val topLeft =
            if (!placement.chipDockOnMic && offsetX != null && offsetY != null) {
                // The user placed it; keep it anchored to the keyboard's bottom-right corner so it
                // survives the keyboard changing height or hiding and re-showing.
                (kb.right + offsetX) to (kb.bottom + offsetY)
            } else {
                val mic = keyboard.micRect()
                // Sit on the keyboard's own voice button when we can read it. If detection blips to
                // null while the chip is already up, hold the last spot. Docked but undetected, go
                // to the top-right of the strip, where Gboard keeps its voice key; otherwise use
                // the bottom-right corner. Either way the user can drag it from there.
                val center =
                    when {
                        mic != null -> mic.centerX() to mic.centerY()
                        existing != null -> return
                        placement.chipDockOnMic ->
                            (kb.right - width / 2 - margin) to (kb.top + height / 2 + margin)
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
                    onDragStart = ::onChipDragStart,
                    onDrag = ::onChipDrag,
                    onDragEnd = ::showPill,
                )
            }
        }
        window.addView(view, params)
        chip = view
    }

    private fun overlayParams(width: Int, height: Int) =
        WindowManager.LayoutParams(
            width,
            height,
            WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            android.graphics.PixelFormat.TRANSLUCENT,
        )

    private fun chipParams(x: Int, y: Int, width: Int, height: Int) =
        overlayParams(width, height).apply {
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

    private fun onChipDragStart() {
        removePill()
        nudged = true
        dragStartX = chipX
        dragStartY = chipY
    }

    /** Puts the chip window ([dx], [dy]) pixels from where the drag began, under the finger. */
    private fun onChipDrag(dx: Float, dy: Float) {
        val view = chip ?: return
        val params = view.layoutParams as? WindowManager.LayoutParams ?: return
        val (x, y) =
            clampToDisplay(
                dragStartX + dx.roundToInt(),
                dragStartY + dy.roundToInt(),
                params.width,
                params.height,
            )
        params.x = x
        params.y = y
        chipX = x
        chipY = y
        window.updateViewLayout(view, params)
    }

    /**
     * Shows the save offer beside the chip, on whichever side has more room, centred on it
     * vertically, and takes it down again after a few seconds.
     */
    private fun showPill() {
        removePill()
        val chipParams = chip?.layoutParams as? WindowManager.LayoutParams ?: return
        val bounds = window.currentWindowMetrics.bounds
        val gap = (ChipMargin.value * resources.displayMetrics.density).toInt()
        val onLeft = chipX + chipParams.width / 2 > bounds.centerX()
        val params =
            overlayParams(
                    WindowManager.LayoutParams.WRAP_CONTENT,
                    WindowManager.LayoutParams.WRAP_CONTENT,
                )
                .apply {
                    gravity = Gravity.CENTER_VERTICAL or if (onLeft) Gravity.END else Gravity.START
                    x = if (onLeft) bounds.right - chipX + gap else chipX + chipParams.width + gap
                    y = chipY + chipParams.height / 2 - bounds.centerY()
                    fitInsetsTypes = 0
                    layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                }
        val view = ComposeView(this)
        owner.attach(view)
        view.setContent { SpeecherTheme { SavePositionPill(onSave = ::saveChipPosition) } }
        window.addView(view, params)
        pill = view
        handler.postDelayed(dismissPill, PILL_MILLIS)
    }

    /**
     * Keeps the dragged spot: the custom position, measured from the keyboard's bottom-right corner
     * like the in-app editor's, with docking turned off so it applies from now on.
     */
    private fun saveChipPosition() {
        removePill()
        val keyboard = keyboardWindow() ?: return
        val kb = Rect().also(keyboard::getBoundsInScreen)
        val store = SettingsStore(this)
        placement =
            store
                .load()
                .copy(
                    chipDockOnMic = false,
                    chipOffsetX = chipX - kb.right,
                    chipOffsetY = chipY - kb.bottom,
                )
        store.save(placement)
    }

    private fun removePill() {
        handler.removeCallbacks(dismissPill)
        pill?.let(window::removeView)
        pill = null
    }

    private fun keyboardWindow() = windows.firstOrNull {
        it.type == AccessibilityWindowInfo.TYPE_INPUT_METHOD
    }

    /** The screen bounds of the keyboard's own voice-input button, if it exposes one. */
    private fun AccessibilityWindowInfo.micRect(): Rect? {
        val voice = root?.let(::findVoiceNode) ?: return null
        return Rect().also(voice::getBoundsInScreen).takeIf { !it.isEmpty }
    }

    private fun findVoiceNode(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        // Match the label, the visible text and the view id: content descriptions are localized, so
        // no fixed word set finds every keyboard's mic key. Missing it is normal, not exceptional —
        // the caller just falls back to a corner the user can drag from. Words must start with a
        // token so a suggestion like "economic" is not taken for the mic; ids such as
        // key_pos_header_voice may carry the token anywhere.
        val words = listOfNotNull(node.contentDescription, node.text)
        val id = node.viewIdResourceName?.lowercase().orEmpty()
        if (words.any(VOICE_WORD::containsMatchIn) || VOICE_TOKENS.any { it in id }) return node
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
        // Only now, before the swap, is the active window still the app being dictated into.
        captureScreen(engine)
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
        ActiveDictation.clearScreen()
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

    /**
     * Reads what the user opted into sharing from the target window. The text is read here; the
     * screenshot arrives later and is dropped if a newer dictation started. A failed or
     * rate-limited screenshot is simply absent.
     */
    private fun captureScreen(engine: DictationEngine) {
        val settings = ActiveDictation.settings
        if (!settings.refinementEnabled || !settings.useTargetContext || passwordFocused) return
        if (settings.includeScreenText) {
            ActiveDictation.screen = rootInActiveWindow?.let(::screenCapture)
        }
        if (!settings.includeScreenshot) return
        takeScreenshot(
            Display.DEFAULT_DISPLAY,
            sharedExecutor,
            object : TakeScreenshotCallback {
                override fun onSuccess(screenshot: ScreenshotResult) {
                    // Runs on the executor: a failed encode is a skipped screenshot, not a crash.
                    val jpeg =
                        screenshot.hardwareBuffer.use { buffer ->
                            runCatching {
                                Bitmap.wrapHardwareBuffer(buffer, screenshot.colorSpace)
                                    ?.let(::screenshotJpeg)
                            }
                                .getOrNull()
                        }
                    if (ActiveDictation.engine === engine) ActiveDictation.screenshotJpeg = jpeg
                }

                override fun onFailure(errorCode: Int) = Unit
            },
        )
    }

    private fun removeChip() {
        removePill()
        chip?.let(window::removeView)
        chip = null
        chipX = Int.MIN_VALUE
        chipY = Int.MIN_VALUE
        nudged = false
    }

    override fun onDestroy() {
        handler.removeCallbacks(refresh)
        removeChip()
        owner.destroy()
        super.onDestroy()
    }

    private companion object {
        const val JITTER_DP = 8f
        const val PILL_MILLIS = 4_000L
        // English plus the common European forms: voz (es/pt), vocal/vocale (fr/it), Sprach- and
        // Mikro- (de), dictado/dictée/Diktat. "mic" covers microphone, micrófono and microfone.
        val VOICE_TOKENS =
            listOf(
                "voice",
                "voz",
                "vocal",
                "sprach",
                "mic",
                "mikro",
                "speak",
                "speech",
                "dicta",
                "dicté",
                "dikt",
            )
        val VOICE_WORD =
            Regex("(?<!\\p{L})(${VOICE_TOKENS.joinToString("|")})", RegexOption.IGNORE_CASE)
    }
}
