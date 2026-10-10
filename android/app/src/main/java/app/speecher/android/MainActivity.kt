package app.speecher.android

import android.Manifest
import android.accessibilityservice.AccessibilityServiceInfo
import android.content.ComponentName
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageInstaller
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
import androidx.compose.runtime.snapshotFlow
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import app.speecher.android.auth.SignInViewModel
import app.speecher.android.auth.TokenStore
import app.speecher.android.dictation.ActiveDictation
import app.speecher.android.dictation.DictationRecord
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SettingsStore
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.SpeechProvider
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.clearInsights
import app.speecher.android.dictation.insightsFile
import app.speecher.android.dictation.loadInsights
import app.speecher.android.dictation.oauth
import app.speecher.android.dictation.sharedHttp
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
import app.speecher.android.ui.SignInStepsSheet
import app.speecher.android.ui.SpeecherScreen
import app.speecher.android.ui.SpeecherTheme
import app.speecher.android.ui.WhatsNew
import app.speecher.android.update.AndroidRelease
import app.speecher.android.update.ApkUpdate
import app.speecher.android.update.INSTALL_STATUS
import app.speecher.android.update.INSTALL_VERSION
import app.speecher.android.update.RELEASES_PAGE
import app.speecher.android.update.RETRY_MILLIS
import app.speecher.android.update.UpdateChannel
import app.speecher.android.update.UpdateState
import app.speecher.android.update.androidReleases
import app.speecher.android.update.commitApk
import app.speecher.android.update.downloadApk
import app.speecher.android.update.installFailure
import app.speecher.android.update.installing
import app.speecher.android.update.newerApk
import app.speecher.android.update.releaseNotes
import app.speecher.android.update.stageApk
import app.speecher.android.update.untilCheck
import app.speecher.android.update.whatsNewSince
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning
import java.io.File
import kotlin.coroutines.cancellation.CancellationException
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
    private var updateState by mutableStateOf<UpdateState>(UpdateState.Idle)
    // The offer Home no longer shows, until a newer version comes along.
    private var dismissedVersion by mutableStateOf<String?>(null)
    // The version an upgrade came from, until Home's What's New card is opened or dismissed.
    private var whatsNewPending by mutableStateOf<String?>(null)
    // What the open What's New page covers changes since, and its notes, null while they load.
    private var notesSince: String? = null
    private var whatsNewNotes by mutableStateOf<Result<List<AndroidRelease>>?>(null)
    private var whatsNewFrom = Page.Home
    // Counts checks begun and channel changes, so only the latest check reports what it found.
    private var checkCount = 0
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
        recordRun()
        showSavedUpdate()
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
                                update = homeUpdate,
                                onUpdate = ::runUpdateAction,
                                onDismissUpdate = ::dismissUpdate,
                                whatsNewVersion =
                                    BuildConfig.VERSION_NAME.takeIf { whatsNewPending != null },
                                onOpenWhatsNew = ::openWhatsNew,
                                onDismissWhatsNew = ::dismissWhatsNew,
                                latest = ActiveDictation.latest,
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
                                ::openAppSettings,
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
                                    updateState = updateState,
                                    onCheckForUpdates = ::runUpdateAction,
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
                                whatsNewNotes,
                                ::loadWhatsNew,
                                // A Nightly Build has no notes of its own, as on the desktop.
                                onViewReleases =
                                    ::openReleases.takeIf {
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
        // Only while the app is in the foreground and checks are on: on opening or returning to it
        // once the interval has passed, then each time it passes again. A new interval applies at
        // once, and a new channel checks at once.
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                snapshotFlow {
                    Triple(
                        settings.autoCheckUpdates,
                        settings.updateCheckMinutes,
                        settings.updateChannel,
                    )
                }
                    .collectLatest { (automatic, minutes) ->
                        if (!automatic) return@collectLatest
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
            // The install waiting on this answer, or its offer once the activity was recreated.
            val state = updateState
            val update =
                state.update?.takeIf {
                    it.version == intent.getStringExtra(INSTALL_VERSION) &&
                        (state is UpdateState.Installing || state is UpdateState.Available)
                }
            if (update != null) {
                updateState = installFailure(update, intent.getIntExtra(INSTALL_STATUS, 0))
            }
            return
        }
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
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.CINNAMON_BUN) {
            startScanner()
            return
        }
        val permission = Manifest.permission.ACCESS_LOCAL_NETWORK
        when {
            checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED -> startScanner()
            // Denied twice, Android stops asking, so only Speecher's settings can allow it.
            computerImport.state == ImportState.Failed(ImportFailure.NoLocalNetwork) &&
                !shouldShowRequestPermissionRationale(permission) -> openAppSettings()
            else -> localNetwork.launch(permission)
        }
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

    private fun openAppSettings() {
        startActivity(
            Intent(
                Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                Uri.fromParts("package", packageName, null),
            )
        )
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

    private val updatePreferences by lazy { getSharedPreferences("updates", MODE_PRIVATE) }

    /** Whether this version made the last check, on the channel now chosen. */
    private val lastCheckCurrent: Boolean
        get() =
            updatePreferences.getString("installed-version", null) == BuildConfig.VERSION_NAME &&
                // Releases before the channel setting only checked Stable.
                updatePreferences.getString("channel", UpdateChannel.Stable.name) ==
                    settings.updateChannel.name

    /** The update the last check found, if this version made that check on this channel. */
    private fun showSavedUpdate() {
        dismissedVersion = updatePreferences.getString("dismissed-version", null)
        if (!lastCheckCurrent) return
        val version = updatePreferences.getString("version", null) ?: return
        val url = updatePreferences.getString("url", null) ?: return
        val page = updatePreferences.getString("page", null) ?: return
        val replacesNightly = updatePreferences.getBoolean("replaces-nightly", false)
        updateState = UpdateState.Available(ApkUpdate(version, url, page, replacesNightly))
    }

    /** What Home's card shows: anything about an update but an offer dismissed for its version. */
    private val homeUpdate: UpdateState?
        get() = updateState.takeIf {
            it.update != null &&
                !(it is UpdateState.Available && it.update.version == dismissedVersion)
        }

    /**
     * Milliseconds until the next check: none once the app itself has updated or the channel
     * changed, else what is left of [interval] since the last.
     */
    private fun untilUpdateCheck(interval: Long): Long =
        if (!lastCheckCurrent) 0
        else
            untilCheck(
                updatePreferences.getLong("last-check", 0),
                System.currentTimeMillis(),
                interval,
            )

    /**
     * Whether the check reached the releases list; only one that did counts as the last check. An
     * offer already showing stays through the check, and through a check that fails. A check the
     * user asked for can offer going back to Stable from a Nightly Build.
     */
    private suspend fun checkForUpdate(manual: Boolean = false): Boolean {
        if (updateState.installing) return false
        val check = ++checkCount
        val before = updateState
        val offer = before as? UpdateState.Available
        if (offer == null) updateState = UpdateState.Checking
        val now = System.currentTimeMillis()
        val channel = settings.updateChannel
        val result =
            try {
                withContext(Dispatchers.IO) {
                    runCatching {
                        newerApk(
                            androidReleases(sharedHttp),
                            channel,
                            BuildConfig.VERSION_NAME,
                            manual,
                        )
                    }
                }
            } catch (cancelled: CancellationException) {
                // As when automatic checks are turned off mid-check: Check now works again.
                if (check == checkCount && updateState == UpdateState.Checking) updateState = before
                throw cancelled
            }
        // A newer check, or a channel change, asked a different question.
        if (check != checkCount) return false
        // The offer an earlier version or channel found is stale either way; the new version is
        // recorded only with a check that counts, so a failure after an upgrade retries instead of
        // waiting out what was left of the old version's interval.
        if (!lastCheckCurrent) updatePreferences.edit { removeOffer() }
        // An update the user started meanwhile keeps the card.
        val applies = !updateState.installing
        return result
            .onSuccess { release ->
                if (applies)
                    updateState = release?.let(UpdateState::Available) ?: UpdateState.UpToDate
                updatePreferences.edit {
                    putLong("last-check", now)
                    putString("installed-version", BuildConfig.VERSION_NAME)
                    putString("channel", channel.name)
                    if (release == null) removeOffer()
                    else {
                        putString("version", release.version)
                        putString("url", release.downloadUrl)
                        putString("page", release.pageUrl)
                        putBoolean("replaces-nightly", release.replacesNightly)
                    }
                }
            }
            .onFailure { if (applies && offer == null) updateState = UpdateState.CheckFailed }
            .isSuccess
    }

    private fun SharedPreferences.Editor.removeOffer() {
        remove("version")
        remove("url")
        remove("page")
        remove("replaces-nightly")
    }

    /**
     * What Home's card and the Check for updates row do: install an offer, retry a failed install
     * or open its release page, and otherwise check.
     */
    private fun runUpdateAction() {
        when (val state = updateState) {
            is UpdateState.Available -> installUpdate(state.update)
            is UpdateState.InstallFailed ->
                if (state.manualInstall)
                    startActivity(Intent(Intent.ACTION_VIEW, state.update.pageUrl.toUri()))
                else installUpdate(state.update)
            else -> lifecycleScope.launch { checkForUpdate(manual = true) }
        }
    }

    private fun installUpdate(release: ApkUpdate) {
        updateState = UpdateState.Downloading(release, null)
        lifecycleScope.launch {
            val apk = File(cacheDir, "update.apk")
            val downloaded = runCatching {
                withContext(Dispatchers.IO) {
                    downloadApk(sharedHttp, release, apk) {
                        updateState = UpdateState.Downloading(release, it)
                    }
                }
            }
            if (downloaded.isFailure) {
                apk.delete()
                updateState =
                    UpdateState.InstallFailed(
                        release,
                        "Couldn't download the update. Check your connection and try again.",
                    )
                return@launch
            }
            val staged = runCatching {
                withContext(Dispatchers.IO) {
                    try {
                        stageApk(this@MainActivity, apk)
                    } finally {
                        apk.delete()
                    }
                }
            }
            val session = staged.getOrElse {
                updateState = installFailure(release, PackageInstaller.STATUS_FAILURE)
                return@launch
            }
            var committed = false
            try {
                // Replacing the app ends its process, and a dictation with it; and Android only
                // shows its prompt, or the failure, over an app on screen. Nothing suspends between
                // this check and the commit, and the chip starts dictations on this same thread.
                while (
                    ActiveDictation.engine != null ||
                        !lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)
                ) {
                    if (ActiveDictation.engine != null) {
                        updateState = UpdateState.WaitingForDictation(release)
                    }
                    delay(500)
                }
                updateState = UpdateState.Installing(release)
                committed = true
                runCatching { commitApk(this@MainActivity, session, release) }
                    .onFailure {
                        updateState = installFailure(release, PackageInstaller.STATUS_FAILURE)
                    }
            } finally {
                // Left before the commit, as when the activity goes: nothing will commit it.
                if (!committed) packageManager.packageInstaller.abandonSession(session)
            }
        }
    }

    private fun dismissUpdate() {
        val update = updateState.update ?: return
        dismissedVersion = update.version
        updatePreferences.edit { putString("dismissed-version", update.version) }
        updateState = UpdateState.Available(update)
    }

    /** Notes an upgrade since the last run for Home's What's New card, and this run as the last. */
    private fun recordRun() {
        // Releases before What's New recorded only the version that made the last check.
        val previous =
            updatePreferences.getString("last-run-version", null)
                ?: updatePreferences.getString("installed-version", null)
        whatsNewPending =
            whatsNewSince(
                previous,
                BuildConfig.VERSION_NAME,
                updatePreferences.getString("whats-new-since", null),
            )
        updatePreferences.edit {
            putString("last-run-version", BuildConfig.VERSION_NAME)
            putString("whats-new-since", whatsNewPending)
        }
    }

    private fun openWhatsNew() {
        notesSince = whatsNewPending
        dismissWhatsNew()
        whatsNewFrom = page
        page = Page.WhatsNew
        loadWhatsNew()
    }

    private fun dismissWhatsNew() {
        whatsNewPending = null
        updatePreferences.edit { remove("whats-new-since") }
    }

    private fun loadWhatsNew() {
        whatsNewNotes = null
        lifecycleScope.launch {
            whatsNewNotes =
                withContext(Dispatchers.IO) {
                    runCatching {
                        releaseNotes(
                            androidReleases(sharedHttp),
                            notesSince,
                            BuildConfig.VERSION_NAME,
                        )
                    }
                }
        }
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

    private fun openReleases() {
        startActivity(Intent(Intent.ACTION_VIEW, RELEASES_PAGE.toUri()))
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
        // What the other channel offered, or is still looking for, no longer applies.
        if (changed.updateChannel != settings.updateChannel) {
            checkCount++
            if (!updateState.installing) updateState = UpdateState.Idle
        }
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
private const val SETTINGS_PAGE = "settings-page"

private fun emptyStatus() = SetupStatus(emptySet(), false, false, false)
