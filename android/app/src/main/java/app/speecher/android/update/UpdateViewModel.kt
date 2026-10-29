package app.speecher.android.update

import android.app.Application
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageInstaller
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.core.content.edit
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import app.speecher.android.BuildConfig
import app.speecher.android.dictation.ActiveDictation
import app.speecher.android.settings.SpeecherSettings
import app.speecher.android.sharedHttp
import java.io.File
import kotlin.coroutines.cancellation.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * The app's own updates: checking for one, the offer the last check found (kept across runs),
 * downloading and installing it, and What's New after an upgrade. Kept outside the activity so an
 * update under way survives the activity being recreated.
 */
class UpdateViewModel(application: Application) : AndroidViewModel(application) {
    private val preferences = application.getSharedPreferences("updates", Context.MODE_PRIVATE)

    var state by mutableStateOf<UpdateState>(UpdateState.Idle)
        private set

    // The offer Home no longer shows, until a newer version comes along.
    private var dismissedVersion by mutableStateOf(preferences.getString(DISMISSED_VERSION, null))

    /**
     * Whether MainActivity is started, which it sets itself. The install waits for it; the process
     * lifecycle would report a stop 700 ms late.
     */
    var onScreen = false

    /** The version an upgrade came from, until Home's What's New card is opened or dismissed. */
    var whatsNewPending by mutableStateOf<String?>(null)
        private set

    /** The open What's New page's notes, null while they load. */
    var whatsNewNotes by mutableStateOf<Result<List<AndroidRelease>>?>(null)
        private set

    // What the open What's New page covers changes since.
    private var notesSince: String? = null
    // Counts checks begun and channel changes, so only the latest check reports what it found.
    private var checkCount = 0

    init {
        recordRun()
    }

    /** What Home's card shows: anything about an update but an offer dismissed for its version. */
    val homeCard: UpdateState?
        get() = state.takeIf {
            it.update != null &&
                !(it is UpdateState.Available && it.update.version == dismissedVersion)
        }

    /** Shows the offer the last check found, if this version made that check on [channel]. */
    fun showSavedOffer(channel: UpdateChannel) {
        if (state != UpdateState.Idle || !lastCheckCurrent(channel)) return
        state = preferences.savedOffer()?.let(UpdateState::Available) ?: return
    }

    /**
     * Checks for as long as the caller runs this: at once when the interval has passed since the
     * last check, then each time it passes again, while [settings] has automatic checks on. A new
     * interval applies at once, and a new channel checks at once. [settings] is read as Compose
     * state.
     */
    suspend fun checkPeriodically(settings: () -> SpeecherSettings) {
        snapshotFlow {
            settings().let { Triple(it.autoCheckUpdates, it.updateCheckMinutes, it.updateChannel) }
        }
            .collectLatest { (automatic, minutes, channel) ->
                if (!automatic) return@collectLatest
                val interval = minutes * 60_000L
                while (true) {
                    delay(untilNextCheck(channel, interval))
                    // A failed check is not a check: try again soon, not an interval later.
                    if (!check(channel)) delay(minOf(interval, RETRY_MILLIS))
                }
            }
    }

    /** Drops what the previous channel offered or is still looking for, which no longer applies. */
    fun changeChannel() {
        checkCount++
        if (!state.installing) state = UpdateState.Idle
    }

    /**
     * What Home's card and the Check for updates row do: install an offer, retry a failed install
     * or [openPage] its release page, and otherwise check on [channel].
     */
    fun proceed(channel: UpdateChannel, openPage: (url: String) -> Unit) {
        when (val current = state) {
            is UpdateState.Available -> install(current.update)
            is UpdateState.InstallFailed ->
                if (current.manualInstall) openPage(current.update.pageUrl)
                else install(current.update)
            else -> viewModelScope.launch { check(channel, manual = true) }
        }
    }

    fun dismissOffer() {
        val update = state.update ?: return
        dismissedVersion = update.version
        preferences.edit { putString(DISMISSED_VERSION, update.version) }
        state = UpdateState.Available(update)
    }

    /** Shows why Android refused [version] with a PackageInstaller [status]. */
    fun installRefused(version: String?, status: Int) {
        // The install waiting on this answer, or its offer once the process was recreated.
        val current = state
        val update =
            current.update?.takeIf {
                it.version == version &&
                    (current is UpdateState.Installing || current is UpdateState.Available)
            } ?: return
        state = installFailure(update, status)
    }

    /** Takes What's New's pending upgrade off Home and loads its notes for the page. */
    fun openWhatsNew() {
        notesSince = whatsNewPending
        dismissWhatsNew()
        loadWhatsNew()
    }

    fun dismissWhatsNew() {
        whatsNewPending = null
        preferences.edit { remove(WHATS_NEW_SINCE) }
    }

