package app.speecher.android

import android.Manifest
import android.accessibilityservice.AccessibilityServiceInfo
import android.content.ComponentName
import android.content.Context
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
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.IntentCompat
import androidx.core.net.toUri
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import app.speecher.android.auth.SignInViewModel
import app.speecher.android.auth.TokenStore
import app.speecher.android.dictation.DictationRecord
import app.speecher.android.dictation.Recovery
import app.speecher.android.dictation.clearInsights
import app.speecher.android.dictation.insightsFile
import app.speecher.android.dictation.latestTranscript
import app.speecher.android.dictation.loadInsights
import app.speecher.android.provider.Provider
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.provider.oauth
import app.speecher.android.settings.SettingsStore
import app.speecher.android.settings.SpeecherSettings
import app.speecher.android.transfer.ComputerSettings
import app.speecher.android.transfer.ImportFailure
import app.speecher.android.transfer.withImported
import app.speecher.android.ui.ChipPosition
import app.speecher.android.ui.ComputerImport
import app.speecher.android.ui.ComputerImportViewModel
import app.speecher.android.ui.Home
import app.speecher.android.ui.ImportState
import app.speecher.android.ui.Insights
import app.speecher.android.ui.Onboarding
import app.speecher.android.ui.SettingsPage
import app.speecher.android.ui.SettingsPageContent
import app.speecher.android.ui.SetupStatus
import app.speecher.android.ui.SignInStepsSheet
import app.speecher.android.ui.SpeecherScreen
import app.speecher.android.ui.SpeecherTheme
import app.speecher.android.ui.WhatsNew
import app.speecher.android.update.INSTALL_STATUS
import app.speecher.android.update.INSTALL_VERSION
import app.speecher.android.update.RELEASES_PAGE
import app.speecher.android.update.UpdateViewModel
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private enum class Page {
    Home,
    Setup,
    Settings,
    ChipPosition,
    Insights,
    WhatsNew,
    ComputerImport,
}

class MainActivity : ComponentActivity() {
    private val tokens by lazy { TokenStore(this) }
    private val settingsStore by lazy { SettingsStore(this) }
    private val signIn: SignInViewModel by viewModels()

    private var status by mutableStateOf(emptyStatus())
    private var settings by mutableStateOf(SpeecherSettings())
    private var insights by mutableStateOf(emptyList<DictationRecord>())
    // Kept here, not on the page, so a clear that fails after the page closed still says so.
    private var clearInsightsFailed by mutableStateOf(false)
    private val updates: UpdateViewModel by viewModels()
    private var whatsNewFrom = Page.Home
    private var shownPage by mutableStateOf(Page.Home)
    // Leaving the import page by any route ends the import, so showing the page whenever an import
    // is under way never brings back an old one.
    private var page: Page
        get() = shownPage
        set(value) {
            if (shownPage == Page.ComputerImport && value != Page.ComputerImport) {
                computerImport.dismiss()
            }
            shownPage = value
        }

