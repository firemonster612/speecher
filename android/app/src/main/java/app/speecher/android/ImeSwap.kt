package app.speecher.android

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.provider.Settings
import android.view.inputmethod.InputMethodManager
import androidx.core.content.edit
import app.speecher.android.dictation.ActiveDictation

class ImeSwap(private val context: Context) {
    private val preferences = context.getSharedPreferences("previous-ime", Context.MODE_PRIVATE)
    private val inputMethods = context.getSystemService(InputMethodManager::class.java)
    private val ownId = ComponentName(context, SpeecherImeService::class.java).flattenToString()

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
        ActiveDictation.swapInProgress = true
        try {
            check(
                Settings.Secure.putString(
                    context.contentResolver,
                    Settings.Secure.DEFAULT_INPUT_METHOD,
                    ownId,
                )
            ) {
                "Could not switch to Speecher"
            }
        } catch (error: Exception) {
            ActiveDictation.swapInProgress = false
            throw error
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
        ActiveDictation.swapInProgress = false
        preferences.edit(commit = true) { clear() }
    }

    fun restoreOnRestart() {
        if (ActiveDictation.swapInProgress) return
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
