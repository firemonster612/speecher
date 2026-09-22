package app.speecher.android

import android.inputmethodservice.InputMethodService
import android.view.View
import android.view.inputmethod.EditorInfo
import androidx.compose.ui.platform.ComposeView
import app.speecher.android.dictation.ActiveDictation
import app.speecher.android.dictation.DictationState
import app.speecher.android.ui.EngineSurface

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
            owner.attach(view)
            view.setContent {
                EngineSurface(
                    panelState.value,
                    ActiveDictation.settings.refinementEnabled,
                    {},
                    {
                        ActiveDictation.engine?.cancel()
                        switchBack()
                    },
                    { ActiveDictation.engine?.insert() },
                    {
                        ActiveDictation.engine?.insertRefined(
                            ActiveDictation.settings.refinementProvider
                        )
                    },
                )
            }
        }

    override fun onStartInput(attribute: EditorInfo?, restarting: Boolean) {
        super.onStartInput(attribute, restarting)
        ActiveDictation.connection = currentInputConnection
        ActiveDictation.imeActive = true
    }

    override fun onFinishInput() {
        ActiveDictation.connection = null
        ActiveDictation.imeActive = false
        super.onFinishInput()
    }

    fun showState(state: DictationState) {
        panelState.value = state
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