    fun loadWhatsNew() {
        whatsNewNotes = null
        viewModelScope.launch {
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

    /** Whether this version made the last check, on [channel]. */
    private fun lastCheckCurrent(channel: UpdateChannel): Boolean =
        preferences.getString(INSTALLED_VERSION, null) == BuildConfig.VERSION_NAME &&
            // Releases before the channel setting only checked Stable.
            preferences.getString(CHANNEL, UpdateChannel.Stable.name) == channel.name

    /**
     * Milliseconds until the next check: none once the app itself has updated or the channel
     * changed, else what is left of [interval] since the last.
     */
    private fun untilNextCheck(channel: UpdateChannel, interval: Long): Long =
        if (!lastCheckCurrent(channel)) 0
        else untilCheck(preferences.getLong(LAST_CHECK, 0), System.currentTimeMillis(), interval)

    /**
     * Whether the check reached the releases list; only one that did counts as the last check. An
     * offer already showing stays through the check, and through a check that fails. A check the
     * user asked for can offer going back to Stable from a Nightly Build.
     */
    private suspend fun check(channel: UpdateChannel, manual: Boolean = false): Boolean {
        if (state.installing) return false
        val check = ++checkCount
        val before = state
        val offer = before as? UpdateState.Available
        if (offer == null) state = UpdateState.Checking
        val now = System.currentTimeMillis()
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
                if (check == checkCount && state == UpdateState.Checking) state = before
                throw cancelled
            }
        // A newer check, or a channel change, asked a different question.
        if (check != checkCount) return false
        // The offer an earlier version or channel found is stale either way; the new version is
        // recorded only with a check that counts, so a failure after an upgrade retries instead of
        // waiting out what was left of the old version's interval.
        if (!lastCheckCurrent(channel)) preferences.edit { removeOffer() }
        // An update the user started meanwhile keeps the card.
        val applies = !state.installing
        return result
            .onSuccess { release ->
                if (applies) state = release?.let(UpdateState::Available) ?: UpdateState.UpToDate
                preferences.edit {
                    putLong(LAST_CHECK, now)
                    putString(INSTALLED_VERSION, BuildConfig.VERSION_NAME)
                    putString(CHANNEL, channel.name)
                    if (release == null) removeOffer() else putOffer(release)
                }
            }
            .onFailure { if (applies && offer == null) state = UpdateState.CheckFailed }
            .isSuccess
    }

    private fun install(release: ApkUpdate) {
        val context = getApplication<Application>()
        state = UpdateState.Downloading(release, null)
        viewModelScope.launch {
            val apk = File(context.cacheDir, "update.apk")
            val downloaded = runCatching {
                withContext(Dispatchers.IO) {
                    downloadApk(sharedHttp, release, apk) {
                        state = UpdateState.Downloading(release, it)
                    }
                }
            }
            if (downloaded.isFailure) {
                apk.delete()
                state =
                    UpdateState.InstallFailed(
                        release,
                        "Couldn't download the update. Check your connection and try again.",
                    )
                return@launch
            }
            val staged = runCatching {
                withContext(Dispatchers.IO) {
                    try {
                        stageApk(context, apk)
                    } finally {
                        apk.delete()
                    }
                }
            }
            val session = staged.getOrElse {
                state = installFailure(release, PackageInstaller.STATUS_FAILURE)
                return@launch
            }
            var committed = false
            try {
                // Replacing the app ends its process, and a dictation with it; and Android only
                // shows its prompt, or the failure, over an app on screen. Nothing suspends between
                // this check and the commit, and the chip starts dictations on this same thread.
                while (ActiveDictation.session != null || !onScreen) {
                    if (ActiveDictation.session != null) {
                        state = UpdateState.WaitingForDictation(release)
                    }
                    delay(500)
                }
                state = UpdateState.Installing(release)
                committed = true
                runCatching { commitApk(context, session, release) }
                    .onFailure { state = installFailure(release, PackageInstaller.STATUS_FAILURE) }
            } finally {
                // Left before the commit, as when Speecher is closed: nothing will commit it.
                if (!committed) context.packageManager.packageInstaller.abandonSession(session)
            }
        }
    }

    /** Notes an upgrade since the last run for Home's What's New card, and this run as the last. */
    private fun recordRun() {
        // Releases before What's New recorded only the version that made the last check.
        val previous =
            preferences.getString(LAST_RUN_VERSION, null)
                ?: preferences.getString(INSTALLED_VERSION, null)
        whatsNewPending =
            whatsNewSince(
                previous,
                BuildConfig.VERSION_NAME,
                preferences.getString(WHATS_NEW_SINCE, null),
            )
        preferences.edit {
            putString(LAST_RUN_VERSION, BuildConfig.VERSION_NAME)
            putString(WHATS_NEW_SINCE, whatsNewPending)
        }
    }
}

// Keys in the "updates" preferences. Earlier releases wrote them, so they keep these values.
private const val LAST_CHECK = "last-check"
private const val INSTALLED_VERSION = "installed-version"
private const val CHANNEL = "channel"
private const val DISMISSED_VERSION = "dismissed-version"
private const val LAST_RUN_VERSION = "last-run-version"
private const val WHATS_NEW_SINCE = "whats-new-since"
private const val OFFER_VERSION = "version"
private const val OFFER_URL = "url"
private const val OFFER_PAGE = "page"
private const val OFFER_REPLACES_NIGHTLY = "replaces-nightly"

private fun SharedPreferences.savedOffer(): ApkUpdate? {
    val version = getString(OFFER_VERSION, null) ?: return null
    val url = getString(OFFER_URL, null) ?: return null
    val page = getString(OFFER_PAGE, null) ?: return null
    return ApkUpdate(version, url, page, getBoolean(OFFER_REPLACES_NIGHTLY, false))
}

private fun SharedPreferences.Editor.putOffer(update: ApkUpdate) {
    putString(OFFER_VERSION, update.version)
    putString(OFFER_URL, update.downloadUrl)
    putString(OFFER_PAGE, update.pageUrl)
    putBoolean(OFFER_REPLACES_NIGHTLY, update.replacesNightly)
}

private fun SharedPreferences.Editor.removeOffer() {
    remove(OFFER_VERSION)
    remove(OFFER_URL)
    remove(OFFER_PAGE)
    remove(OFFER_REPLACES_NIGHTLY)
}
