package app.speecher.android

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.inputmethodservice.InputMethodService
import android.view.View
import android.view.inputmethod.EditorInfo
import androidx.compose.ui.platform.ComposeView
import app.speecher.android.dictation.ActiveDictation
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.FailureReason
import app.speecher.android.ui.DictationPanel
import app.speecher.android.ui.SpeecherTheme

/** The id Android uses for this keyboard: the short `package/.Class` form of its component. */
fun speecherImeId(context: Context): String =
    ComponentName(context, SpeecherImeService::class.java).flattenToShortString()

class SpeecherImeService : InputMethodService() {
    private val owner = ServiceViewOwner()
    private var panelState =
        androidx.compose.runtime.mutableStateOf<DictationState>(DictationState.Connecting)

    override fun onCreate() {
        super.onCreate()
        ImeSwap(this).restoreOnRestart()
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
            // Compose looks up its owners from the window root, which the IME framework owns.
            window.window?.decorView?.let(owner::attach)
            owner.attach(view)
            view.setContent {
                SpeecherTheme {
                    DictationPanel(
                        panelState.value,
                        ActiveDictation.settings.refinementEnabled,
                        onCancel = {
                            ActiveDictation.engine?.cancel()
                            switchBack()
                        },
                        onInsert = { ActiveDictation.engine?.insert() },
                        onInsertRefined = {
                            ActiveDictation.engine?.insertRefined(
                                ActiveDictation.settings.refinementProvider
                            )
                        },
                        onRecover = ::recover,
                    )
                }
            }
        }

    override fun onStartInput(attribute: EditorInfo?, restarting: Boolean) {
        super.onStartInput(attribute, restarting)
        ActiveDictation.connection = currentInputConnection
    }

    override fun onFinishInput() {
        ActiveDictation.connection = null
        super.onFinishInput()
    }

    override fun onFinishInputView(finishingInput: Boolean) {
        ActiveDictation.engine?.cancel()
        super.onFinishInputView(finishingInput)
        if (
            android.provider.Settings.Secure.getString(
                contentResolver,
                android.provider.Settings.Secure.DEFAULT_INPUT_METHOD,
            ) == speecherImeId(this)
        )
            switchBack()
    }

    fun showState(state: DictationState) {
        panelState.value = state
    }

    /** Network and provider failures retry in place; the others need the app. */
    private fun recover() {
        val failed = panelState.value as? DictationState.Failed ?: return
        if (failed.reason == FailureReason.Network || failed.reason == FailureReason.Provider) {
            ActiveDictation.engine?.retry()
            return
        }
        ActiveDictation.engine?.cancel()
        switchBack()
        startActivity(
            Intent(this, MainActivity::class.java)
                .putExtra("sign_in_provider", failed.provider?.name)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        )
    }

    private fun switchBack() {
        ActiveDictation.engine?.close()
        ActiveDictation.engine = null
        ImeSwap(this).switchBack(this)
    }

    override fun onDestroy() {
        ActiveDictation.observe = null
        ActiveDictation.onInserted = null
        owner.destroy()
        super.onDestroy()
    }
}
