package app.speecher.android

import android.Manifest
import android.accessibilityservice.AccessibilityServiceInfo
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.accessibility.AccessibilityManager
import android.view.inputmethod.InputMethodManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
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
import app.speecher.android.ui.SettingsPage
import app.speecher.android.ui.SettingsPageContent
import app.speecher.android.ui.SignInStepsSheet
import app.speecher.android.ui.SpeecherScreen
import app.speecher.android.ui.SpeecherTheme
import app.speecher.android.update.ApkUpdate
import app.speecher.android.update.RETRY_MILLIS
import app.speecher.android.update.installApk
import app.speecher.android.update.newerApk
import app.speecher.android.update.untilCheck
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
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
    private var updating by mutableStateOf(false)
    private var updateFailed by mutableStateOf(false)
    private var page by mutableStateOf(Page.Home)
    // Where leaving setup goes: Settings when its Setup assistant row opened it, otherwise Home.
    private var setupFrom = Page.Home
    // The page open from the Settings list, or null for the list itself.
    private var settingsPage by mutableStateOf<SettingsPage?>(null)
    // The provider whose "Before you sign in" steps are up. The browser only opens from there.
    private var signInSteps by mutableStateOf<Provider?>(null)
    // The Settings page a sign-in started from, to go back to once it succeeds or is left. Saved
    // with the activity, so recreating it while the browser is up keeps it.
    private var signInFrom: SettingsPage? = null

    private val microphone =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { refresh() }

    // The provider waiting on the notification prompt before its browser sign-in opens. Saved with
    // the activity, whose recreation under the prompt would otherwise drop the sign-in.
    private var signInAfterPrompt: Provider? = null

    // Only for the sign-in notification. Refused, it stays hidden and sign-in works the same.
    private val notifications =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) {
            signInAfterPrompt?.let { signIn.start(this, it) }
            signInAfterPrompt = null
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        signInAfterPrompt =
            savedInstanceState?.getString(SIGN_IN_AFTER_PROMPT)?.let(Provider::valueOf)
        signInFrom = savedInstanceState?.getString(SIGN_IN_FROM)?.let(SettingsPage::valueOf)
        settings = settingsStore.load()
        refresh()
        signIn.restore(status.working)
        page = if (status.complete && signIn.activeProvider == null) Page.Home else Page.Setup
        if (savedInstanceState == null) handleIntent(intent)
        setContent {
            SpeecherTheme {
                signInSteps?.let { provider ->
                    SignInStepsSheet(
                        provider,
                        onOpen = { openSignIn(provider) },
                        onDismiss = {
                            signInSteps = null
                            returnFromSignIn()
                        },
                    )
                }
                // A failed attempt leaves the list up, since its Accounts section holds the error.
                LaunchedEffect(signIn.activeProvider) {
                    if (signIn.activeProvider != null) return@LaunchedEffect
                    if (signIn.error == null) returnFromSignIn() else signInFrom = null
                }
                BackHandler(page != Page.Home, ::back)
                when (page) {
                    Page.Home ->
                        SpeecherScreen("Speecher", onBack = null) {
                            Home(
                                status,
                                settings,
                                { page = Page.Setup },
                                { page = Page.Settings },
                                ::signInFromSettings,
                                ::openAccessibilitySettings,
                                update = update,
                                updating = updating,
                                updateFailed = updateFailed,
                                onUpdate = ::installUpdate,
                            )
                        }
                    Page.Setup ->
                        SpeecherScreen("Set up Speecher", onBack = null) {
                            Onboarding(
                                status,
                                { signInSteps = it },
                                { microphone.launch(Manifest.permission.RECORD_AUDIO) },
                                { startActivity(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)) },
                                ::openAccessibilitySettings,
                                {
                                    startActivity(
                                        Intent(
                                            Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                                            Uri.fromParts("package", packageName, null),
                                        )
                                    )
                                },
                                onFinish = ::leaveSetup,
                                signingIn = signIn.activeProvider,
                                signInError = signIn.error,
                                onPasteCode = signIn::paste,
                            )
                        }
                    Page.Settings -> {
                        val open = settingsPage
                        if (open == null) {
                            SpeecherScreen("Settings", onBack = ::back) {
                                app.speecher.android.ui.Settings(
                                    settings,
                                    status.signedIn,
                                    { settingsPage = it },
                                    { signInSteps = it },
                                    ::signOut,
                                    ::changeSettings,
                                    sessionEnded = status.sessionEnded,
                                    signingIn = signIn.activeProvider,
                                    signInError = signIn.error,
                                    onPasteCode = signIn::paste,
                                    onRunSetup = {
                                        setupFrom = Page.Settings
                                        page = Page.Setup
                                    },
                                )
                            }
                        } else {
                            SpeecherScreen(open.title, onBack = ::back) {
                                SettingsPageContent(
                                    open,
                                    settings,
                                    status.signedIn,
                                    ::changeSettings,
                                    ::signInFromSettings,
                                    { page = Page.ChipPosition },
                                )
                            }
                        }
                    }
                    Page.ChipPosition ->
                        SpeecherScreen("Button position", onBack = ::back) {
                            ChipPosition(
                                settings.chipOffsetX,
                                settings.chipOffsetY,
                                onSave = { x, y ->
                                    changeSettings(settings.copy(chipOffsetX = x, chipOffsetY = y))
                                    page = Page.Settings
                                },
                                onDock = {
                                    changeSettings(settings.copy(chipDockOnMic = true))
                                    page = Page.Settings
                                },
                            )
                        }
                }
            }
        }
        // The keyboard and accessibility toggles change outside the app, so poll while visible.
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.RESUMED) {
                // The chip's save offer writes settings too, so never edit a stale copy.
                settings = settingsStore.load()
                while (true) {
                    refresh()
                    delay(1_000)
                }
            }
        }
        showSavedUpdate()
        // Only while the app is in the foreground: on opening or returning to it once the interval
        // has passed, then each time it passes again. A new interval applies at once.
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                snapshotFlow { settings.updateCheckMinutes }
                    .collectLatest { minutes ->
                        val interval = minutes * 60_000L
                        while (true) {
                            delay(untilUpdateCheck(interval))
                            // A failed check is not a check: try again soon, not an interval later.
                            if (!checkForUpdate()) delay(minOf(interval, RETRY_MILLIS))
                        }
                    }
            }
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putString(SIGN_IN_AFTER_PROMPT, signInAfterPrompt?.name)
        outState.putString(SIGN_IN_FROM, signInFrom?.name)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    /** Opens what a panel failure's recovery asks for: a provider's sign-in or a settings page. */
    private fun handleIntent(intent: Intent) {
        intent
            .getStringExtra("settings_page")
            ?.let { name -> SettingsPage.entries.firstOrNull { it.name == name } }
            ?.let {
                page = Page.Settings
                settingsPage = it
                return
            }
        val provider =
            intent.getStringExtra("sign_in_provider")?.let { name ->
                Provider.entries.firstOrNull { it.name == name }
            } ?: return
        signInFromSettings(provider)
    }

    /**
     * Signs in from the Settings list, whose Accounts section shows the paste fallback, and comes
     * back to the page it was asked from.
     */
    private fun signInFromSettings(provider: Provider) {
        signInFrom = settingsPage.takeIf { page == Page.Settings }
        page = Page.Settings
        settingsPage = null
        signInSteps = provider
    }

    // Only from the Settings list the sign-in left the user on; anywhere else they have moved on.
    private fun returnFromSignIn() {
        if (page == Page.Settings && settingsPage == null) settingsPage = signInFrom
        signInFrom = null
    }

    private fun back() {
        when {
            page == Page.ChipPosition -> page = Page.Settings
            page == Page.Settings && settingsPage != null -> settingsPage = null
            page == Page.Setup -> leaveSetup()
            else -> page = Page.Home
        }
    }

    private fun leaveSetup() {
        page = setupFrom
        setupFrom = Page.Home
    }

    private val updatePreferences by lazy { getSharedPreferences("updates", MODE_PRIVATE) }

    private val sameVersion: Boolean
        get() = updatePreferences.getString("installed-version", null) == BuildConfig.VERSION_NAME

    /** The update the last check found, if this version made that check. */
    private fun showSavedUpdate() {
        if (!sameVersion) return
        val version = updatePreferences.getString("version", null) ?: return
        val url = updatePreferences.getString("url", null) ?: return
        update = ApkUpdate(version, url)
    }

    /**
     * Milliseconds until the next check: none once the app itself has updated, else what is left of
     * [interval] since the last.
     */
    private fun untilUpdateCheck(interval: Long): Long =
        if (!sameVersion) 0
        else
            untilCheck(
                updatePreferences.getLong("last-check", 0),
                System.currentTimeMillis(),
                interval,
            )

    /** Whether the check reached the releases list; only one that did counts as the last check. */
    private suspend fun checkForUpdate(): Boolean {
        val now = System.currentTimeMillis()
        val result =
            withContext(Dispatchers.IO) {
                runCatching { newerApk(sharedHttp, BuildConfig.VERSION_NAME) }
            }
        // The offer an earlier version found is stale either way; the new version is recorded only
        // with a check that counts, so a failure after an upgrade retries instead of waiting out
        // what was left of the old version's interval.
        if (!sameVersion) {
            updatePreferences.edit {
                remove("version")
                remove("url")
            }
        }
        return result
            .onSuccess { release ->
                update = release
                updatePreferences.edit {
                    putLong("last-check", now)
                    putString("installed-version", BuildConfig.VERSION_NAME)
                    if (release == null) {
                        remove("version")
                        remove("url")
                    } else {
                        putString("version", release.version)
                        putString("url", release.downloadUrl)
                    }
                }
            }
            .isSuccess
    }

    private fun installUpdate() {
        val release = update ?: return
        updateFailed = false
        updating = true
        lifecycleScope.launch {
            runCatching {
                withContext(Dispatchers.IO) { installApk(this@MainActivity, sharedHttp, release) }
            }
                .onFailure { updateFailed = true }
            updating = false
        }
    }

    private fun openAccessibilitySettings() {
        startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
    }

    private fun refresh() {
        status =
            SetupStatus(
                signedIn = tokens.signedIn(),
                sessionEnded = tokens.sessionEnded(),
                microphoneGranted = granted(Manifest.permission.RECORD_AUDIO),
                keyboardEnabled = keyboardEnabled(),
                chipEnabled = chipEnabled(),
            )
    }

    private fun changeSettings(changed: SpeecherSettings) {
        settingsStore.save(changed)
        settings = changed
    }

    /**
     * The sheet explained the notification, so ask for it now, and open the browser once the prompt
     * is answered either way; opening it at once would bury the prompt under the browser.
     */
    private fun openSignIn(provider: Provider) {
        signInSteps = null
        if (
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                !granted(Manifest.permission.POST_NOTIFICATIONS)
        ) {
            signInAfterPrompt = provider
            notifications.launch(Manifest.permission.POST_NOTIFICATIONS)
        } else signIn.start(this, provider)
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

private const val SIGN_IN_AFTER_PROMPT = "sign-in-after-prompt"
private const val SIGN_IN_FROM = "sign-in-from"

private fun emptyStatus() = SetupStatus(emptySet(), false, false, false)
