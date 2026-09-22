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
                    true,
                    {},
                    {
                        ActiveDictation.engine?.cancel()
                        switchBack()
                    },
                    {
                        ActiveDictation.engine?.insert()
                        switchBack()
                    },
                    {
                        ActiveDictation.engine?.insertRefined(
                            app.speecher.android.dictation.Provider.Claude
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
        /* Swap owns the saved IME and subtype in milestone 6. */
    }

    override fun onDestroy() {
        owner.destroy()
        super.onDestroy()
    }
}
