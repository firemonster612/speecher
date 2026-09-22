package app.speecher.android

import android.Manifest
import android.accessibilityservice.AccessibilityServiceInfo
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.provider.Settings
import android.view.accessibility.AccessibilityManager
import android.view.inputmethod.InputMethodManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import app.speecher.android.auth.SignIn
import app.speecher.android.auth.TokenStore
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SettingsStore
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.ui.Home
import app.speecher.android.ui.Onboarding
import app.speecher.android.ui.SpeecherScreen
import app.speecher.android.ui.SpeecherTheme
import app.speecher.protocol.OAuthProvider
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private enum class Page {
    Home,
    Setup,
    Settings,
}

class MainActivity : ComponentActivity() {
    private val tokens by lazy { TokenStore(this) }
    private val settingsStore by lazy { SettingsStore(this) }
    private val signIn by lazy { SignIn(this, tokens) }

    private var status by mutableStateOf(emptyStatus())
    private var settings by mutableStateOf(SpeecherSettings())

    private val microphone =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { refresh() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = settingsStore.load()
        refresh()
        var page by mutableStateOf(if (status.complete) Page.Home else Page.Setup)
        setContent {
            SpeecherTheme {
                BackHandler(page != Page.Home) { page = Page.Home }
                when (page) {
                    Page.Home ->
                        SpeecherScreen("Speecher", onBack = null) {
                            Home(status, settings, { page = Page.Setup }, { page = Page.Settings })
                        }
                    Page.Setup ->
                        SpeecherScreen("Set up Speecher", onBack = null) {
                            Onboarding(
                                status,
                                ::startSignIn,
                                { microphone.launch(Manifest.permission.RECORD_AUDIO) },
                                { startActivity(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)) },
                                { startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)) },
                            )
                        }
                    Page.Settings ->
                        SpeecherScreen("Settings", onBack = { page = Page.Home }) {
                            app.speecher.android.ui.Settings(
                                settings,
                                status.signedIn,
                                ::changeSettings,
                                ::startSignIn,
                                ::signOut,
                            )
                        }
                }
            }
        }
        // The adb grant and the system toggles change outside the app, so poll while visible.
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.RESUMED) {
                while (true) {
                    refresh()
                    delay(1_000)
                }
            }
        }
    }

    override fun onDestroy() {
        signIn.close()
        super.onDestroy()
    }

    private fun refresh() {
        status =
            SetupStatus(
                signedIn = tokens.signedIn(),
                microphoneGranted = granted(Manifest.permission.RECORD_AUDIO),
                keyboardEnabled = keyboardEnabled(),
                chipEnabled = chipEnabled(),
                swapGranted = granted(Manifest.permission.WRITE_SECURE_SETTINGS),
            )
    }

    private fun changeSettings(changed: SpeecherSettings) {
        settingsStore.save(changed)
        settings = changed
    }

    private fun startSignIn(provider: Provider) {
        signIn.start(provider.oauth) { result ->
            result.onFailure {
                Toast.makeText(this, "Sign-in failed: ${it.message}", Toast.LENGTH_LONG).show()
            }
            refresh()
        }
    }

    private fun signOut(provider: Provider) {
        tokens.signOut(provider.oauth)
        refresh()
    }

    private fun granted(permission: String) =
        checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED

    private fun keyboardEnabled(): Boolean {
        val ownId = ComponentName(this, SpeecherImeService::class.java).flattenToString()
        return getSystemService(InputMethodManager::class.java).enabledInputMethodList.any {
            it.id == ownId
        }
    }

    private fun chipEnabled(): Boolean {
        val own = ComponentName(this, SpeecherChipService::class.java)
        return getSystemService(AccessibilityManager::class.java)
            .getEnabledAccessibilityServiceList(AccessibilityServiceInfo.FEEDBACK_ALL_MASK)
            .any {
                it.resolveInfo.serviceInfo.let { s -> ComponentName(s.packageName, s.name) == own }
            }
    }
}

private fun emptyStatus() = SetupStatus(emptySet(), false, false, false, false)

private val Provider.oauth: OAuthProvider
    get() = if (this == Provider.Claude) OAuthProvider.Claude else OAuthProvider.ChatGpt
