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
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.edit
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import app.speecher.android.auth.SignInViewModel
import app.speecher.android.auth.TokenStore
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SettingsStore
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.oauth
import app.speecher.android.dictation.sharedHttp
import app.speecher.android.ui.ChipPosition
import app.speecher.android.ui.Home
import app.speecher.android.ui.Onboarding
import app.speecher.android.ui.SpeecherScreen
import app.speecher.android.ui.SpeecherTheme
import app.speecher.android.update.ApkUpdate
import app.speecher.android.update.installApk
import app.speecher.android.update.newerApk
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private enum class Page {
    Home,
    Setup,
    Settings,
    ChipPosition,
}

class MainActivity : ComponentActivity() {
    private val tokens by lazy { TokenStore(this) }
    private val settingsStore by lazy { SettingsStore(this) }
    private val signIn: SignInViewModel by viewModels()

    private var status by mutableStateOf(emptyStatus())
    private var settings by mutableStateOf(SpeecherSettings())
    private var update by mutableStateOf<ApkUpdate?>(null)
    private var updateError by mutableStateOf<String?>(null)
    private var page by mutableStateOf(Page.Home)

    private val microphone =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { refresh() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = settingsStore.load()
        refresh()
        signIn.restore(this)
        page = if (status.complete && signIn.activeProvider == null) Page.Home else Page.Setup
        if (savedInstanceState == null) handleSignInIntent(intent)
        setContent {
            SpeecherTheme {
                BackHandler(page != Page.Home) {
                    page = if (page == Page.ChipPosition) Page.Settings else Page.Home
                }
                when (page) {
                    Page.Home ->
                        SpeecherScreen("Speecher", onBack = null) {
                            Home(
                                status,
                                settings,
                                { page = Page.Setup },
                                { page = Page.Settings },
                                update = update,
                                onUpdate = ::installUpdate,
                                updateError = updateError,
                            )
                        }
                    Page.Setup ->
                        SpeecherScreen("Set up Speecher", onBack = null) {
                            Onboarding(
                                status,
                                ::startSignIn,
                                { microphone.launch(Manifest.permission.RECORD_AUDIO) },
                                { startActivity(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)) },
                                { startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)) },
                                onFinish = { page = Page.Home },
                                signingIn = signIn.activeProvider,
                                signInError = signIn.error,
                                onPasteCode = signIn::paste,
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
                                { page = Page.ChipPosition },
                                signingIn = signIn.activeProvider,
                                signInError = signIn.error,
                                onPasteCode = signIn::paste,
                            )
                        }
                    Page.ChipPosition ->
                        SpeecherScreen(
                            "Button position",
                            onBack = { page = Page.Settings },
                        ) {
                            ChipPosition(settings.chipOffsetX, settings.chipOffsetY) { x, y ->
                                changeSettings(settings.copy(chipOffsetX = x, chipOffsetY = y))
                                page = Page.Settings
                            }
                        }
                }
            }
        }
        // The keyboard and accessibility toggles change outside the app, so poll while visible.
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.RESUMED) {
                while (true) {
                    refresh()
                    delay(1_000)
                }
            }
        }
        checkForUpdate()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleSignInIntent(intent)
    }

    private fun handleSignInIntent(intent: Intent) {
        val provider =
            intent.getStringExtra("sign_in_provider")?.let { name ->
                Provider.entries.firstOrNull { it.name == name }
            } ?: return
        page = Page.Settings
        startSignIn(provider)
    }

    private fun checkForUpdate() {
        val preferences = getSharedPreferences("updates", MODE_PRIVATE)
        val sameVersion =
            preferences.getString("installed-version", null) == BuildConfig.VERSION_NAME
        if (sameVersion) {
            val version = preferences.getString("version", null)
            val url = preferences.getString("url", null)
            if (version != null && url != null) update = ApkUpdate(version, url)
        }
        val now = System.currentTimeMillis()
        if (sameVersion && now - preferences.getLong("last-check", 0) < 86_400_000) return
        lifecycleScope.launch {
            val result =
                withContext(Dispatchers.IO) {
                    runCatching { newerApk(sharedHttp, BuildConfig.VERSION_NAME) }
                }
            preferences.edit {
                putLong("last-check", now)
                putString("installed-version", BuildConfig.VERSION_NAME)
                if (!sameVersion) {
                    remove("version")
                    remove("url")
                }
            }
            result.onSuccess { release ->
                update = release
                preferences.edit {
                    if (release == null) {
                        remove("version")
                        remove("url")
                    } else {
                        putString("version", release.version)
                        putString("url", release.downloadUrl)
                    }
                }
            }
        }
    }

    private fun installUpdate() {
        val release = update ?: return
        lifecycleScope.launch {
            runCatching {
                withContext(Dispatchers.IO) { installApk(this@MainActivity, sharedHttp, release) }
            }
                .onFailure { updateError = "Could not install update: ${it.message}" }
        }
    }

    private fun refresh() {
        status =
            SetupStatus(
                signedIn = tokens.signedIn(),
                microphoneGranted = granted(Manifest.permission.RECORD_AUDIO),
                keyboardEnabled = keyboardEnabled(),
                chipEnabled = chipEnabled(),
            )
    }

    private fun changeSettings(changed: SpeecherSettings) {
        settingsStore.save(changed)
        settings = changed
    }

    private fun startSignIn(provider: Provider) {
        signIn.start(this, provider)
    }

    private fun signOut(provider: Provider) {
        tokens.signOut(provider.oauth)
        refresh()
    }

    private fun granted(permission: String) =
        checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED

    private fun keyboardEnabled(): Boolean {
        val ownId = speecherImeId(this)
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

private fun emptyStatus() = SetupStatus(emptySet(), false, false, false)
