package app.speecher.android

import android.content.Context
import android.provider.Settings
import android.view.inputmethod.InputMethodManager
import androidx.core.content.edit
import app.speecher.android.dictation.ActiveDictation

/**
 * A temporary keyboard swap that needs no privileged permission. The accessibility service switches
 * to our keyboard with `SoftKeyboardController.switchToInputMethod`, and our IME switches back with
 * `InputMethodService.switchInputMethod`. This class only remembers which keyboard to return to and
 * performs the IME-side switch back.
 */
class ImeSwap(private val context: Context) {
    private val preferences = context.getSharedPreferences("previous-ime", Context.MODE_PRIVATE)
    private val inputMethods = context.getSystemService(InputMethodManager::class.java)
    private val ownId = speecherImeId(context)

    val previousId: String?
        get() = preferences.getString("id", null)

    /** Record the keyboard to return to. Call before switching to ours. */
    fun rememberPrevious() {
        val previous =
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)
        if (previous.isNullOrBlank() || previous == ownId) return
        preferences.edit(commit = true) {
            putString("id", previous)
            putInt("subtype", Settings.Secure.getInt(context.contentResolver, SELECTED_SUBTYPE, -1))
        }
    }

    /** Our keyboard is the default but nothing is dictating: a swap a dead process never undid. */
    fun stranded(): Boolean =
        ActiveDictation.engine == null &&
            previousId != null &&
            Settings.Secure.getString(
                context.contentResolver,
                Settings.Secure.DEFAULT_INPUT_METHOD,
            ) == ownId

    /** Switch back to the remembered keyboard from inside our IME. */
    fun switchBack(service: SpeecherImeService) {
        val previous = previousId ?: return
        val savedSubtype = preferences.getInt("subtype", -1)
        val info = inputMethods.enabledInputMethodList.firstOrNull { it.id == previous }
        val subtype =
            info
                ?.let { inputMethods.getEnabledInputMethodSubtypeList(it, true) }
                ?.firstOrNull { it.hashCode() == savedSubtype }
        if (info != null) service.switchInputMethod(previous, subtype)
        else service.switchToPreviousInputMethod()
        clear()
    }

    fun clear() = preferences.edit(commit = true) { clear() }

    private companion object {
        const val SELECTED_SUBTYPE = "selected_input_method_subtype"
    }
}