    // Where leaving setup goes: Settings when its Setup assistant row opened it, otherwise Home.
    private var setupFrom = Page.Home
    // The page open from the Settings list, or null for the list itself.
    private var settingsPage by mutableStateOf<SettingsPage?>(null)
    // The provider whose "Before you sign in" steps are up. The browser only opens from there.
    private var signInSteps by mutableStateOf<Provider?>(null)
    // The Settings page a sign-in started from, to go back to once it succeeds or is left. Saved
    // with the activity, so recreating it while the browser is up keeps it.
    private var signInFrom: SettingsPage? = null
    private val computerImport: ComputerImportViewModel by viewModels()
    private val snackbar = SnackbarHostState()

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
        // A dictation into another app, beside this one, stores use counts meanwhile.
        settingsStore.observe(lifecycle) { settings = settingsStore.load() }
        refresh()
        signIn.restore(status.working)
        page = if (status.complete && signIn.activeProvider == null) Page.Home else Page.Setup
        updates.showSavedOffer(settings.updateChannel)
        savedInstanceState?.getString(SETTINGS_PAGE)?.let {
            page = Page.Settings
            settingsPage = SettingsPage.valueOf(it)
        }
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
                // The import outlives the activity, so a recreated one, or one a scan finished
                // behind, shows it too.
                LaunchedEffect(computerImport.state != null) {
                    if (computerImport.state != null) page = Page.ComputerImport
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
                                { page = Page.Insights },
                                update = updates.homeCard,
                                onUpdate = ::proceedWithUpdate,
                                onDismissUpdate = updates::dismissOffer,
                                whatsNewVersion =
                                    BuildConfig.VERSION_NAME.takeIf {
                                        updates.whatsNewPending != null
                                    },
                                onOpenWhatsNew = ::openWhatsNew,
                                onDismissWhatsNew = updates::dismissWhatsNew,
                                latest = latestTranscript,
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
                                this@MainActivity::openAppSettings,
                                onFinish = ::leaveSetup,
                                onUseServer = {
                                    changeSettings(
                                        settings.copy(
                                            transcriptionProvider = SpeechProvider.Endpoint
                                        )
                                    )
                                    page = Page.Settings
                                    settingsPage = SettingsPage.Transcription
                                },
                                signingIn = signIn.activeProvider,
                                signInError = signIn.error,
                                onPasteCode = signIn::paste,
                            )
                        }
                    Page.Settings -> {
                        val open = settingsPage
                        if (open == null) {
                            SpeecherScreen(
                                "Settings",
                                onBack = ::back,
                                snackbarHost = { SnackbarHost(snackbar) },
                            ) {
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
                                    updateState = updates.state,
                                    onCheckForUpdates = ::proceedWithUpdate,
                                    onOpenWhatsNew = ::openWhatsNew,
                                    onRunSetup = {
                                        setupFrom = Page.Settings
                                        page = Page.Setup
                                    },
                                    onImportFromComputer = ::scanComputerCode,
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
                                    status.chipEnabled,
                                    ::openAccessibilitySettings,
                                )
                            }
                        }
                    }
                    Page.Insights -> {
                        // A dictation into Home's practice field leaves the app resumed, so the
                        // history is read again whenever the page opens.
                        LaunchedEffect(Unit) { reloadInsights() }
                        SpeecherScreen("Insights", onBack = ::back) {
                            Insights(
                                insights,
                                settings,
                                ::changeSettings,
                                ::deleteInsights,
                                clearInsightsFailed,
                                status.chipEnabled,
                                {
                                    page = Page.Settings
                                    settingsPage = SettingsPage.LearnedCorrections
                                },
                            )
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
                    Page.ComputerImport ->
                        computerImport.state?.let {
                            ComputerImport(
                                it,
                                settings,
                                ::back,
                                ::scanComputerCode,
                                ::import,
                                ::merge,
                            )
                        }
                    Page.WhatsNew ->
                        SpeecherScreen("What's New", onBack = ::back) {
                            WhatsNew(
                                updates.whatsNewNotes,
                                updates::loadWhatsNew,
                                // A Nightly Build has no notes of its own, as on the desktop.
                                onViewReleases =
                                    { openPage(RELEASES_PAGE) }.takeIf {
                                        "-nightly." in BuildConfig.VERSION_NAME
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
                // Dictations inserted while the app was away are in the file by now.
                reloadInsights()
                while (true) {
                    refresh()
                    delay(1_000)
                }
            }
        }
        // Automatic update checks run only while the app is in the foreground.
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) { updates.checkPeriodically { settings } }
        }
    }

    override fun onStart() {
        super.onStart()
        updates.onScreen = true
    }

    override fun onStop() {
        updates.onScreen = false
        super.onStop()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putString(SIGN_IN_AFTER_PROMPT, signInAfterPrompt?.name)
        outState.putString(SIGN_IN_FROM, signInFrom?.name)
        // So a file picked from a page, such as a vocabulary import, reaches it.
        outState.putString(SETTINGS_PAGE, settingsPage?.name?.takeIf { page == Page.Settings })
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    /**
     * Opens what a panel failure's recovery asks for: a provider's sign-in or a settings page. Or
     * shows why Android refused an update.
     */
    private fun handleIntent(intent: Intent) {
        if (intent.hasExtra(INSTALL_STATUS)) {
            updates.installRefused(
                intent.getStringExtra(INSTALL_VERSION),
                intent.getIntExtra(INSTALL_STATUS, 0),
            )
            return
        }
        when (
            val recovery = IntentCompat.getSerializableExtra(intent, RECOVERY, Recovery::class.java)
        ) {
            is Recovery.SignIn -> signInFromSettings(recovery.account)
            Recovery.ChooseSpokenLanguage,
            Recovery.CheckSpeechServer -> openSettings(SettingsPage.Transcription)
            Recovery.ChooseCleanup -> openSettings(SettingsPage.Profiles)
            // Speecher opens as it was; a fresh start lands on Setup, whose microphone step asks.
            Recovery.AllowMicrophone,
            Recovery.Retry,
            null -> {}
        }
    }

    private fun openSettings(settings: SettingsPage) {
        page = Page.Settings
        settingsPage = settings
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
            page == Page.WhatsNew -> page = whatsNewFrom
            page == Page.ComputerImport -> page = Page.Settings
            page == Page.Settings && settingsPage != null -> settingsPage = null
            page == Page.Setup -> leaveSetup()
            else -> page = Page.Home
        }
    }

    private fun leaveSetup() {
        page = setupFrom
        setupFrom = Page.Home
    }

    // From Android 17 a socket to the computer's private address fails until the app holds this.
    private val localNetwork =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) startScanner() else computerImport.fail(ImportFailure.NoLocalNetwork)
        }

    private fun scanComputerCode() {
        val refused = computerImport.state == ImportState.Failed(ImportFailure.NoLocalNetwork)
        if (mayUseLocalNetwork(localNetwork, refused)) startScanner()
    }

    /** Starts Google's code scanner, which needs no camera permission, for the computer's code. */
    private fun startScanner() {
        val options =
            GmsBarcodeScannerOptions.Builder().setBarcodeFormats(Barcode.FORMAT_QR_CODE).build()
        // The activity may be recreated before the scan ends, so its result goes to the import,
        // which outlives it, never to this activity.
        val retained = computerImport
        GmsBarcodeScanning.getClient(this, options)
            .startScan()
            .addOnSuccessListener { retained.importFrom(it.rawValue.orEmpty()) }
            .addOnFailureListener { retained.fail(ImportFailure.ScannerUnavailable) }
    }

    private fun import(computer: ComputerSettings) {
        merge(computer)
        lifecycleScope.launch { snackbar.showSnackbar("Imported from ${computer.computer}") }
    }

    /**
     * Merges [computer]'s settings into those stored now, not the copy the preview showed, since a
     * dictation may have stored use counts meanwhile.
     */
    private fun merge(computer: ComputerSettings) {
        changeSettings(settingsStore.load().withImported(computer))
        page = Page.Settings
    }

    private fun proceedWithUpdate() {
        updates.proceed(settings.updateChannel, ::openPage)
    }

    private fun openWhatsNew() {
        whatsNewFrom = page
        page = Page.WhatsNew
        updates.openWhatsNew()
    }

    private suspend fun reloadInsights() {
        insights = loadInsights(insightsFile(this))
    }

    private fun deleteInsights() {
        clearInsightsFailed = false
        clearInsights(insightsFile(this)) { cleared ->
            runOnUiThread {
                if (cleared) insights = emptyList()
                clearInsightsFailed = !cleared
            }
        }
    }

    private fun openPage(url: String) {
        startActivity(Intent(Intent.ACTION_VIEW, url.toUri()))
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
                ownServer = settings.transcribesWithServer,
            )
    }

    private fun changeSettings(changed: SpeecherSettings) {
        if (changed.updateChannel != settings.updateChannel) updates.changeChannel()
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

/** Opens Speecher's page in Android's settings, where its permissions are. */
fun Context.openAppSettings() {
    startActivity(
        Intent(
            Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
            Uri.fromParts("package", packageName, null),
        )
    )
}

/** The extra carrying the [Recovery] a panel failure opens Speecher for. */
const val RECOVERY = "recovery"

private const val SIGN_IN_AFTER_PROMPT = "sign-in-after-prompt"
private const val SIGN_IN_FROM = "sign-in-from"
private const val SETTINGS_PAGE = "settings-page"

private fun emptyStatus() = SetupStatus(emptySet(), false, false, false)
