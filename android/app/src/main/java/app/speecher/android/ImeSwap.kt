package app.speecher.android

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.provider.Settings
import android.view.inputmethod.InputMethodManager
import androidx.core.content.edit
import app.speecher.android.dictation.ActiveDictation

class ImeSwap(private val context: Context) {
    private val preferences = context.getSharedPreferences("previous-ime", Context.MODE_PRIVATE)
    private val inputMethods = context.getSystemService(InputMethodManager::class.java)
    private val ownId = speecherImeId(context)

    fun activate() {
        check(
            context.checkSelfPermission(Manifest.permission.WRITE_SECURE_SETTINGS) ==
                PackageManager.PERMISSION_GRANTED
        ) {
            "Silent keyboard switching needs the adb grant"
        }
        check(inputMethods.enabledInputMethodList.any { it.id == ownId }) {
            "Enable the Speecher keyboard in system settings"
        }
        val previous =
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)
        check(!previous.isNullOrBlank() && previous != ownId) { "No previous keyboard to restore" }
        val subtype = Settings.Secure.getInt(context.contentResolver, SELECTED_SUBTYPE, -1)
        preferences.edit(commit = true) {
            putString("id", previous)
            putInt("subtype", subtype)
        }
        check(
            Settings.Secure.putString(
                context.contentResolver,
                Settings.Secure.DEFAULT_INPUT_METHOD,
                ownId,
            )
        ) {
            "Could not switch to Speecher"
        }
    }

    fun switchBack(service: SpeecherImeService) {
        val previous = preferences.getString("id", null) ?: return
        val savedSubtype = preferences.getInt("subtype", -1)
        val info = inputMethods.enabledInputMethodList.firstOrNull { it.id == previous }
        val subtype =
            info
                ?.let { inputMethods.getEnabledInputMethodSubtypeList(it, true) }
                ?.firstOrNull { it.hashCode() == savedSubtype }
        if (info != null) service.switchInputMethod(previous, subtype)
        else service.switchToPreviousInputMethod()
        preferences.edit(commit = true) { clear() }
    }

    /**
     * Crash recovery. Our keyboard is the default but this process has no live dictation, so the
     * previous process died mid-swap and would otherwise strand the user in our keyboard. A swap in
     * progress always has a live engine, so this never undoes one.
     */
    fun restoreOnRestart() {
        if (ActiveDictation.engine != null) return
        val previous = preferences.getString("id", null) ?: return
        val selected =
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)
        if (selected != ownId) return
        if (
            context.checkSelfPermission(Manifest.permission.WRITE_SECURE_SETTINGS) !=
                PackageManager.PERMISSION_GRANTED
        )
            return
        val restored =
            Settings.Secure.putString(
                context.contentResolver,
                Settings.Secure.DEFAULT_INPUT_METHOD,
                previous,
            )
        val subtypeRestored =
            Settings.Secure.putInt(
                context.contentResolver,
                SELECTED_SUBTYPE,
                preferences.getInt("subtype", -1),
            )
        if (restored && subtypeRestored) {
            preferences.edit(commit = true) { clear() }
        }
    }

    private companion object {
        const val SELECTED_SUBTYPE = "selected_input_method_subtype"
    }
}
